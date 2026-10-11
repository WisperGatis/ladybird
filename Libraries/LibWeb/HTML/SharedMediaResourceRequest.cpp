/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Headers.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/HTML/SharedMediaResourceRequest.h>
#include <LibWeb/MimeSniff/MimeType.h>
#include <LibWeb/Page/Page.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(SharedMediaResourceRequest);

static URL::URL media_url_for_diagnostics(URL::URL url)
{
    // Media URLs often carry signed authorization tokens and user-specific query data.
    url.set_query({});
    url.set_fragment({});
    return url;
}

GC::Ref<SharedMediaResourceRequest> SharedMediaResourceRequest::get_or_create(JS::Realm& realm, GC::Ref<Page> page, URL::URL const& url, CORSSettingAttribute cors_setting, URL::Origin const& origin)
{
    auto document = Bindings::principal_host_defined_environment_settings_object(realm).responsible_document();
    VERIFY(document);
    auto& shared_media_resource_requests = document->shared_media_resource_requests();
    ListOfAvailableImages::Key key;
    key.url = url.to_string();
    key.mode = cors_setting;
    if (cors_setting != CORSSettingAttribute::NoCORS)
        key.origin = origin;

    if (auto it = shared_media_resource_requests.find(key); it != shared_media_resource_requests.end())
        return *it->value;
    auto request = realm.create<SharedMediaResourceRequest>(page, url, move(key), *document);
    shared_media_resource_requests.set(request->m_key, request);
    return request;
}

SharedMediaResourceRequest::SharedMediaResourceRequest(GC::Ref<Page> page, URL::URL url, ListOfAvailableImages::Key key, GC::Ref<DOM::Document> document)
    : m_page(page)
    , m_url(move(url))
    , m_key(move(key))
    , m_document(document)
{
}

SharedMediaResourceRequest::~SharedMediaResourceRequest() = default;

void SharedMediaResourceRequest::finalize()
{
    Base::finalize();
    // NOTE: A cancelled request may already have been replaced by a newer request for the same URL.
    auto& shared_media_resource_requests = m_document->shared_media_resource_requests();
    if (auto it = shared_media_resource_requests.find(m_key); it != shared_media_resource_requests.end() && it->value == this)
        shared_media_resource_requests.remove(m_key);
}

void SharedMediaResourceRequest::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_fetch_controller);
    visitor.visit(m_document);
    visitor.visit(m_page);
    for (auto& callback : m_callbacks) {
        visitor.visit(callback.on_finish);
        visitor.visit(callback.on_fail);
    }
}

void SharedMediaResourceRequest::fetch_resource(JS::Realm& realm, GC::Ref<Fetch::Infrastructure::Request> request)
{
    Fetch::Infrastructure::FetchAlgorithms::Input fetch_algorithms_input {};
    fetch_algorithms_input.process_response = [this, &realm](GC::Ref<Fetch::Infrastructure::Response> response) {
        m_is_cors_cross_origin = response->is_cors_cross_origin();
        // FIXME: If the response is CORS cross-origin, we must use its internal response to query any of its data. See:
        //        https://github.com/whatwg/html/issues/9355
        response = response->unsafe_response();

        if (response->is_network_error()) {
            handle_failed_fetch(response->network_error_message().value_or("Failed to fetch media resource"_string));
            return;
        }

        auto resolved_url = response->url().has_value() ? response->url().value() : m_url;
        dbgln("Media response: status {}, URL {}", response->status(), media_url_for_diagnostics(resolved_url));

        auto content_type_header = response->header_list()->get("Content-Type"sv.bytes());

        VERIFY(response->body());
        auto& global = realm.global_object();

        auto process_body = GC::create_function(heap(), [this, resolved_url, content_type_header](ByteBuffer data) {
            classify_and_finish(resolved_url, content_type_header, move(data));
        });
        auto process_body_error = GC::create_function(heap(), [this](JS::Value) {
            handle_failed_fetch("Failed to fetch media resource"_string);
        });

        response->body()->fully_read(realm, process_body, process_body_error, GC::Ref { global });
    };

    m_state = State::Fetching;

    auto fetch_controller = Fetch::Fetching::fetch(
        realm,
        request,
        Fetch::Infrastructure::FetchAlgorithms::create(realm.vm(), move(fetch_algorithms_input)))
                                 .release_value_but_fixme_should_propagate_errors();

    m_fetch_controller = move(fetch_controller);
}

void SharedMediaResourceRequest::classify_and_finish(URL::URL resolved_url, Optional<ByteBuffer> content_type_header, ByteBuffer data)
{
    if (m_state == State::Finished || m_state == State::Failed)
        return;

    auto url_string = resolved_url.to_string();
    auto url_string_view = url_string.bytes_as_string_view();

    // Some download endpoints (including paths ending in `.mp4/`) return an HLS-like content
    // type. Prefer the concrete media URL suffix so those resources continue through the
    // normal browser fetch path.
    auto is_regular_media_url = [&] {
        for (auto extension : { ".mp4"sv, ".webm"sv, ".mkv"sv, ".ogv"sv, ".mov"sv }) {
            if (url_string_view.contains(extension, CaseSensitivity::CaseInsensitive))
                return true;
        }
        return false;
    }();

    bool is_hls = false;
    StringView detection_method = "none"sv;
    if (content_type_header.has_value()) {
        auto mime_type = MimeSniff::MimeType::parse(StringView { content_type_header.value() });
        if (mime_type.has_value()) {
            auto const& essence = mime_type->essence();
            is_hls = !is_regular_media_url && (essence == "application/vnd.apple.mpegurl"sv || essence == "application/x-mpegurl"sv || essence == "audio/mpegurl"sv || essence == "audio/x-mpegurl"sv);
            if (is_hls)
                detection_method = "mime"sv;
        }
    }
    // Also check URL extension as fallback
    if (!is_hls) {
        is_hls = url_string_view.ends_with(".m3u8"sv, CaseSensitivity::CaseInsensitive) || url_string_view.ends_with(".m3u"sv, CaseSensitivity::CaseInsensitive);
        if (is_hls)
            detection_method = "extension"sv;
    }

    // Some players serve playlists from opaque URLs with a generic MIME type. Sniff only a
    // small prefix for the HLS signature before selecting the HLS loader. This avoids
    // treating arbitrary media bytes as HLS. Some HLS providers intentionally use an
    // MP4-looking URL for a playlist (PerfectGirls currently does this). A real playlist
    // signature is stronger evidence than the misleading suffix, so sniff the response body
    // even when the URL resembles regular media. A valid MP4 body cannot begin with #EXTM3U.
    if (!is_hls) {
        auto sniff_length = min(data.size(), 4096uz);
        auto prefix = StringView { data.data(), sniff_length };
        is_hls = prefix.contains("#EXTM3U"sv);
        if (is_hls)
            detection_method = "signature"sv;
    }

    dbgln("Media response classification: URL {}, HLS {}, detected-by {}", media_url_for_diagnostics(resolved_url), is_hls, detection_method);

    m_resolved_url = move(resolved_url);
    m_is_hls = is_hls;
    m_data = move(data);
    m_state = State::Finished;

    for (auto& callback : m_callbacks) {
        if (callback.on_finish)
            callback.on_finish->function()(m_resolved_url, m_is_hls, m_data);
    }
    m_callbacks.clear();
}

void SharedMediaResourceRequest::handle_failed_fetch(String error_message)
{
    if (m_state == State::Failed)
        return;

    dbgln("SharedMediaResourceRequest: failed media resource {}: {}", media_url_for_diagnostics(m_url), error_message);
    m_state = State::Failed;
    m_error_message = move(error_message);
    for (auto& callback : m_callbacks) {
        if (callback.on_fail)
            callback.on_fail->function()(m_error_message);
    }
    m_callbacks.clear();
}

u64 SharedMediaResourceRequest::add_callbacks(Function<void(URL::URL const&, bool, ByteBuffer const&)> on_finish, Function<void(String)> on_fail)
{
    if (m_state == State::Finished) {
        if (on_finish)
            on_finish(m_resolved_url, m_is_hls, m_data);
        return 0;
    }

    if (m_state == State::Failed) {
        if (on_fail)
            on_fail(m_error_message);
        return 0;
    }

    Callbacks callbacks;
    callbacks.id = m_next_callbacks_id++;
    if (on_finish)
        callbacks.on_finish = GC::create_function(vm().heap(), move(on_finish));
    if (on_fail)
        callbacks.on_fail = GC::create_function(vm().heap(), move(on_fail));

    auto id = callbacks.id;
    m_callbacks.append(move(callbacks));
    return id;
}

void SharedMediaResourceRequest::remove_callbacks(u64 id)
{
    if (id == 0)
        return;

    m_callbacks.remove_all_matching([&](auto const& callbacks) { return callbacks.id == id; });

    if (m_state != State::Fetching || !m_callbacks.is_empty())
        return;

    // Nobody is waiting for this resource any more. Abort the download and forget this request, so that a later
    // request for the same URL starts a fresh fetch instead of observing the aborted one.
    m_state = State::Failed;
    m_error_message = "Media fetch was cancelled"_string;
    if (m_fetch_controller && m_fetch_controller->state() == Fetch::Infrastructure::FetchController::State::Ongoing)
        m_fetch_controller->stop_fetch();
    auto& shared_media_resource_requests = m_document->shared_media_resource_requests();
    if (auto it = shared_media_resource_requests.find(m_key); it != shared_media_resource_requests.end() && it->value == this)
        shared_media_resource_requests.remove(m_key);
}

bool SharedMediaResourceRequest::needs_fetching() const
{
    return m_state == State::New;
}

bool SharedMediaResourceRequest::is_fetching() const
{
    return m_state == State::Fetching;
}

}

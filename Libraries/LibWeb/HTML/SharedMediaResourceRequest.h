/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Function.h>
#include <LibGC/Function.h>
#include <LibGC/Ptr.h>
#include <LibJS/Heap/Cell.h>
#include <LibURL/URL.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/ListOfAvailableImages.h>

namespace Web::HTML {

// Shares a single network fetch of a media resource's bytes across every <video>/<audio>
// element that requests the same URL. Ad networks and thumbnail grids commonly reuse the
// same creative across several elements on a page; without this, each element downloads
// the identical file independently.
class SharedMediaResourceRequest final : public JS::Cell {
    GC_CELL(SharedMediaResourceRequest, JS::Cell);
    GC_DECLARE_ALLOCATOR(SharedMediaResourceRequest);

public:
    [[nodiscard]] static GC::Ref<SharedMediaResourceRequest> get_or_create(JS::Realm&, GC::Ref<Page>, URL::URL const&, CORSSettingAttribute, URL::Origin const&);

    virtual ~SharedMediaResourceRequest() override;

    URL::URL const& url() const { return m_url; }
    [[nodiscard]] bool is_cors_cross_origin() const { return m_is_cors_cross_origin; }

    void fetch_resource(JS::Realm&, GC::Ref<Fetch::Infrastructure::Request>);

    // on_finish is invoked with the resolved URL, whether the resource was classified as an
    // HLS stream, and the fetched bytes (valid only for the duration of the call).
    // Returns an id that can be passed to remove_callbacks() if the caller stops caring about the result. If the
    // resource has already finished or failed, the callbacks are invoked immediately and 0 is returned.
    u64 add_callbacks(Function<void(URL::URL const&, bool is_hls, ByteBuffer const&)> on_finish, Function<void(String)> on_fail);

    // Removes a previously added consumer. If it was the last one waiting on an in-flight fetch, the fetch is aborted so
    // that abandoned media (for example a superseded or navigated-away video) does not keep downloading.
    void remove_callbacks(u64 id);

    bool is_fetching() const;
    bool needs_fetching() const;

private:
    explicit SharedMediaResourceRequest(GC::Ref<Page>, URL::URL, ListOfAvailableImages::Key, GC::Ref<DOM::Document>);

    virtual void finalize() override;
    virtual void visit_edges(JS::Cell::Visitor&) override;

    void classify_and_finish(URL::URL resolved_url, Optional<ByteBuffer> content_type_header, ByteBuffer data);
    void handle_failed_fetch(String error_message);

    enum class State {
        New,
        Fetching,
        Finished,
        Failed,
    };

    State m_state { State::New };
    u64 m_next_callbacks_id { 1 };

    GC::Ref<Page> m_page;

    struct Callbacks {
        u64 id { 0 };
        GC::Ptr<GC::Function<void(URL::URL const&, bool, ByteBuffer const&)>> on_finish;
        GC::Ptr<GC::Function<void(String)>> on_fail;
    };
    Vector<Callbacks> m_callbacks;

    URL::URL m_url;
    ListOfAvailableImages::Key m_key;
    URL::URL m_resolved_url;
    bool m_is_hls { false };
    ByteBuffer m_data;
    String m_error_message;
    bool m_is_cors_cross_origin { false };
    GC::Ptr<Fetch::Infrastructure::FetchController> m_fetch_controller;

    GC::Ptr<DOM::Document> m_document;
};

}

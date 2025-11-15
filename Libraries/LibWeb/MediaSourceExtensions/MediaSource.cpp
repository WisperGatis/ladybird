/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/MediaSourceExtensions/SourceBufferList.h>
#include <LibWeb/MimeSniff/MimeType.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(MediaSource);

WebIDL::ExceptionOr<GC::Ref<MediaSource>> MediaSource::construct_impl(JS::Realm& realm)
{
    return realm.create<MediaSource>(realm);
}

MediaSource::MediaSource(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

MediaSource::~MediaSource() = default;

void MediaSource::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaSource);
    Base::initialize(realm);

    m_source_buffers = realm.create<SourceBufferList>(realm);
    m_active_source_buffers = realm.create<SourceBufferList>(realm);
}

void MediaSource::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_source_buffers);
    visitor.visit(m_active_source_buffers);
    visitor.visit(m_media_element);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
void MediaSource::set_onsourceopen(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceopen, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceopen()
{
    return event_handler_attribute(EventNames::sourceopen);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
void MediaSource::set_onsourceended(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceended, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceended()
{
    return event_handler_attribute(EventNames::sourceended);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
void MediaSource::set_onsourceclose(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceclose, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceclose()
{
    return event_handler_attribute(EventNames::sourceclose);
}

// https://w3c.github.io/media-source/#dom-mediasource-sourcebuffers
GC::Ptr<SourceBufferList> MediaSource::source_buffers()
{
    return m_source_buffers;
}

// https://w3c.github.io/media-source/#dom-mediasource-activesourcebuffers
GC::Ptr<SourceBufferList> MediaSource::active_source_buffers()
{
    return m_active_source_buffers;
}

// https://w3c.github.io/media-source/#dom-mediasource-duration
WebIDL::ExceptionOr<void> MediaSource::set_duration(double new_duration)
{
    // FIXME: Implement full duration setting algorithm
    // For now, just update the duration if we're open
    if (m_ready_state != Bindings::ReadyState::Open)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "MediaSource is not open"sv };

    m_duration = new_duration;
    return {};
}

// https://w3c.github.io/media-source/#dom-mediasource-addsourcebuffer
WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> MediaSource::add_source_buffer(String const& type)
{
    // 1. If type is an empty string then throw a TypeError exception and abort these steps.
    if (type.is_empty())
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Type string is empty"sv };

    // 2. If type contains a MIME type that is not supported ..., then throw a NotSupportedError exception and abort these steps.
    if (!is_type_supported(vm(), type))
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Type is not supported"sv };

    // 3. If the user agent can't handle any more SourceBuffer objects then throw a QuotaExceededError exception and abort these steps.
    // FIXME: Implement quota checking

    // 4. If the readyState attribute is not in the "open" state then throw an InvalidStateError exception and abort these steps.
    if (m_ready_state != Bindings::ReadyState::Open)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "MediaSource is not open"sv };

    // 5. Create a new SourceBuffer object and associated resources.
    auto source_buffer = realm().create<SourceBuffer>(realm(), type);
    source_buffer->set_parent_media_source(*this);

    // 6. Add the new object to sourceBuffers and fire addsourcebuffer on that object.
    m_source_buffers->add(source_buffer);

    // FIXME: 7. Return the new object.
    return source_buffer;
}

// https://w3c.github.io/media-source/#dom-mediasource-removesourcebuffer
WebIDL::ExceptionOr<void> MediaSource::remove_source_buffer(GC::Ref<SourceBuffer> source_buffer)
{
    // 1. If sourceBuffer specifies an object that is not in sourceBuffers then throw a NotFoundError exception and abort these steps.
    if (!m_source_buffers->contains(source_buffer))
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "SourceBuffer not found"sv };

    // 2. If the sourceBuffer.updating attribute equals true, then run the following steps: ...
    // FIXME: Implement proper removal logic

    // 3. Let SourceBuffer audioTracks list equal the AudioTrackList object returned by sourceBuffer.audioTracks.
    // FIXME: Handle audio/video tracks

    // 4. Remove all the tracks in the SourceBuffer audioTracks list from the audioTracks attribute of the HTMLMediaElement.
    // FIXME: Implement track removal

    // 5. Remove sourceBuffer from sourceBuffers and fire removesourcebuffer on that object.
    m_source_buffers->remove(source_buffer);

    return {};
}

// https://w3c.github.io/media-source/#dom-mediasource-endofstream
WebIDL::ExceptionOr<void> MediaSource::end_of_stream(Optional<Bindings::EndOfStreamError>)
{
    // 1. If the readyState attribute is not in the "open" state then throw an InvalidStateError exception and abort these steps.
    if (m_ready_state != Bindings::ReadyState::Open)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "MediaSource is not open"sv };

    // 2. If the updating attribute equals true on any SourceBuffer in sourceBuffers, then throw an InvalidStateError exception and abort these steps.
    // FIXME: Check if any SourceBuffer is updating

    // 3. Run the end of stream algorithm with the error parameter set to error.
    // FIXME: Handle error parameter
    m_ready_state = Bindings::ReadyState::Ended;

    // Fire sourceended event
    dispatch_event(DOM::Event::create(realm(), EventNames::sourceended));

    return {};
}

// https://w3c.github.io/media-source/#dom-mediasource-istypesupported
bool MediaSource::is_type_supported(JS::VM&, String const& type)
{
    // 1. If type is an empty string, then return false.
    if (type.is_empty())
        return false;

    // 2. If type does not contain a valid MIME type string, then return false.
    auto mime_type = MimeSniff::MimeType::parse(type);
    if (!mime_type.has_value())
        return false;

    // FIXME: 3. If type contains a media type or media subtype that the MediaSource does not support, then
    //    return false.

    // FIXME: 4. If type contains a codec that the MediaSource does not support, then return false.

    // FIXME: 5. If the MediaSource does not support the specified combination of media type, media
    //    subtype, and codecs then return false.

    // 6. Return true.
    return true;
}

// Non-standard: Attach to HTMLMediaElement
void MediaSource::attach_to_element(GC::Ref<HTML::HTMLMediaElement> element)
{
    m_media_element = element;
    m_ready_state = Bindings::ReadyState::Open;

    // Dispatch sourceopen event asynchronously
    HTML::queue_global_task(HTML::Task::Source::DOMManipulation, element, GC::create_function(heap(), [this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::sourceopen));
    }));
}

// Non-standard: Detach from HTMLMediaElement
void MediaSource::detach_from_element()
{
    m_media_element = nullptr;
    m_ready_state = Bindings::ReadyState::Closed;

    // Dispatch sourceclose event
    dispatch_event(DOM::Event::create(realm(), EventNames::sourceclose));
}

}

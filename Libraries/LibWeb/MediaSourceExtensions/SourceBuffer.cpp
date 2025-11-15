/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/TimeRanges.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(SourceBuffer);

GC::Ref<SourceBuffer> SourceBuffer::create(JS::Realm& realm, String const& type)
{
    return realm.create<SourceBuffer>(realm, type);
}

SourceBuffer::SourceBuffer(JS::Realm& realm, String const& type)
    : DOM::EventTarget(realm)
    , m_mime_type(type)
{
}

SourceBuffer::~SourceBuffer() = default;

void SourceBuffer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SourceBuffer);
    Base::initialize(realm);
}

void SourceBuffer::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_parent_media_source);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdatestart
void SourceBuffer::set_onupdatestart(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::updatestart, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdatestart
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdatestart()
{
    return event_handler_attribute(EventNames::updatestart);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdate
void SourceBuffer::set_onupdate(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::update, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdate
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdate()
{
    return event_handler_attribute(EventNames::update);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdateend
void SourceBuffer::set_onupdateend(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::updateend, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdateend
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdateend()
{
    return event_handler_attribute(EventNames::updateend);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onerror
void SourceBuffer::set_onerror(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::error, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onerror
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onerror()
{
    return event_handler_attribute(EventNames::error);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onabort
void SourceBuffer::set_onabort(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::abort, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onabort
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onabort()
{
    return event_handler_attribute(EventNames::abort);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-appendbuffer
WebIDL::ExceptionOr<void> SourceBuffer::append_buffer(GC::Root<WebIDL::BufferSource> const& data)
{
    // 1. If this object has been removed from the sourceBuffers attribute of the parent media source,
    //    then throw an InvalidStateError exception and abort these steps.
    if (!m_parent_media_source)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "SourceBuffer has been removed"sv };

    // 2. If the updating attribute equals true, then throw an InvalidStateError exception and abort these steps.
    if (m_updating)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "SourceBuffer is updating"sv };

    // 3. If the readyState attribute of the parent media source is in the "ended" state, then run the following steps: ...
    // FIXME: Handle ended state transition

    // 4. If the readyState attribute of the parent media source is not in the "open" state, then throw an InvalidStateError exception and abort these steps.
    if (m_parent_media_source->ready_state() != Bindings::ReadyState::Open)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "MediaSource is not open"sv };

    // 5. If the HTMLMediaElement.error attribute is not null, then throw an InvalidStateError exception and abort these steps.
    // FIXME: Check media element error state

    // 6. Let data be a copy of the bytes passed to the method.
    auto data_buffer_result = WebIDL::get_buffer_source_copy(*data.cell()->raw_object());
    if (data_buffer_result.is_error()) {
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Failed to copy buffer data"sv };
    }
    m_pending_data = data_buffer_result.release_value();

    // Additional validation for HLS.js compatibility
    if (m_pending_data.is_empty()) {
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Empty buffer data"sv };
    }

    // 7. Set the updating attribute to true.
    m_updating = true;

    // 8. Queue a task to fire an event named updatestart at this SourceBuffer object.
    HTML::queue_global_task(HTML::Task::Source::DOMManipulation, *this, GC::create_function(heap(), [this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updatestart));
    }));

    // 9. Asynchronously run the buffer append algorithm.
    HTML::queue_global_task(HTML::Task::Source::DOMManipulation, *this, GC::create_function(heap(), [this] {
        process_append();
    }));

    return {};
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-remove
WebIDL::ExceptionOr<void> SourceBuffer::remove(double start, double end)
{
    // 1. If this object has been removed from the sourceBuffers attribute of the parent media source,
    //    then throw an InvalidStateError exception and abort these steps.
    if (!m_parent_media_source)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "SourceBuffer has been removed"sv };

    // 2. If the updating attribute equals true, then throw an InvalidStateError exception and abort these steps.
    if (m_updating)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "SourceBuffer is updating"sv };

    // 3. If the readyState attribute of the parent media source is not in the "open" state,
    //    then throw an InvalidStateError exception and abort these steps.
    if (m_parent_media_source->ready_state() != Bindings::ReadyState::Open)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "MediaSource is not open"sv };

    // 4. If start is negative or greater than end, then throw a TypeError exception and abort these steps.
    if (start < 0 || start > end)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Invalid remove range: start must be non-negative and less than or equal to end"sv };

    // 5. Set the updating attribute to true.
    m_updating = true;

    // 6. Queue a task to fire an event named updatestart at this SourceBuffer object.
    HTML::queue_global_task(HTML::Task::Source::DOMManipulation, *this, GC::create_function(heap(), [this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updatestart));
    }));

    // 7. Asynchronously run the buffer remove algorithm.
    HTML::queue_global_task(HTML::Task::Source::DOMManipulation, *this, GC::create_function(heap(), [this, start, end] {
        process_remove(start, end);
    }));

    return {};
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-buffered
GC::Ref<HTML::TimeRanges> SourceBuffer::buffered() const
{
    auto time_ranges = realm().create<HTML::TimeRanges>(realm());

    // Add each buffered range to the TimeRanges object
    for (auto const& range : m_buffered_ranges) {
        // Only add ranges with valid start/end times
        if (range.start < range.end && range.end > 0.0) {
            time_ranges->add_range(range.start, range.end);
        }
    }

    return time_ranges;
}

// Non-standard: Process appended data
void SourceBuffer::process_append()
{
    // Enhanced implementation: Pass data to media element's decoder if attached
    if (m_parent_media_source && m_parent_media_source->media_element()) {
        // For now, store the data in buffered ranges with approximate timestamps
        // FIXME: Integrate with media element's decoder pipeline
        BufferRange range;

        // Calculate approximate timestamps based on current buffered content
        // This is a simplified approach for HLS compatibility
        double last_end_time = 0.0;
        if (!m_buffered_ranges.is_empty()) {
            last_end_time = m_buffered_ranges.last().end;
        }

        // Estimate segment duration based on data size (rough approximation for HLS)
        // Typical HLS segments are 2-10 seconds, assume 6 seconds as a default
        double estimated_duration = 6.0;

        range.start = last_end_time;
        range.end = last_end_time + estimated_duration;
        if (auto copy_result = ByteBuffer::copy(m_pending_data.data(), m_pending_data.size()); !copy_result.is_error()) {
            range.data = copy_result.release_value();
        } else {
            dbgln("Failed to copy buffer data: {}", copy_result.error());
            // Continue with empty buffer to avoid breaking the flow
        }

        m_buffered_ranges.append(move(range));

        dbgln("SourceBuffer: Appended {} bytes of data (MIME type: {})", m_pending_data.size(), m_mime_type);

        // In a complete implementation, we would:
        // 1. Parse the media data to extract timing information
        // 2. Pass parsed data to the media element's decoder
        // 3. Update buffered ranges accurately
    }

    // Clear pending data
    m_pending_data.clear();

    // Set updating to false
    m_updating = false;

    // Fire update event
    dispatch_event(DOM::Event::create(realm(), EventNames::update));

    // Fire updateend event
    dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
}

// Non-standard: Process buffer removal
void SourceBuffer::process_remove(double start, double end)
{
    // Simplified implementation: Remove ranges that overlap with [start, end)
    size_t i = 0;
    while (i < m_buffered_ranges.size()) {
        auto& range = m_buffered_ranges[i];

        // Check if the range overlaps with the removal interval
        if (range.end <= start || range.start >= end) {
            // No overlap, keep the range
            ++i;
        } else {
            // Range overlaps, remove it for simplicity
            // FIXME: Implement proper range splitting and trimming
            m_buffered_ranges.remove(i);
        }
    }

    // Set updating to false
    m_updating = false;

    // Fire update event
    dispatch_event(DOM::Event::create(realm(), EventNames::update));

    // Fire updateend event
    dispatch_event(DOM::Event::create(realm(), EventNames::updateend));

    dbgln("SourceBuffer: Removed data from range [{}, {})", start, end);
}

// Internal: Set parent media source
void SourceBuffer::set_parent_media_source(MediaSource& source)
{
    m_parent_media_source = &source;
}

}

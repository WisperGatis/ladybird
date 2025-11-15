/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Forward.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-mediasource
class MediaSource : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaSource, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaSource);

public:
    [[nodiscard]] static WebIDL::ExceptionOr<GC::Ref<MediaSource>> construct_impl(JS::Realm&);

    // https://w3c.github.io/media-source/#dom-mediasource-canconstructindedicatedworker
    static bool can_construct_in_dedicated_worker(JS::VM&) { return true; }

    // https://w3c.github.io/media-source/#dom-mediasource-sourcebuffers
    GC::Ptr<SourceBufferList> source_buffers();

    // https://w3c.github.io/media-source/#dom-mediasource-activesourcebuffers
    GC::Ptr<SourceBufferList> active_source_buffers();

    // https://w3c.github.io/media-source/#dom-mediasource-readystate
    Bindings::ReadyState ready_state() const { return m_ready_state; }

    // https://w3c.github.io/media-source/#dom-mediasource-duration
    double duration() const { return m_duration; }
    WebIDL::ExceptionOr<void> set_duration(double);

    void set_onsourceopen(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceopen();

    void set_onsourceended(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceended();

    void set_onsourceclose(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceclose();

    // https://w3c.github.io/media-source/#dom-mediasource-addsourcebuffer
    WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> add_source_buffer(String const& type);

    // https://w3c.github.io/media-source/#dom-mediasource-removesourcebuffer
    WebIDL::ExceptionOr<void> remove_source_buffer(GC::Ref<SourceBuffer>);

    // https://w3c.github.io/media-source/#dom-mediasource-endofstream
    WebIDL::ExceptionOr<void> end_of_stream(Optional<Bindings::EndOfStreamError> error);

    static bool is_type_supported(JS::VM&, String const&);

    // Internal methods
    void attach_to_element(GC::Ref<HTML::HTMLMediaElement>);
    void detach_from_element();
    GC::Ptr<HTML::HTMLMediaElement> media_element() const { return m_media_element; }

protected:
    MediaSource(JS::Realm&);

    virtual ~MediaSource() override;

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

private:
    Bindings::ReadyState m_ready_state { Bindings::ReadyState::Closed };
    GC::Ptr<SourceBufferList> m_source_buffers;
    GC::Ptr<SourceBufferList> m_active_source_buffers;
    double m_duration { NAN };
    GC::Ptr<HTML::HTMLMediaElement> m_media_element;
};

}

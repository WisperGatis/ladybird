/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Forward.h>
#include <LibWeb/WebIDL/Buffers.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-sourcebuffer
class SourceBuffer : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBuffer, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBuffer);

public:
    static GC::Ref<SourceBuffer> create(JS::Realm&, String const& type);

    // https://w3c.github.io/media-source/#dom-sourcebuffer-updating
    bool updating() const { return m_updating; }

    // https://w3c.github.io/media-source/#dom-sourcebuffer-appendbuffer
    WebIDL::ExceptionOr<void> append_buffer(GC::Root<WebIDL::BufferSource> const& data);

    // https://w3c.github.io/media-source/#dom-sourcebuffer-remove
    WebIDL::ExceptionOr<void> remove(double start, double end);

    // https://w3c.github.io/media-source/#dom-sourcebuffer-buffered
    GC::Ref<HTML::TimeRanges> buffered() const;

    void set_onupdatestart(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdatestart();

    void set_onupdate(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdate();

    void set_onupdateend(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdateend();

    void set_onerror(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onerror();

    void set_onabort(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onabort();

    // Internal methods
    void set_parent_media_source(MediaSource&);
    GC::Ptr<MediaSource> parent_media_source() const { return m_parent_media_source; }

protected:
    SourceBuffer(JS::Realm&, String const& type);

    virtual ~SourceBuffer() override;

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

private:
    void process_append();
    void process_remove(double start, double end);

    bool m_updating { false };
    ByteBuffer m_pending_data;
    GC::Ptr<MediaSource> m_parent_media_source;
    String m_mime_type;

    // Basic buffer management for HLS segments
    struct BufferRange {
        double start;
        double end;
        ByteBuffer data;
    };
    Vector<BufferRange> m_buffered_ranges;
};

}

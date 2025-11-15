/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/Forward.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-sourcebufferlist
class SourceBufferList : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBufferList, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBufferList);

public:
    // https://w3c.github.io/media-source/#dom-sourcebufferlist-length
    size_t length() const { return m_buffers.size(); }

    // https://w3c.github.io/media-source/#dom-sourcebufferlist-item
    GC::Ptr<SourceBuffer> item(size_t index) const;
    GC::Ptr<SourceBuffer> operator[](size_t index) const { return item(index); }

    void set_onaddsourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onaddsourcebuffer();

    void set_onremovesourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onremovesourcebuffer();

    // Internal methods
    void add(GC::Ref<SourceBuffer>);
    void remove(GC::Ref<SourceBuffer>);
    bool contains(GC::Ref<SourceBuffer>) const;

private:
    SourceBufferList(JS::Realm&);

    virtual ~SourceBufferList() override;

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    Vector<GC::Ref<SourceBuffer>> m_buffers;
};

}

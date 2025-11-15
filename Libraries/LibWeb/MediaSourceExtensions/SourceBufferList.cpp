/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferListPrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/MediaSourceExtensions/SourceBufferList.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(SourceBufferList);

SourceBufferList::SourceBufferList(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

SourceBufferList::~SourceBufferList() = default;

void SourceBufferList::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SourceBufferList);
    Base::initialize(realm);
}

void SourceBufferList::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    for (auto& buffer : m_buffers)
        visitor.visit(buffer);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-item
GC::Ptr<SourceBuffer> SourceBufferList::item(size_t index) const
{
    if (index >= m_buffers.size())
        return nullptr;
    return m_buffers[index];
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onaddsourcebuffer
void SourceBufferList::set_onaddsourcebuffer(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::addsourcebuffer, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onaddsourcebuffer
GC::Ptr<WebIDL::CallbackType> SourceBufferList::onaddsourcebuffer()
{
    return event_handler_attribute(EventNames::addsourcebuffer);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onremovesourcebuffer
void SourceBufferList::set_onremovesourcebuffer(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::removesourcebuffer, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onremovesourcebuffer
GC::Ptr<WebIDL::CallbackType> SourceBufferList::onremovesourcebuffer()
{
    return event_handler_attribute(EventNames::removesourcebuffer);
}

// Internal: Add a SourceBuffer
void SourceBufferList::add(GC::Ref<SourceBuffer> buffer)
{
    m_buffers.append(buffer);
    dispatch_event(DOM::Event::create(realm(), EventNames::addsourcebuffer));
}

// Internal: Remove a SourceBuffer
void SourceBufferList::remove(GC::Ref<SourceBuffer> buffer)
{
    m_buffers.remove_first_matching([&](auto& item) {
        return item == buffer;
    });
    dispatch_event(DOM::Event::create(realm(), EventNames::removesourcebuffer));
}

// Internal: Check if contains a SourceBuffer
bool SourceBufferList::contains(GC::Ref<SourceBuffer> buffer) const
{
    for (auto& item : m_buffers) {
        if (item == buffer)
            return true;
    }
    return false;
}

}

/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibJS/Runtime/Object.h>

namespace Web::Extensions {
class Extension;
}

namespace Web::Extensions::API {

class ExtensionRuntime final : public JS::Object {
    JS_OBJECT(ExtensionRuntime, JS::Object);
    GC_DECLARE_ALLOCATOR(ExtensionRuntime);

public:
    static GC::Ref<ExtensionRuntime> create(JS::Realm& realm, Extension& extension);
    virtual ~ExtensionRuntime() override = default;

    virtual void initialize(JS::Realm& realm) override;

private:
    explicit ExtensionRuntime(JS::Realm& realm, Extension& extension);

    virtual void visit_edges(GC::Cell::Visitor& visitor) override;

    // Properties
    JS_DECLARE_NATIVE_FUNCTION(id_getter);
    JS_DECLARE_NATIVE_FUNCTION(last_error_getter);

    // Methods
    JS_DECLARE_NATIVE_FUNCTION(get_manifest);
    JS_DECLARE_NATIVE_FUNCTION(get_url);
    JS_DECLARE_NATIVE_FUNCTION(send_message);
    JS_DECLARE_NATIVE_FUNCTION(on_message);
    JS_DECLARE_NATIVE_FUNCTION(connect);
    JS_DECLARE_NATIVE_FUNCTION(reload);

    GC::Ptr<Extension> m_extension;
};

class ExtensionEvent {
public:
    ExtensionEvent() = default;
    ~ExtensionEvent() = default;
};

} 
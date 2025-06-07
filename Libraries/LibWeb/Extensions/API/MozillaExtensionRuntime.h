/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGC/Ptr.h>
#include <LibJS/Runtime/Object.h>
#include <LibWeb/Extensions/Extension.h>

namespace Web::Extensions::API {

class MozillaExtensionRuntime final : public JS::Object {
    JS_OBJECT(MozillaExtensionRuntime, JS::Object);
    GC_DECLARE_ALLOCATOR(MozillaExtensionRuntime);

public:
    static GC::Ref<MozillaExtensionRuntime> create(JS::Realm& realm, Extension& extension);
    virtual ~MozillaExtensionRuntime() override = default;

    virtual void initialize(JS::Realm& realm) override;

    // Mozilla WebExtension runtime API methods
    JS_DECLARE_NATIVE_FUNCTION(get_manifest);
    JS_DECLARE_NATIVE_FUNCTION(get_url);
    JS_DECLARE_NATIVE_FUNCTION(send_message);
    JS_DECLARE_NATIVE_FUNCTION(on_message); // Event listener setup
    JS_DECLARE_NATIVE_FUNCTION(connect);
    JS_DECLARE_NATIVE_FUNCTION(connect_native);
    JS_DECLARE_NATIVE_FUNCTION(send_native_message);
    JS_DECLARE_NATIVE_FUNCTION(reload);
    JS_DECLARE_NATIVE_FUNCTION(request_update_check);
    JS_DECLARE_NATIVE_FUNCTION(open_options_page);
    JS_DECLARE_NATIVE_FUNCTION(set_uninstall_url);
    JS_DECLARE_NATIVE_FUNCTION(get_platform_info);
    JS_DECLARE_NATIVE_FUNCTION(get_browser_info);

    // Properties
    String id() const { return m_extension->id(); }
    JS_DECLARE_NATIVE_FUNCTION(get_id);

private:
    explicit MozillaExtensionRuntime(JS::Realm& realm, Extension& extension);

    virtual void visit_edges(GC::Cell::Visitor&) override;

    NonnullRefPtr<Extension> m_extension;
};

} 
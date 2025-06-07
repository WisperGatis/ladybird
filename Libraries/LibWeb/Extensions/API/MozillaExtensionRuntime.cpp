/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MozillaExtensionRuntime.h"
#include <AK/StringBuilder.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/PropertyKey.h>
#include <LibURL/URL.h>
#include <LibWeb/Extensions/ExtensionManifest.h>

namespace Web::Extensions::API {

GC_DEFINE_ALLOCATOR(MozillaExtensionRuntime);

GC::Ref<MozillaExtensionRuntime> MozillaExtensionRuntime::create(JS::Realm& realm, Extension& extension)
{
    return realm.heap().allocate<MozillaExtensionRuntime>(realm, extension);
}

MozillaExtensionRuntime::MozillaExtensionRuntime(JS::Realm& realm, Extension& extension)
    : JS::Object(JS::Object::ConstructWithoutPrototypeTag {}, realm)
    , m_extension(extension)
{
}

void MozillaExtensionRuntime::initialize(JS::Realm& realm)
{
    Base::initialize(realm);

    u8 attr = JS::Attribute::Configurable | JS::Attribute::Writable;

    // Mozilla WebExtension runtime API methods
    define_native_function(realm, JS::PropertyKey { "getManifest"_string }, get_manifest, 0, attr);
    define_native_function(realm, JS::PropertyKey { "getURL"_string }, get_url, 1, attr);
    define_native_function(realm, JS::PropertyKey { "sendMessage"_string }, send_message, 1, attr);
    define_native_function(realm, JS::PropertyKey { "connect"_string }, connect, 0, attr);
    define_native_function(realm, JS::PropertyKey { "connectNative"_string }, connect_native, 1, attr);
    define_native_function(realm, JS::PropertyKey { "sendNativeMessage"_string }, send_native_message, 2, attr);
    define_native_function(realm, JS::PropertyKey { "reload"_string }, reload, 0, attr);
    define_native_function(realm, JS::PropertyKey { "requestUpdateCheck"_string }, request_update_check, 0, attr);
    define_native_function(realm, JS::PropertyKey { "openOptionsPage"_string }, open_options_page, 0, attr);
    define_native_function(realm, JS::PropertyKey { "setUninstallURL"_string }, set_uninstall_url, 1, attr);
    define_native_function(realm, JS::PropertyKey { "getPlatformInfo"_string }, get_platform_info, 0, attr);
    define_native_function(realm, JS::PropertyKey { "getBrowserInfo"_string }, get_browser_info, 0, attr);

    // Properties
    define_native_accessor(realm, JS::PropertyKey { "id"_string }, get_id, nullptr, attr);

    // Event objects (these would need proper implementation for real usage)
    auto on_message_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onMessage"_string, on_message_obj, attr);

    auto on_connect_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onConnect"_string, on_connect_obj, attr);

    auto on_startup_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onStartup"_string, on_startup_obj, attr);

    auto on_installed_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onInstalled"_string, on_installed_obj, attr);

    auto on_suspend_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onSuspend"_string, on_suspend_obj, attr);

    auto on_suspend_canceled_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onSuspendCanceled"_string, on_suspend_canceled_obj, attr);

    auto on_update_available_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    define_direct_property("onUpdateAvailable"_string, on_update_available_obj, attr);
}

void MozillaExtensionRuntime::visit_edges(GC::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    // m_extension is RefPtr, not GC managed
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::get_id)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<MozillaExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<MozillaExtensionRuntime>(*this_object);
    
    return JS::PrimitiveString::create(vm, runtime.m_extension->id());
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::get_manifest)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<MozillaExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<MozillaExtensionRuntime>(*this_object);
    auto const& manifest = runtime.m_extension->manifest();
    
    // Create a JavaScript object representing the manifest
    auto& realm = *vm.current_realm();
    auto manifest_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "manifest_version"_string }, { .value = JS::Value(static_cast<i32>(manifest.manifest_version())), .writable = true, .enumerable = true, .configurable = true }));
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "name"_string }, { .value = JS::PrimitiveString::create(vm, manifest.name()), .writable = true, .enumerable = true, .configurable = true }));
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "version"_string }, { .value = JS::PrimitiveString::create(vm, manifest.version()), .writable = true, .enumerable = true, .configurable = true }));
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "description"_string }, { .value = JS::PrimitiveString::create(vm, manifest.description()), .writable = true, .enumerable = true, .configurable = true }));
    
    // Add permissions array
    auto permissions_array = TRY(JS::Array::create(realm, 0));
    for (size_t i = 0; i < manifest.permissions().size(); ++i) {
        auto const& permission = manifest.permissions()[i];
        TRY(permissions_array->create_data_property(i, JS::PrimitiveString::create(vm, permission.value)));
    }
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "permissions"_string }, { .value = permissions_array, .writable = true, .enumerable = true, .configurable = true }));
    
    // Add Mozilla-specific fields
    if (manifest.platform() == ExtensionPlatform::Mozilla) {
        if (manifest.gecko_id().has_value()) {
            auto applications_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
            auto gecko_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
            MUST(gecko_obj->define_property_or_throw(JS::PropertyKey { "id"_string }, { .value = JS::PrimitiveString::create(vm, *manifest.gecko_id()), .writable = true, .enumerable = true, .configurable = true }));
            
            if (manifest.strict_min_version().has_value()) {
                MUST(gecko_obj->define_property_or_throw(JS::PropertyKey { "strict_min_version"_string }, { .value = JS::PrimitiveString::create(vm, *manifest.strict_min_version()), .writable = true, .enumerable = true, .configurable = true }));
            }
            
            if (manifest.strict_max_version().has_value()) {
                MUST(gecko_obj->define_property_or_throw(JS::PropertyKey { "strict_max_version"_string }, { .value = JS::PrimitiveString::create(vm, *manifest.strict_max_version()), .writable = true, .enumerable = true, .configurable = true }));
            }
            
            MUST(applications_obj->define_property_or_throw(JS::PropertyKey { "gecko"_string }, { .value = gecko_obj, .writable = true, .enumerable = true, .configurable = true }));
            MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "applications"_string }, { .value = applications_obj, .writable = true, .enumerable = true, .configurable = true }));
        }
    }
    
    return manifest_obj;
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::get_url)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<MozillaExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<MozillaExtensionRuntime>(*this_object);
    
    if (vm.argument_count() < 1) {
        return vm.throw_completion<JS::TypeError>("getURL requires at least 1 argument"sv);
    }

    auto path_arg = vm.argument(0);
    if (!path_arg.is_string()) {
        return vm.throw_completion<JS::TypeError>("Path must be a string"sv);
    }

    auto path = TRY(path_arg.to_string(vm));
    auto base_url = runtime.m_extension->base_url();
    
    // Resolve the path relative to the extension's base URL
    auto full_url = base_url.complete_url(path.to_byte_string());
    
    if (!full_url.has_value()) {
        return vm.throw_completion<JS::TypeError>("Failed to resolve URL"sv);
    }
    
    return JS::PrimitiveString::create(vm, full_url->to_byte_string());
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::send_message)
{
    // TODO: Implement message passing between extension components
    // For now, return a resolved promise
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::on_message)
{
    // TODO: Implement event listener registration
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::connect)
{
    // TODO: Implement long-lived connections
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::connect_native)
{
    // TODO: Implement native messaging
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::send_native_message)
{
    // TODO: Implement native messaging
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::reload)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<MozillaExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    
    // TODO: Implement extension reload
    dbgln("MozillaExtensionRuntime: reload() called - not yet implemented");
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::request_update_check)
{
    // TODO: Implement update checking
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::open_options_page)
{
    // TODO: Implement options page opening
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::set_uninstall_url)
{
    // TODO: Implement uninstall URL setting
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::get_platform_info)
{
    auto& realm = *vm.current_realm();
    auto platform_info = JS::Object::create(realm, realm.intrinsics().object_prototype());
    
    // Return platform information
    MUST(platform_info->define_property_or_throw(JS::PropertyKey { "os"_string }, { .value = JS::PrimitiveString::create(vm, "mac"_string), .writable = true, .enumerable = true, .configurable = true }));
    MUST(platform_info->define_property_or_throw(JS::PropertyKey { "arch"_string }, { .value = JS::PrimitiveString::create(vm, "x86-64"_string), .writable = true, .enumerable = true, .configurable = true }));
    
    return platform_info;
}

JS_DEFINE_NATIVE_FUNCTION(MozillaExtensionRuntime::get_browser_info)
{
    auto& realm = *vm.current_realm();
    auto browser_info = JS::Object::create(realm, realm.intrinsics().object_prototype());
    
    // Return browser information
    MUST(browser_info->define_property_or_throw(JS::PropertyKey { "name"_string }, { .value = JS::PrimitiveString::create(vm, "Ladybird"_string), .writable = true, .enumerable = true, .configurable = true }));
    MUST(browser_info->define_property_or_throw(JS::PropertyKey { "vendor"_string }, { .value = JS::PrimitiveString::create(vm, "Ladybird Project"_string), .writable = true, .enumerable = true, .configurable = true }));
    MUST(browser_info->define_property_or_throw(JS::PropertyKey { "version"_string }, { .value = JS::PrimitiveString::create(vm, "1.0.0"_string), .writable = true, .enumerable = true, .configurable = true }));
    MUST(browser_info->define_property_or_throw(JS::PropertyKey { "buildID"_string }, { .value = JS::PrimitiveString::create(vm, "20250101"_string), .writable = true, .enumerable = true, .configurable = true }));
    
    return browser_info;
}

} 
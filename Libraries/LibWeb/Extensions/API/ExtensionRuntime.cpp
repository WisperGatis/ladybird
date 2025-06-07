/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ExtensionRuntime.h"
#include <AK/StringBuilder.h>
#include <AK/TypeCasts.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PropertyKey.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Extensions/Extension.h>
#include <LibWeb/Extensions/ExtensionManager.h>

namespace Web::Extensions::API {

GC_DEFINE_ALLOCATOR(ExtensionRuntime);

GC::Ref<ExtensionRuntime> ExtensionRuntime::create(JS::Realm& realm, Extension& extension)
{
    return realm.heap().allocate<ExtensionRuntime>(realm, extension);
}

ExtensionRuntime::ExtensionRuntime(JS::Realm& realm, Extension& extension)
    : JS::Object(JS::Object::ConstructWithoutPrototypeTag {}, realm)
    , m_extension(extension)
{
}

void ExtensionRuntime::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    
    u8 attr = JS::Attribute::Configurable | JS::Attribute::Writable;
    
    // Properties
    define_native_accessor(realm, JS::PropertyKey { "id"_string }, id_getter, nullptr, attr);
    define_native_accessor(realm, JS::PropertyKey { "lastError"_string }, last_error_getter, nullptr, attr);
    
    // Methods
    define_native_function(realm, JS::PropertyKey { "getManifest"_string }, get_manifest, 0, attr);
    define_native_function(realm, JS::PropertyKey { "getURL"_string }, get_url, 1, attr);
    define_native_function(realm, JS::PropertyKey { "sendMessage"_string }, send_message, 1, attr);
    define_native_function(realm, JS::PropertyKey { "onMessage"_string }, on_message, 1, attr);
    define_native_function(realm, JS::PropertyKey { "connect"_string }, connect, 1, attr);
    define_native_function(realm, JS::PropertyKey { "reload"_string }, reload, 0, attr);
}

void ExtensionRuntime::visit_edges(GC::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    // Extension is RefCounted, not GC::Cell, so we don't need to visit it
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::id_getter)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<ExtensionRuntime>(*this_object);
    return JS::PrimitiveString::create(vm, runtime.m_extension->id());
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::last_error_getter)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<ExtensionRuntime>(*this_object);
    auto const& error = runtime.m_extension->last_error();
    
    if (error.is_empty()) {
        return JS::js_null();
    }
    
    auto& realm = *vm.current_realm();
    auto error_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    MUST(error_obj->define_property_or_throw(JS::PropertyKey { "message"_string }, { .value = JS::PrimitiveString::create(vm, error), .writable = true, .enumerable = true, .configurable = true }));
    return error_obj;
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::get_manifest)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<ExtensionRuntime>(*this_object);
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
    
    // Add host permissions (for MV3)
    auto host_permissions_array = TRY(JS::Array::create(realm, 0));
    for (size_t i = 0; i < manifest.host_permissions().size(); ++i) {
        auto const& host_permission = manifest.host_permissions()[i];
        TRY(host_permissions_array->create_data_property(i, JS::PrimitiveString::create(vm, host_permission)));
    }
    MUST(manifest_obj->define_property_or_throw(JS::PropertyKey { "host_permissions"_string }, { .value = host_permissions_array, .writable = true, .enumerable = true, .configurable = true }));
    
    return manifest_obj;
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::get_url)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& runtime = as<ExtensionRuntime>(*this_object);
    
    if (vm.argument_count() < 1) {
        return vm.throw_completion<JS::TypeError>("getURL requires at least 1 argument"sv);
    }
    
    auto resource_path = TRY(vm.argument(0).to_string(vm));
    
    // Construct the full extension URL
    auto base_url = runtime.m_extension->base_url();
    StringBuilder url_builder;
    url_builder.append(base_url.to_byte_string());
    url_builder.append(resource_path.to_byte_string());
    auto full_url = url_builder.to_string();
    if (full_url.is_error()) {
        return vm.throw_completion<JS::TypeError>("Failed to construct URL"sv);
    }
    
    return JS::PrimitiveString::create(vm, full_url.release_value());
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::send_message)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    // Note: runtime is intentionally unused for now as the function is a TODO
    (void)as<ExtensionRuntime>(*this_object);
    
    if (vm.argument_count() < 1) {
        return vm.throw_completion<JS::TypeError>("sendMessage requires at least 1 argument"sv);
    }
    
    // Note: message is intentionally unused for now as the function is a TODO
    (void)vm.argument(0);
    
    // TODO: Implement message passing between extension contexts
    // This would involve:
    // 1. Serializing the message
    // 2. Routing it to the appropriate context (background, content script, popup, etc.)
    // 3. Handling responses
    
    // For now, just return undefined
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::on_message)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    // Note: runtime is intentionally unused for now as the function is a TODO
    (void)as<ExtensionRuntime>(*this_object);
    
    if (vm.argument_count() < 1) {
        return vm.throw_completion<JS::TypeError>("onMessage requires at least 1 argument"sv);
    }
    
    auto listener = vm.argument(0);
    if (!listener.is_function()) {
        return vm.throw_completion<JS::TypeError>("onMessage listener must be a function"sv);
    }
    
    // TODO: Store the listener for when messages are received
    // This would involve adding the listener to a list and calling it when messages arrive
    
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::connect)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    // Note: runtime is intentionally unused for now as the function is a TODO
    (void)as<ExtensionRuntime>(*this_object);
    
    // TODO: Implement runtime.connect for creating long-lived connections
    // This would return a Port object for bidirectional communication
    
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ExtensionRuntime::reload)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ExtensionRuntime>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    // Note: runtime is intentionally unused for now as the function is a TODO
    (void)as<ExtensionRuntime>(*this_object);
    
    // TODO: Implement extension reload
    // This would reload the extension's scripts and resources
    
    return JS::js_undefined();
}

} 
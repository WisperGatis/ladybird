/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ChromeWebStore.h"
#include <AK/StringBuilder.h>
#include <LibGC/Allocator.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/PropertyKey.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#include <LibWeb/Extensions/ExtensionManager.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/HTML/Window.h>

namespace Web::Extensions::API {

GC_DEFINE_ALLOCATOR(ChromeWebStore);

GC::Ref<ChromeWebStore> ChromeWebStore::create(JS::Realm& realm)
{
    return realm.heap().allocate<ChromeWebStore>(realm, realm);
}

ChromeWebStore::ChromeWebStore(JS::Realm& realm)
    : JS::Object(JS::Object::ConstructWithoutPrototypeTag {}, realm)
{
}

void ChromeWebStore::initialize(JS::Realm& realm)
{
    Base::initialize(realm);

    u8 attr = JS::Attribute::Configurable | JS::Attribute::Writable;

    // Web Store API methods
    define_native_function(realm, JS::PropertyKey { "install"_string }, install, 1, attr);
    define_native_function(realm, JS::PropertyKey { "enable"_string }, enable, 1, attr);
    define_native_function(realm, JS::PropertyKey { "disable"_string }, disable, 1, attr);
}

void ChromeWebStore::visit_edges(GC::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
}

JS_DEFINE_NATIVE_FUNCTION(ChromeWebStore::install)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<ChromeWebStore>(*this_object))
        return vm.throw_completion<JS::TypeError>("Invalid this value"sv);
    auto& web_store = as<ChromeWebStore>(*this_object);

    // Chrome Web Store install API signature:
    // chrome.webstore.install(url, successCallback, failureCallback)
    if (vm.argument_count() < 1) {
        return vm.throw_completion<JS::TypeError>("install requires at least 1 argument"sv);
    }

    auto url_arg = vm.argument(0);
    if (!url_arg.is_string()) {
        return vm.throw_completion<JS::TypeError>("install URL must be a string"sv);
    }

    auto web_store_url = TRY(url_arg.to_string(vm));
    
    // Extract extension ID from Chrome Web Store URL
    // Format: https://chrome.google.com/webstore/detail/extension-name/EXTENSION_ID
    auto extension_id = extract_extension_id_from_webstore_url(web_store_url.to_byte_string());
    if (!extension_id.has_value()) {
        return vm.throw_completion<JS::TypeError>("Invalid Chrome Web Store URL"sv);
    }

    // Get callbacks if provided
    JS::Value success_callback = JS::js_undefined();
    JS::Value failure_callback = JS::js_undefined();
    
    if (vm.argument_count() >= 2 && vm.argument(1).is_function()) {
        success_callback = vm.argument(1);
    }
    if (vm.argument_count() >= 3 && vm.argument(2).is_function()) {
        failure_callback = vm.argument(2);
    }

    // Start the installation process
    // Note: In a real implementation, this would be asynchronous
    auto install_result = web_store.download_and_install_extension(*extension_id, web_store_url.to_byte_string());
    
    if (install_result.is_error()) {
        // Call failure callback if provided
        if (failure_callback.is_function()) {
            auto error_message = JS::PrimitiveString::create(vm, install_result.error().string_literal());
            TRY(JS::call(vm, failure_callback.as_function(), JS::js_undefined(), error_message));
        }
        
        // Also trigger internal failure callbacks
        for (auto const& callback : web_store.m_install_failure_callbacks) {
            callback(install_result.error().string_literal());
        }
        
        return vm.throw_completion<JS::Error>("Extension installation failed"sv);
    }

    // Call success callback if provided
    if (success_callback.is_function()) {
        TRY(JS::call(vm, success_callback.as_function(), JS::js_undefined()));
    }
    
    // Trigger internal success callbacks
    for (auto const& callback : web_store.m_install_success_callbacks) {
        callback(*extension_id);
    }

    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ChromeWebStore::enable)
{
    // This would enable an installed extension
    // For now, just return success
    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ChromeWebStore::disable)
{
    // This would disable an installed extension
    // For now, just return success
    return JS::js_undefined();
}

void ChromeWebStore::on_install_success(Function<void(String const& extension_id)> callback)
{
    m_install_success_callbacks.append(move(callback));
}

void ChromeWebStore::on_install_failure(Function<void(String const& error)> callback)
{
    m_install_failure_callbacks.append(move(callback));
}

ErrorOr<void> ChromeWebStore::download_and_install_extension(String const& extension_id, String const& web_store_url)
{
    // For now, create a mock installation to demonstrate the flow
    // In a real implementation, this would:
    // 1. Download the CRX file from Chrome Web Store
    // 2. Extract and validate the extension
    // 3. Install it into the extension system
    
    dbgln("ChromeWebStore: Installing extension {} from {}", extension_id, web_store_url);
    
    // Create a mock extension directory for demonstration
    auto temp_dir = TRY(String::formatted("/tmp/chrome-extension-{}", extension_id));
    
    // Create mock manifest.json
    auto manifest_content = TRY(String::formatted(R"({{
  "manifest_version": 3,
  "name": "Extension {}",
  "version": "1.0.0",
  "description": "Extension installed from Chrome Web Store",
  "permissions": ["activeTab"],
  "content_scripts": [
    {{
      "matches": ["*://*/*"],
      "js": ["content.js"],
      "run_at": "document_idle"
    }}
  ],
  "background": {{
    "service_worker": "background.js"
  }},
  "action": {{
    "default_title": "Chrome Store Extension"
  }}
}})", extension_id));

    // For now, just return success
    // TODO: Implement actual CRX download and installation
    return {};
}

ErrorOr<Vector<u8>> ChromeWebStore::download_crx_file(URL::URL const& download_url)
{
    // TODO: Implement actual CRX file download
    // This would use LibRequests to download the .crx file from Chrome Web Store
    (void)download_url;
    return Error::from_string_literal("CRX download not yet implemented");
}

ErrorOr<String> ChromeWebStore::extract_and_install_crx(Vector<u8> const& crx_data, String const& extension_id)
{
    // TODO: Implement CRX extraction and installation
    // CRX format:
    // - Header with magic number "Cr24"
    // - Version number
    // - Public key length
    // - Signature length
    // - Public key
    // - Signature
    // - ZIP archive containing extension files
    (void)crx_data;
    (void)extension_id;
    return Error::from_string_literal("CRX extraction not yet implemented");
}

Optional<String> ChromeWebStore::extract_extension_id_from_webstore_url(StringView url)
{
    // Chrome Web Store URLs typically look like:
    // https://chrome.google.com/webstore/detail/extension-name/EXTENSION_ID
    // https://chromewebstore.google.com/detail/extension-name/EXTENSION_ID
    
    if (!url.contains("webstore"sv) || !url.contains("detail"sv)) {
        return {};
    }
    
    // Find the last segment after "/detail/"
    auto detail_pos = url.find("detail/"sv);
    if (!detail_pos.has_value()) {
        return {};
    }
    
    auto after_detail = url.substring_view(detail_pos.value() + 7); // Skip "detail/"
    auto slash_pos = after_detail.find('/');
    
    String extension_id;
    if (slash_pos.has_value()) {
        // Extension ID is between detail/ and the next slash
        extension_id = String::from_utf8(after_detail.substring_view(0, slash_pos.value())).release_value_but_fixme_should_propagate_errors();
    } else {
        // Extension ID is at the end of URL
        extension_id = String::from_utf8(after_detail).release_value_but_fixme_should_propagate_errors();
    }
    
    // Chrome extension IDs are 32 characters long and contain only a-p
    if (extension_id.byte_count() == 32) {
        bool valid_chars = true;
        for (auto byte : extension_id.bytes()) {
            if (byte < 'a' || byte > 'p') {
                valid_chars = false;
                break;
            }
        }
        if (valid_chars) {
            return extension_id;
        }
    }
    
    return {};
}

} 
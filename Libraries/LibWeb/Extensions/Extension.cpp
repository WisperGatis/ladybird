/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Extension.h"
#include <AK/JsonParser.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Realm.h>
#include <LibURL/URL.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Extensions/API/ExtensionRuntime.h>
#include <LibWeb/Extensions/API/MozillaExtensionRuntime.h>
#include <LibWeb/HTML/HTMLHeadElement.h>
#include <LibWeb/HTML/HTMLScriptElement.h>
#include <LibWeb/HTML/HTMLStyleElement.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/ClassicScript.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/Namespace.h>

namespace Web::Extensions {

ErrorOr<NonnullRefPtr<Extension>> Extension::create_from_directory(String const& extension_path)
{
    // Load manifest.json
    auto manifest_path = LexicalPath::join(extension_path, "manifest.json"sv).string();
    if (!FileSystem::exists(manifest_path)) {
        return Error::from_string_literal("manifest.json not found");
    }
    
    auto manifest_file = TRY(Core::File::open(manifest_path, Core::File::OpenMode::Read));
    auto manifest_content = TRY(manifest_file->read_until_eof());
    
    auto manifest_json_result = JsonParser::parse(manifest_content);
    if (manifest_json_result.is_error()) {
        return Error::from_string_literal("Failed to parse manifest.json");
    }
    
    auto manifest_json = manifest_json_result.release_value();
    if (!manifest_json.is_object()) {
        return Error::from_string_literal("manifest.json must be an object");
    }
    
    auto manifest = TRY(ExtensionManifest::parse_from_json(manifest_json.as_object()));
    if (!manifest.is_valid()) {
        return Error::from_string_literal("Invalid manifest");
    }
    
    return create_from_manifest(move(manifest), extension_path);
}

ErrorOr<NonnullRefPtr<Extension>> Extension::create_from_manifest(ExtensionManifest manifest, String const& base_path)
{
    auto extension = adopt_ref(*new Extension(move(manifest), base_path));
    TRY(extension->load_and_validate());
    return extension;
}

Extension::Extension(ExtensionManifest manifest, String base_path)
    : m_manifest(move(manifest))
    , m_base_path(move(base_path))
{
}

ErrorOr<void> Extension::load_and_validate()
{
    // Validate that required files exist
    for (auto const& content_script : m_manifest.content_scripts()) {
        for (auto const& js_file : content_script.js) {
            auto file_path = TRY(resolve_file_path(js_file));
            if (!FileSystem::exists(file_path)) {
                return Error::from_string_literal("Content script JS file not found");
            }
        }
        for (auto const& css_file : content_script.css) {
            auto file_path = TRY(resolve_file_path(css_file));
            if (!FileSystem::exists(file_path)) {
                return Error::from_string_literal("Content script CSS file not found");
            }
        }
    }
    
    // Validate background scripts
    if (m_manifest.background().has_value()) {
        auto const& background = m_manifest.background().value();
        for (auto const& script_file : background.scripts) {
            auto file_path = TRY(resolve_file_path(script_file));
            if (!FileSystem::exists(file_path)) {
                return Error::from_string_literal("Background script file not found");
            }
        }
        
        if (!background.service_worker.is_empty()) {
            auto file_path = TRY(resolve_file_path(background.service_worker));
            if (!FileSystem::exists(file_path)) {
                return Error::from_string_literal("Service worker file not found");
            }
        }
    }
    
    m_state = ExtensionState::Disabled;
    return {};
}

ErrorOr<String> Extension::resolve_file_path(String const& relative_path) const
{
    auto path_bytes = LexicalPath::join(m_base_path, relative_path).string();
    return TRY(String::from_utf8(path_bytes.view()));
}

ErrorOr<void> Extension::initialize_background_script(HTML::EnvironmentSettingsObject&)
{
    if (!m_manifest.background().has_value()) {
        return {};
    }
    
    auto const& background = m_manifest.background().value();
    BackgroundScriptInfo script_info;
    script_info.script_id = generate_script_id(ScriptContext::Background);
    script_info.script_files = background.scripts;
    script_info.service_worker_file = background.service_worker;
    script_info.is_persistent = background.persistent;
    script_info.is_service_worker = !background.service_worker.is_empty();
    
    // For now, we'll create a realm for the background script
    // In a full implementation, this would be more sophisticated
    auto& vm = Bindings::main_thread_vm();
    auto execution_context = Bindings::create_a_new_javascript_realm(vm, nullptr, nullptr);
    script_info.realm = execution_context->realm;
    
    // Inject extension APIs into the background script context
    TRY(inject_extension_apis(*script_info.realm, ScriptContext::Background));
    
    // Load and execute background scripts
    if (!script_info.is_service_worker) {
        for (auto const& script_file : script_info.script_files) {
            auto script_content = TRY(load_script_file(script_file));
            // TODO: Execute the script in the background context
            // This would involve creating a proper execution environment
        }
    } else {
        // Load service worker
        auto sw_content = TRY(load_script_file(script_info.service_worker_file));
        // TODO: Execute service worker
    }
    
    m_background_script = move(script_info);
    return {};
}

ErrorOr<void> Extension::inject_content_scripts(DOM::Document& document)
{
    auto const& url = document.url();
    
    for (auto const& content_script : m_manifest.content_scripts()) {
        if (!should_inject_content_script(document, content_script)) {
            continue;
        }
        
        ContentScriptInfo script_info;
        auto url_byte_string = url.to_byte_string();
        auto url_string = TRY(String::from_utf8(url_byte_string.view()));
        script_info.script_id = generate_script_id(ScriptContext::ContentScript, url_string);
        script_info.matched_patterns = content_script.matches;
        script_info.js_files = content_script.js;
        script_info.css_files = content_script.css;
        script_info.run_at = content_script.run_at;
        script_info.all_frames = content_script.all_frames;
        script_info.target_document = &document;
        
        // Inject CSS files first
        TRY(inject_css_files(script_info, document));
        
        // Then execute JS files based on run_at timing
        if (script_info.run_at == "document_start"sv) {
            TRY(execute_content_script_files(script_info, document));
        } else {
            // For document_idle and document_end, we'll need to defer execution
            // For now, execute immediately
            TRY(execute_content_script_files(script_info, document));
        }
        
        m_content_scripts.append(move(script_info));
    }
    
    return {};
}

bool Extension::should_inject_content_script(DOM::Document const& document, ContentScript const& script) const
{
    auto const& url = document.url();
    
    // Check if URL matches any of the patterns
    bool matches = false;
    for (auto const& pattern : script.matches) {
        if (match_pattern(pattern, url)) {
            matches = true;
            break;
        }
    }
    
    if (!matches) {
        return false;
    }
    
    // Check exclude patterns
    for (auto const& exclude_pattern : script.exclude_matches) {
        if (match_pattern(exclude_pattern, url)) {
            return false;
        }
    }
    
    // Check frame conditions
    if (!script.all_frames && document.browsing_context() && !document.browsing_context()->is_top_level()) {
        return false; // Don't inject in frames unless all_frames is true
    }
    
    return true;
}

ErrorOr<void> Extension::execute_content_script_files(ContentScriptInfo& script_info, DOM::Document& document)
{
    // Create an isolated execution context for content scripts
    auto& vm = document.vm();
    auto execution_context = Bindings::create_a_new_javascript_realm(vm, nullptr, nullptr);
    script_info.realm = execution_context->realm;
    
    // Inject extension APIs into the script context
    TRY(inject_extension_apis(*script_info.realm, ScriptContext::ContentScript));
    
    for (auto const& js_file : script_info.js_files) {
        auto script_content = TRY(load_script_file(js_file));
        
        // Create and execute the script
        auto script_filename = TRY(resolve_file_path(js_file));
        auto script_url = URL::create_with_url_or_path(script_filename.to_byte_string()).value_or(URL::URL());
        auto classic_script = HTML::ClassicScript::create(script_filename.to_byte_string(), script_content, *script_info.realm, script_url);
        
        // Execute in the document's context but with extension privileges
        // TODO: Execute the script properly
        (void)classic_script;
    }
    
    return {};
}

ErrorOr<void> Extension::inject_css_files(ContentScriptInfo const& script_info, DOM::Document& document)
{
    for (auto const& css_file : script_info.css_files) {
        auto css_content = TRY(load_css_file(css_file));
        
        // TODO: Create a style element and inject the CSS
        // This needs proper WebIDL error handling which we'll implement later
        (void)css_content;
        (void)document;
    }
    
    return {};
}

ErrorOr<String> Extension::load_script_file(String const& relative_path) const
{
    auto file_path = TRY(resolve_file_path(relative_path));
    auto file = TRY(Core::File::open(file_path, Core::File::OpenMode::Read));
    auto content = TRY(file->read_until_eof());
    return TRY(String::from_utf8(content));
}

ErrorOr<String> Extension::load_css_file(String const& relative_path) const
{
    auto file_path = TRY(resolve_file_path(relative_path));
    auto file = TRY(Core::File::open(file_path, Core::File::OpenMode::Read));
    auto content = TRY(file->read_until_eof());
    return TRY(String::from_utf8(content));
}

ErrorOr<Vector<u8>> Extension::load_resource_file(String const& relative_path) const
{
    auto file_path = TRY(resolve_file_path(relative_path));
    auto file = TRY(Core::File::open(file_path, Core::File::OpenMode::Read));
    auto buffer = TRY(file->read_until_eof());
    Vector<u8> result;
    result.ensure_capacity(buffer.size());
    for (auto byte : buffer.bytes()) {
        result.unchecked_append(byte);
    }
    return result;
}

bool Extension::is_resource_web_accessible(String const& resource_path, URL::URL const& requesting_origin) const
{
    for (auto const& war : m_manifest.web_accessible_resources()) {
        // Check if the resource matches any of the accessible resources
        for (auto const& resource_pattern : war.resources) {
            if (match_glob(resource_pattern, resource_path)) {
                // Check if the requesting origin is allowed
                for (auto const& match_pattern : war.matches) {
                    if (Extension::match_pattern(match_pattern, requesting_origin)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool Extension::has_permission(String const& permission) const
{
    for (auto const& perm : m_manifest.permissions()) {
        if (perm.value == permission) {
            return true;
        }
    }
    return false;
}

bool Extension::has_host_permission(URL::URL const& url) const
{
    // Check explicit host permissions
    for (auto const& host_pattern : m_manifest.host_permissions()) {
        if (match_pattern(host_pattern, url)) {
            return true;
        }
    }
    
    // Check host permissions in regular permissions (MV2 style)
    for (auto const& perm : m_manifest.permissions()) {
        if (perm.type == ExtensionPermission::Type::Host) {
            if (match_pattern(perm.value, url)) {
                return true;
            }
        }
    }
    
    return false;
}

void Extension::notify_document_loaded(DOM::Document& document)
{
    // Inject content scripts that should run at document_idle
    for (auto& script_info : m_content_scripts) {
        if (script_info.target_document == &document && script_info.run_at == "document_idle"sv) {
            // Re-execute if needed
        }
    }
}

void Extension::notify_document_unloaded(DOM::Document& document)
{
    // Clean up content scripts for this document
    m_content_scripts.remove_all_matching([&document](auto const& script_info) {
        return script_info.target_document == &document;
    });
}

void Extension::notify_navigation_committed(DOM::Document& document, URL::URL const& url)
{
    (void)url; // Mark as intentionally unused
    
    // Clean up old content scripts and inject new ones if needed
    notify_document_unloaded(document);
    
    // The document will call inject_content_scripts when appropriate
}

String Extension::generate_script_id(ScriptContext context, String const& additional_info) const
{
    StringBuilder builder;
    builder.append(m_manifest.id());
    builder.append("-"sv);
    
    switch (context) {
    case ScriptContext::Background:
        builder.append("background"sv);
        break;
    case ScriptContext::ContentScript:
        builder.append("content"sv);
        break;
    case ScriptContext::Popup:
        builder.append("popup"sv);
        break;
    case ScriptContext::Options:
        builder.append("options"sv);
        break;
    case ScriptContext::DevTools:
        builder.append("devtools"sv);
        break;
    }
    
    if (!additional_info.is_empty()) {
        builder.append("-"sv);
        builder.append(additional_info);
    }
    
    // Add timestamp for uniqueness
    builder.append("-"sv);
    builder.append(String::number(time(nullptr)));
    
    return builder.to_string().release_value_but_fixme_should_propagate_errors();
}

bool Extension::match_pattern(StringView pattern, URL::URL const& url)
{
    // Simple pattern matching implementation
    // This should be expanded to fully support Chrome extension match patterns
    
    if (pattern == "<all_urls>"sv) {
        return true;
    }
    
    // Parse the pattern
    auto pattern_url_result = URLPattern::parse(pattern);
    if (pattern_url_result.is_error()) {
        return false;
    }
    
    auto pattern_url = pattern_url_result.release_value();
    return pattern_url.matches(url);
}

bool Extension::match_glob(StringView glob_pattern, StringView string)
{
    // Simple glob matching - should be expanded for full glob support
    if (glob_pattern == "*"sv) {
        return true;
    }
    
    if (glob_pattern.ends_with("*"sv)) {
        auto prefix = glob_pattern.substring_view(0, glob_pattern.length() - 1);
        return string.starts_with(prefix);
    }
    
    if (glob_pattern.starts_with("*"sv)) {
        auto suffix = glob_pattern.substring_view(1);
        return string.ends_with(suffix);
    }
    
    return glob_pattern == string;
}

// URLPattern implementation
ErrorOr<URLPattern> URLPattern::parse(StringView pattern)
{
    // Basic URL pattern parsing
    // Format: <scheme>://<host><path>
    // Where scheme, host, and path can contain wildcards
    
    auto scheme_separator = pattern.find("://"sv);
    if (!scheme_separator.has_value()) {
        return Error::from_string_literal("Invalid URL pattern: missing ://");
    }
    
    auto scheme = pattern.substring_view(0, *scheme_separator);
    auto rest = pattern.substring_view(*scheme_separator + 3);
    
    auto path_separator = rest.find("/"sv);
    String host;
    String path;
    
    if (path_separator.has_value()) {
        host = MUST(String::from_utf8(rest.substring_view(0, *path_separator)));
        path = MUST(String::from_utf8(rest.substring_view(*path_separator)));
    } else {
        host = MUST(String::from_utf8(rest));
        path = "/*"_string;
    }
    
    return URLPattern(MUST(String::from_utf8(scheme)), move(host), move(path));
}

URLPattern::URLPattern(String scheme, String host, String path)
    : m_scheme(move(scheme))
    , m_host(move(host))
    , m_path(move(path))
{
    m_match_subdomains = m_host.bytes_as_string_view().starts_with("*."sv);
}

bool URLPattern::matches(URL::URL const& url) const
{
    // Check scheme
    if (m_scheme != "*" && m_scheme != url.scheme()) {
        return false;
    }
    
    // Check host
    if (m_host != "*") {
        if (m_match_subdomains) {
            auto base_host = m_host.substring_from_byte_offset(2).release_value_but_fixme_should_propagate_errors();
            auto host_opt = url.host();
            auto formatted_base = MUST(String::formatted(".{}", base_host));
            auto host_serialized = host_opt->serialize();
            if (!host_opt.has_value() || (host_serialized != base_host && !host_serialized.bytes_as_string_view().ends_with(formatted_base.bytes_as_string_view()))) {
                return false;
            }
        } else {
            auto host_opt = url.host();
            if (!host_opt.has_value() || m_host != host_opt->serialize()) {
                return false;
            }
        }
    }
    
    // Check path
    if (m_path != "/*") {
        if (m_path.bytes_as_string_view().ends_with("*"sv)) {
            auto prefix = m_path.bytes_as_string_view().substring_view(0, m_path.bytes_as_string_view().length() - 1);
            auto path_serialized = url.serialize_path();
            if (!path_serialized.bytes_as_string_view().starts_with(prefix)) {
                return false;
            }
        } else {
            if (m_path != url.serialize_path()) {
                return false;
            }
        }
    }
    
    return true;
}

ErrorOr<void> Extension::inject_extension_apis(JS::Realm& realm, ScriptContext context)
{
    (void)context; // TODO: Use context to determine which APIs to inject
    
    // Create the appropriate API namespace based on extension platform
    if (m_manifest.platform() == ExtensionPlatform::Mozilla) {
        // Create browser.runtime API for Mozilla WebExtensions
        auto mozilla_runtime = API::MozillaExtensionRuntime::create(realm, *this);
        
        // Create the browser object if it doesn't exist
        auto& global_object = realm.global_object();
        auto browser_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
        browser_obj->define_direct_property("runtime"_string, mozilla_runtime, JS::default_attributes);
        
        // Inject into the global scope
        global_object.define_direct_property("browser"_string, browser_obj, JS::default_attributes);
        
        dbgln("Extension: Injected Mozilla WebExtension APIs (browser.runtime) for extension {}", id());
    } else {
        // Create chrome.runtime API for Chrome extensions
        auto chrome_runtime = API::ExtensionRuntime::create(realm, *this);
        
        // Create the chrome object if it doesn't exist
        auto& global_object = realm.global_object();
        auto chrome_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
        chrome_obj->define_direct_property("runtime"_string, chrome_runtime, JS::default_attributes);
        
        // Inject into the global scope
        global_object.define_direct_property("chrome"_string, chrome_obj, JS::default_attributes);
        
        dbgln("Extension: Injected Chrome Extension APIs (chrome.runtime) for extension {}", id());
    }
    
    // TODO: Add more APIs as needed (tabs, storage, webRequest, etc.)
    // For now, we just inject the runtime API which is the most fundamental
    
    return {};
}

} 
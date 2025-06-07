/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "ExtensionManifest.h"
#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibFileSystem/FileSystem.h>
#include <LibGC/Ptr.h>
#include <LibJS/Runtime/Realm.h>
#include <LibURL/URL.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/Environments.h>

namespace Web::Extensions {

enum class ExtensionState {
    Disabled,
    Enabled,
    Installing,
    Uninstalling,
    Error
};

enum class ScriptContext {
    Background,
    ContentScript,
    Popup,
    Options,
    DevTools
};

struct ContentScriptInfo {
    String script_id;
    Vector<String> matched_patterns;
    Vector<String> js_files;
    Vector<String> css_files;
    String run_at;
    bool all_frames;
    DOM::Document* target_document;
    GC::Ptr<JS::Realm> realm;
};

struct BackgroundScriptInfo {
    String script_id;
    Vector<String> script_files;
    String service_worker_file;
    GC::Ptr<JS::Realm> realm;
    bool is_persistent;
    bool is_service_worker;
};

class Extension final : public RefCounted<Extension> {
public:
    static ErrorOr<NonnullRefPtr<Extension>> create_from_directory(String const& extension_path);
    static ErrorOr<NonnullRefPtr<Extension>> create_from_manifest(ExtensionManifest manifest, String const& base_path);
    
    ~Extension() = default;

    // Basic properties
    String const& id() const { return m_manifest.id(); }
    String const& name() const { return m_manifest.name(); }
    String const& version() const { return m_manifest.version(); }
    String const& description() const { return m_manifest.description(); }
    ExtensionManifest const& manifest() const { return m_manifest; }
    String const& base_path() const { return m_base_path; }
    URL::URL const& base_url() const { return m_manifest.base_url(); }

    // State management
    ExtensionState state() const { return m_state; }
    void set_state(ExtensionState state) { m_state = state; }
    bool is_enabled() const { return m_state == ExtensionState::Enabled; }

    // Manifest access for configuration
    ExtensionManifest& mutable_manifest() { return m_manifest; }

    // Script contexts
    ErrorOr<void> initialize_background_script(HTML::EnvironmentSettingsObject& settings);
    ErrorOr<void> inject_content_scripts(DOM::Document& document);
    bool should_inject_content_script(DOM::Document const& document, ContentScript const& script) const;
    
    // API injection
    ErrorOr<void> inject_extension_apis(JS::Realm& realm, ScriptContext context);
    
    // File loading
    ErrorOr<String> load_script_file(String const& relative_path) const;
    ErrorOr<String> load_css_file(String const& relative_path) const;
    ErrorOr<Vector<u8>> load_resource_file(String const& relative_path) const;
    bool is_resource_web_accessible(String const& resource_path, URL::URL const& requesting_origin) const;

    // Permission management
    bool has_permission(String const& permission) const;
    bool has_host_permission(URL::URL const& url) const;
    Vector<ExtensionPermission> const& permissions() const { return m_manifest.permissions(); }

    // Content script management
    Vector<ContentScriptInfo> const& active_content_scripts() const { return m_content_scripts; }
    Optional<BackgroundScriptInfo> const& background_script() const { return m_background_script; }

    // Runtime API access
    void set_extension_api_provider(GC::Ptr<JS::Object> api_provider) { m_extension_api_provider = api_provider; }
    GC::Ptr<JS::Object> extension_api_provider() const { return m_extension_api_provider; }

    // Event handling
    void notify_document_loaded(DOM::Document& document);
    void notify_document_unloaded(DOM::Document& document);
    void notify_navigation_committed(DOM::Document& document, URL::URL const& url);

    // Error handling
    String const& last_error() const { return m_last_error; }
    void set_error(String error) { 
        m_last_error = move(error); 
        m_state = ExtensionState::Error; 
    }

    // URL matching for content scripts
    static bool match_pattern(StringView pattern, URL::URL const& url);
    static bool match_glob(StringView glob_pattern, StringView string);

private:
    Extension(ExtensionManifest manifest, String base_path);

    ErrorOr<void> load_and_validate();
    ErrorOr<String> resolve_file_path(String const& relative_path) const;
    
    ErrorOr<void> execute_content_script_files(ContentScriptInfo& script_info, DOM::Document& document);
    ErrorOr<void> inject_css_files(ContentScriptInfo const& script_info, DOM::Document& document);

    String generate_script_id(ScriptContext context, String const& additional_info = {}) const;

    ExtensionManifest m_manifest;
    String m_base_path;
    ExtensionState m_state { ExtensionState::Disabled };

    // Script contexts
    Vector<ContentScriptInfo> m_content_scripts;
    Optional<BackgroundScriptInfo> m_background_script;

    // Runtime state
    GC::Ptr<JS::Object> m_extension_api_provider;
    String m_last_error;

    // Document tracking for cleanup
    Vector<GC::Ptr<DOM::Document>> m_injected_documents;
};

// URL pattern matching utilities
class URLPattern {
public:
    static ErrorOr<URLPattern> parse(StringView pattern);
    
    bool matches(URL::URL const& url) const;
    
    String const& scheme() const { return m_scheme; }
    String const& host() const { return m_host; }
    String const& path() const { return m_path; }
    
private:
    URLPattern(String scheme, String host, String path);
    
    String m_scheme;
    String m_host;
    String m_path;
    bool m_match_subdomains { false };
};

} 
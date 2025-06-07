/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Extension.h"
#include "ExtensionManifest.h"
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGC/Cell.h>
#include <LibGC/Ptr.h>
#include <LibJS/Runtime/Realm.h>
#include <LibURL/URL.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/Page/Page.h>

namespace Web::Extensions {

enum class InstallationMode {
    Development,    // Load unpacked extension from directory
    CRX,           // Install from .crx file  
    XPI,           // Install from .xpi file (Mozilla)
    WebStore,      // Install from Chrome Web Store (future)
    AMO            // Install from addons.mozilla.org (future)
};

enum class ExtensionType {
    Chrome,
    Mozilla
};

struct ExtensionInstallationRequest {
    String source_path;
    InstallationMode mode;
    ExtensionType extension_type { ExtensionType::Chrome };
    bool enabled_by_default { true };
    Function<void(String const& extension_id, bool success, String const& error)> completion_callback;
};

struct ExtensionEvent {
    enum class Type {
        Installed,
        Enabled,
        Disabled,
        Uninstalled,
        UpdateAvailable,
        Updated,
        Error
    };
    
    Type type;
    String extension_id;
    String details;
};

class ExtensionManager final : public GC::Cell {
    GC_CELL(ExtensionManager, GC::Cell);
    GC_DECLARE_ALLOCATOR(ExtensionManager);

public:
    static GC::Ref<ExtensionManager> create(Page& page);

    ~ExtensionManager() = default;

    // Extension lifecycle
    ErrorOr<String> install_extension(ExtensionInstallationRequest const& request);
    ErrorOr<void> uninstall_extension(String const& extension_id);
    ErrorOr<void> enable_extension(String const& extension_id);
    ErrorOr<void> disable_extension(String const& extension_id);
    ErrorOr<void> reload_extension(String const& extension_id);

    // Extension discovery and loading
    ErrorOr<void> load_extensions_from_directory(String const& extensions_directory);
    ErrorOr<void> scan_for_development_extensions();
    
    // Extension access
    RefPtr<Extension> get_extension(String const& extension_id) const;
    Vector<RefPtr<Extension>> get_all_extensions() const;
    Vector<RefPtr<Extension>> get_enabled_extensions() const;
    
    // Document events - called by the browser engine
    void notify_document_created(DOM::Document& document);
    void notify_document_loaded(DOM::Document& document);
    void notify_document_unloaded(DOM::Document& document);
    void notify_navigation_committed(DOM::Document& document, URL::URL const& url);

    // Content script injection
    ErrorOr<void> inject_content_scripts_for_document(DOM::Document& document);
    
    // Extension resource serving
    bool is_extension_resource_request(URL::URL const& url) const;
    ErrorOr<Vector<u8>> handle_extension_resource_request(URL::URL const& url, URL::URL const& requesting_origin) const;
    
    // Extension API providers
    void register_api_provider(String const& api_name, GC::Ref<JS::Object> provider);
    GC::Ptr<JS::Object> get_api_provider(String const& api_name) const;
    
    // Chrome Web Store integration
    // void inject_chrome_webstore_api_if_needed(DOM::Document& document);

    // Event handling
    void on_extension_event(Function<void(ExtensionEvent const&)> callback);
    
    // Configuration
    void set_extensions_directory(String const& directory) { m_extensions_directory = directory; }
    String const& extensions_directory() const { return m_extensions_directory; }
    
    void set_development_mode(bool enabled) { m_development_mode = enabled; }
    bool development_mode() const { return m_development_mode; }

    // Statistics and debugging
    size_t extension_count() const { return m_extensions.size(); }
    size_t enabled_extension_count() const;
    Vector<String> get_extension_errors() const;

private:
    explicit ExtensionManager(Page& page);

    virtual void visit_edges(GC::Cell::Visitor&) override;

    // Internal extension management
    ErrorOr<String> load_extension_from_directory(String const& extension_path, bool enable_by_default = true);
    ErrorOr<String> generate_extension_id(ExtensionManifest const& manifest, String const& source_path, ExtensionType extension_type = ExtensionType::Chrome);
    ErrorOr<void> validate_extension_installation(Extension const& extension);
    
    // Content script timing
    bool should_inject_content_scripts_now(DOM::Document const& document, String const& run_at);
    
    // Extension URL handling
    static String generate_extension_base_url(String const& extension_id, ExtensionType extension_type = ExtensionType::Chrome);
    static Optional<String> extract_extension_id_from_url(URL::URL const& url);
    
    // Event notification
    void notify_extension_event(ExtensionEvent::Type type, String const& extension_id, String const& details = {});

    // Storage and persistence
    ErrorOr<void> save_extension_state();
    ErrorOr<void> load_extension_state();

    Page& m_page;
    
    // Extension storage
    HashMap<String, RefPtr<Extension>> m_extensions;
    
    // Configuration
    String m_extensions_directory;
    bool m_development_mode { false };
    
    // API providers
    HashMap<String, GC::Ref<JS::Object>> m_api_providers;
    
    // Event callbacks
    Vector<Function<void(ExtensionEvent const&)>> m_event_callbacks;
    
    // Document tracking
    Vector<GC::Ptr<DOM::Document>> m_tracked_documents;
};

} 
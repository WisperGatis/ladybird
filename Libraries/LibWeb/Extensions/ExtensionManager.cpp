/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ExtensionManager.h"
#include <AK/JsonParser.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCrypto/Hash/SHA1.h>
#include <LibFileSystem/FileSystem.h>
#include <LibJS/Runtime/Object.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Extensions/API/ChromeWebStore.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Page/Page.h>

namespace Web::Extensions {

GC_DEFINE_ALLOCATOR(ExtensionManager);

GC::Ref<ExtensionManager> ExtensionManager::create(Page& page)
{
    return page.heap().allocate<ExtensionManager>(page);
}

ExtensionManager::ExtensionManager(Page& page)
    : m_page(page)
{
    // Set default extensions directory
    m_extensions_directory = "/usr/local/share/ladybird/extensions"_string;
}

void ExtensionManager::visit_edges(GC::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    
    for (auto& [id, extension] : m_extensions) {
        // Visit any GC objects in extensions if needed
        (void)id;
        (void)extension;
    }
    
    for (auto& [name, provider] : m_api_providers) {
        visitor.visit(provider);
        (void)name;
    }
    
    for (auto& document : m_tracked_documents) {
        visitor.visit(document);
    }
}

ErrorOr<String> ExtensionManager::install_extension(ExtensionInstallationRequest const& request)
{
    RefPtr<Extension> extension;
    
    switch (request.mode) {
    case InstallationMode::Development:
        extension = TRY(Extension::create_from_directory(request.source_path));
        break;
    case InstallationMode::CRX:
        return Error::from_string_literal("CRX installation not yet implemented");
    case InstallationMode::XPI:
        return Error::from_string_literal("XPI installation not yet implemented");
    case InstallationMode::WebStore:
        return Error::from_string_literal("Web Store installation not yet implemented");
    case InstallationMode::AMO:
        return Error::from_string_literal("addons.mozilla.org installation not yet implemented");
    }
    
    if (!extension) {
        return Error::from_string_literal("Failed to create extension");
    }
    
    // Generate extension ID if not already set
    if (extension->id().is_empty()) {
        auto extension_id = TRY(generate_extension_id(extension->manifest(), request.source_path, request.extension_type));
        extension->mutable_manifest().set_id(extension_id);
    }
    
    // Generate base URL for the extension
    auto base_url_string = generate_extension_base_url(extension->id(), request.extension_type);
    auto base_url_optional = URL::create_with_url_or_path(base_url_string.to_byte_string());
    if (!base_url_optional.has_value()) {
        return Error::from_string_literal("Failed to create base URL for extension");
    }
    extension->mutable_manifest().set_base_url(base_url_optional.value());
    
    // Validate installation
    TRY(validate_extension_installation(*extension));
    
    // Install the extension
    auto extension_id = extension->id();
    m_extensions.set(extension_id, extension);
    
    if (request.enabled_by_default) {
        TRY(enable_extension(extension_id));
    }
    
    notify_extension_event(ExtensionEvent::Type::Installed, extension_id);
    
    if (request.completion_callback) {
        request.completion_callback(extension_id, true, {});
    }
    
    return extension_id;
}

ErrorOr<void> ExtensionManager::uninstall_extension(String const& extension_id)
{
    auto extension = get_extension(extension_id);
    if (!extension) {
        return Error::from_string_literal("Extension not found");
    }
    
    // Disable the extension first
    if (extension->is_enabled()) {
        TRY(disable_extension(extension_id));
    }
    
    // Remove from tracking
    m_extensions.remove(extension_id);
    
    notify_extension_event(ExtensionEvent::Type::Uninstalled, extension_id);
    
    return {};
}

ErrorOr<void> ExtensionManager::enable_extension(String const& extension_id)
{
    auto extension = get_extension(extension_id);
    if (!extension) {
        return Error::from_string_literal("Extension not found");
    }
    
    if (extension->is_enabled()) {
        return {}; // Already enabled
    }
    
    extension->set_state(ExtensionState::Enabled);
    
    // Initialize background scripts if any
    if (extension->manifest().background().has_value()) {
        // TODO: Create proper environment settings object
        // For now, use a dummy one
        auto* settings = static_cast<HTML::EnvironmentSettingsObject*>(nullptr);
        if (settings) {
            TRY(extension->initialize_background_script(*settings));
        }
    }
    
    // Inject content scripts into existing documents
    for (auto& document : m_tracked_documents) {
        if (document) {
            TRY(extension->inject_content_scripts(*document));
        }
    }
    
    notify_extension_event(ExtensionEvent::Type::Enabled, extension_id);
    
    return {};
}

ErrorOr<void> ExtensionManager::disable_extension(String const& extension_id)
{
    auto extension = get_extension(extension_id);
    if (!extension) {
        return Error::from_string_literal("Extension not found");
    }
    
    if (!extension->is_enabled()) {
        return {}; // Already disabled
    }
    
    extension->set_state(ExtensionState::Disabled);
    
    // TODO: Clean up background scripts and content scripts
    // This would involve:
    // - Stopping background script execution
    // - Removing injected content scripts
    // - Cleaning up extension API objects
    
    notify_extension_event(ExtensionEvent::Type::Disabled, extension_id);
    
    return {};
}

ErrorOr<void> ExtensionManager::reload_extension(String const& extension_id)
{
    auto extension = get_extension(extension_id);
    if (!extension) {
        return Error::from_string_literal("Extension not found");
    }
    
    auto was_enabled = extension->is_enabled();
    auto base_path = extension->base_path();
    
    // Disable and uninstall
    TRY(disable_extension(extension_id));
    TRY(uninstall_extension(extension_id));
    
    // Reinstall
    ExtensionInstallationRequest request {
        .source_path = base_path,
        .mode = InstallationMode::Development,
        .enabled_by_default = was_enabled
    };
    
    TRY(install_extension(request));
    
    return {};
}

ErrorOr<void> ExtensionManager::load_extensions_from_directory(String const& extensions_directory)
{
    if (!FileSystem::exists(extensions_directory)) {
        return Error::from_string_literal("Extensions directory does not exist");
    }
    
    auto directory = TRY(Core::Directory::create(extensions_directory.to_byte_string(), Core::Directory::CreateDirectories::No));
    
    TRY(directory.for_each_entry(Core::DirIterator::Flags::SkipDots, [this](auto const& entry, auto const&) -> ErrorOr<IterationDecision> {
        if (entry.type != Core::DirectoryEntry::Type::Directory) {
            return IterationDecision::Continue;
        }
        
        auto extension_path = LexicalPath::join(m_extensions_directory, entry.name).string();
        auto manifest_path = LexicalPath::join(extension_path, "manifest.json"sv).string();
        
        if (!FileSystem::exists(manifest_path)) {
            return IterationDecision::Continue; // Skip directories without manifest
        }
        
        // Try to load the extension
        auto extension_path_string = TRY(String::from_utf8(extension_path.view()));
        auto load_result = load_extension_from_directory(extension_path_string);
        if (load_result.is_error()) {
            // Log error but continue with other extensions
            dbgln("Failed to load extension from {}: {}", extension_path, load_result.error());
        }
        
        return IterationDecision::Continue;
    }));
    
    return {};
}

ErrorOr<void> ExtensionManager::scan_for_development_extensions()
{
    if (!m_development_mode) {
        return {};
    }
    
    // Scan common development directories
    Vector<String> dev_dirs = {
        "/tmp/ladybird-extensions"_string,
        "~/.local/share/ladybird/dev-extensions"_string
    };
    
    for (auto const& dir : dev_dirs) {
        if (FileSystem::exists(dir)) {
            TRY(load_extensions_from_directory(dir));
        }
    }
    
    return {};
}

RefPtr<Extension> ExtensionManager::get_extension(String const& extension_id) const
{
    auto it = m_extensions.find(extension_id);
    if (it != m_extensions.end()) {
        return it->value;
    }
    return nullptr;
}

Vector<RefPtr<Extension>> ExtensionManager::get_all_extensions() const
{
    Vector<RefPtr<Extension>> extensions;
    for (auto const& [id, extension] : m_extensions) {
        extensions.append(extension);
        (void)id;
    }
    return extensions;
}

Vector<RefPtr<Extension>> ExtensionManager::get_enabled_extensions() const
{
    Vector<RefPtr<Extension>> enabled_extensions;
    for (auto const& [id, extension] : m_extensions) {
        if (extension->is_enabled()) {
            enabled_extensions.append(extension);
        }
        (void)id;
    }
    return enabled_extensions;
}

void ExtensionManager::notify_document_created(DOM::Document& document)
{
    // Track the document
    m_tracked_documents.append(&document);
    
    // Inject Chrome Web Store API if we're on a Chrome Web Store page
    // inject_chrome_webstore_api_if_needed(document);
    
    // This is called early in document creation, content scripts will be injected later
}

void ExtensionManager::notify_document_loaded(DOM::Document& document)
{
    (void)inject_content_scripts_for_document(document);
}

void ExtensionManager::notify_document_unloaded(DOM::Document& document)
{
    // Remove from tracking
    m_tracked_documents.remove_all_matching([&document](auto const& tracked_doc) {
        return tracked_doc.ptr() == &document;
    });
    
    // Notify extensions about the unloaded document
    for (auto const& [id, extension] : m_extensions) {
        if (extension->is_enabled()) {
            extension->notify_document_unloaded(document);
        }
        (void)id;
    }
}

void ExtensionManager::notify_navigation_committed(DOM::Document& document, URL::URL const& url)
{
    (void)url; // Mark as intentionally unused
    
    // Remove any existing content scripts for this document
    notify_document_unloaded(document);
    
    // Inject content scripts for the new document
    (void)inject_content_scripts_for_document(document);
}

ErrorOr<void> ExtensionManager::inject_content_scripts_for_document(DOM::Document& document)
{
    for (auto const& [id, extension] : m_extensions) {
        if (extension->is_enabled()) {
            TRY(extension->inject_content_scripts(document));
        }
        (void)id;
    }
    return {};
}

bool ExtensionManager::is_extension_resource_request(URL::URL const& url) const
{
    return url.scheme() == "chrome-extension" || url.scheme() == "moz-extension";
}

ErrorOr<Vector<u8>> ExtensionManager::handle_extension_resource_request(URL::URL const& url, URL::URL const& requesting_origin) const
{
    if (!is_extension_resource_request(url)) {
        return Error::from_string_literal("Not an extension resource request");
    }
    
    auto extension_id = extract_extension_id_from_url(url);
    if (!extension_id.has_value()) {
        return Error::from_string_literal("Invalid extension URL");
    }
    
    auto extension = get_extension(*extension_id);
    if (!extension) {
        return Error::from_string_literal("Extension not found");
    }
    
    if (!extension->is_enabled()) {
        return Error::from_string_literal("Extension is disabled");
    }
    
    auto resource_path = url.serialize_path();
    if (resource_path.starts_with('/')) {
        resource_path = resource_path.substring_from_byte_offset(1).release_value_but_fixme_should_propagate_errors();
    }
    
    // Check if the resource is web accessible
    if (!extension->is_resource_web_accessible(resource_path, requesting_origin)) {
        return Error::from_string_literal("Resource is not web accessible");
    }
    
    return extension->load_resource_file(resource_path);
}

void ExtensionManager::register_api_provider(String const& api_name, GC::Ref<JS::Object> provider)
{
    m_api_providers.set(api_name, provider);
}

GC::Ptr<JS::Object> ExtensionManager::get_api_provider(String const& api_name) const
{
    auto it = m_api_providers.find(api_name);
    if (it != m_api_providers.end()) {
        return it->value;
    }
    return nullptr;
}

void ExtensionManager::on_extension_event(Function<void(ExtensionEvent const&)> callback)
{
    m_event_callbacks.append(move(callback));
}

size_t ExtensionManager::enabled_extension_count() const
{
    size_t count = 0;
    for (auto const& [id, extension] : m_extensions) {
        if (extension->is_enabled()) {
            count++;
        }
        (void)id;
    }
    return count;
}

Vector<String> ExtensionManager::get_extension_errors() const
{
    Vector<String> errors;
    for (auto const& [id, extension] : m_extensions) {
        if (extension->state() == ExtensionState::Error) {
            auto error_message = String::formatted("{}: {}", extension->name(), extension->last_error());
            if (error_message.is_error()) {
                errors.append("Extension error (formatting failed)"_string);
            } else {
                errors.append(error_message.release_value());
            }
        }
        (void)id;
    }
    return errors;
}

ErrorOr<String> ExtensionManager::load_extension_from_directory(String const& extension_path, bool enable_by_default)
{
    // Try to detect extension type by reading manifest.json
    auto manifest_path = LexicalPath::join(extension_path, "manifest.json"sv).string();
    ExtensionType extension_type = ExtensionType::Chrome; // Default to Chrome
    
    if (FileSystem::exists(manifest_path)) {
        auto manifest_file_result = Core::File::open(manifest_path, Core::File::OpenMode::Read);
        if (!manifest_file_result.is_error()) {
            auto manifest_content_result = manifest_file_result.value()->read_until_eof();
            if (!manifest_content_result.is_error()) {
                auto manifest_json_result = JsonParser::parse(manifest_content_result.value());
                if (!manifest_json_result.is_error() && manifest_json_result.value().is_object()) {
                    auto const& manifest_obj = manifest_json_result.value().as_object();
                    // Check for Mozilla-specific fields
                    if (manifest_obj.has("applications"sv) || manifest_obj.has("browser_specific_settings"sv)) {
                        extension_type = ExtensionType::Mozilla;
                    }
                }
            }
        }
    }
    
    ExtensionInstallationRequest request {
        .source_path = extension_path,
        .mode = InstallationMode::Development,
        .extension_type = extension_type,
        .enabled_by_default = enable_by_default
    };
    
    return install_extension(request);
}

ErrorOr<String> ExtensionManager::generate_extension_id(ExtensionManifest const& manifest, String const& source_path, ExtensionType extension_type)
{
    // For Mozilla extensions, prefer the gecko ID if available
    if (extension_type == ExtensionType::Mozilla && manifest.platform() == ExtensionPlatform::Mozilla) {
        auto const& gecko_id = manifest.gecko_id();
        if (gecko_id.has_value() && !gecko_id->is_empty()) {
            return *gecko_id;
        }
    }
    
    // Generate a deterministic ID based on the manifest name and source path
    // This ensures the same extension gets the same ID across runs
    
    StringBuilder id_input;
    id_input.append(manifest.name());
    id_input.append(source_path);
    
    auto hash = ::Crypto::Hash::SHA1::hash(id_input.string_view().bytes());
    
    if (extension_type == ExtensionType::Mozilla) {
        // For Mozilla extensions without gecko ID, generate a UUID-like ID
        StringBuilder extension_id;
        extension_id.append('{');
        for (size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                extension_id.append('-');
            }
            extension_id.appendff("{:02x}", hash.data[i]);
        }
        extension_id.append('}');
        return extension_id.to_string();
    } else {
        // Convert hash to a Chrome extension-style ID (32 characters, a-p only)
        StringBuilder extension_id;
        for (size_t i = 0; i < 16; ++i) {
            auto byte = hash.data[i];
            extension_id.append(static_cast<char>('a' + (byte & 0x0F)));
            extension_id.append(static_cast<char>('a' + ((byte >> 4) & 0x0F)));
        }
        return extension_id.to_string();
    }
}

ErrorOr<void> ExtensionManager::validate_extension_installation(Extension const& extension)
{
    // Check for conflicts with existing extensions
    for (auto const& [id, existing_extension] : m_extensions) {
        if (existing_extension->name() == extension.name() && existing_extension->id() != extension.id()) {
            return Error::from_string_literal("Extension with same name already installed");
        }
        (void)id;
    }
    
    // Validate manifest
    if (!extension.manifest().is_valid()) {
        return Error::from_string_literal("Extension manifest is invalid");
    }
    
    return {};
}

bool ExtensionManager::should_inject_content_scripts_now(DOM::Document const& document, String const& run_at)
{
    if (run_at == "document_start"sv) {
        return true; // Always inject at document start
    } else if (run_at == "document_end"sv) {
        return document.ready_state() != "loading"sv;
    } else if (run_at == "document_idle"sv) {
        return document.ready_state() == "complete"sv;
    }
    
    return false; // Unknown run_at value
}

String ExtensionManager::generate_extension_base_url(String const& extension_id, ExtensionType extension_type)
{
    if (extension_type == ExtensionType::Mozilla) {
        return MUST(String::formatted("moz-extension://{}/", extension_id));
    } else {
        return MUST(String::formatted("chrome-extension://{}/", extension_id));
    }
}

Optional<String> ExtensionManager::extract_extension_id_from_url(URL::URL const& url)
{
    if (url.scheme() != "chrome-extension" && url.scheme() != "moz-extension") {
        return {};
    }
    
    auto host = url.host();
    if (!host.has_value()) {
        return {};
    }
    
    return host->serialize();
}

void ExtensionManager::notify_extension_event(ExtensionEvent::Type type, String const& extension_id, String const& details)
{
    ExtensionEvent event {
        .type = type,
        .extension_id = extension_id,
        .details = details
    };
    
    for (auto const& callback : m_event_callbacks) {
        callback(event);
    }
}

/*
void ExtensionManager::inject_chrome_webstore_api_if_needed(DOM::Document& document)
{
    auto const& url = document.url();
    auto url_string = url.to_byte_string();
    
    // Check if this is a Chrome Web Store page
    bool is_webstore_page = url_string.contains("chrome.google.com/webstore"sv) ||
                           url_string.contains("chromewebstore.google.com"sv);
    
    if (!is_webstore_page) {
        return;
    }
    
    // Get the window object
    auto window = document.window();
    if (!window) {
        return;
    }
    
    auto& realm = window->realm();
    
    // Create the chrome.webstore API object
    auto web_store_api = API::ChromeWebStore::create(realm);
    
    // Create the chrome object if it doesn't exist
    auto chrome_obj = JS::Object::create(realm, realm.intrinsics().object_prototype());
    chrome_obj->define_direct_property("webstore"_string, web_store_api, JS::default_attributes);
    
    // Inject into the global window object
    window->define_direct_property("chrome"_string, chrome_obj, JS::default_attributes);
    
    dbgln("ExtensionManager: Injected Chrome Web Store API into {}", url_string);
}
*/

ErrorOr<void> ExtensionManager::save_extension_state()
{
    // TODO: Implement persistence of extension state
    // This would save which extensions are installed and enabled
    return {};
}

ErrorOr<void> ExtensionManager::load_extension_state()
{
    // TODO: Implement loading of persisted extension state
    // This would restore previously installed extensions
    return {};
}

} 
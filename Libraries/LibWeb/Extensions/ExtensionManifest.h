/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibURL/URL.h>

namespace Web::Extensions {

enum class ManifestVersion {
    V2 = 2,
    V3 = 3
};

enum class ExtensionPlatform {
    Chrome,
    Mozilla
};

struct ExtensionPermission {
    enum class Type {
        API,
        Host,
        ActiveTab,
        Tabs,
        Storage,
        Background,
        Scripting,
        WebRequest,
        WebRequestBlocking,
        ContextMenus,
        Cookies,
        History,
        Bookmarks,
        Downloads,
        Management,
        Notifications,
        Identity,
        WebNavigation,
        DeclarativeContent,
        PageCapture,
        TopSites,
        FontSettings,
        Privacy,
        System,
        TTS,
        WebAuthN,
        Alarms,
        OffScreen,
        SidePanel,
        Action,
        Commands,
        ContextMenus2,
        DesktopCapture,
        DisplaySource,
        DocumentScan,
        Enterprise,
        FileBrowserHandler,
        FileSystemProvider,
        GCM,
        Geolocation,
        Idle,
        LoginState,
        NativeMessaging,
        Platformkeys,
        Power,
        PrinterProvider,
        Search,
        Sessions,
        SignedInDevices,
        TabCapture,
        TabGroups,
        Terminal,
        VPN,
        WallPaper,
        WebRequest2
    };

    Type type;
    String value;
    Vector<String> host_patterns;

    static Optional<ExtensionPermission> from_string(StringView permission_string);
};

struct ContentScript {
    Vector<String> matches;
    Vector<String> exclude_matches;
    Vector<String> include_globs;
    Vector<String> exclude_globs;
    Vector<String> js;
    Vector<String> css;
    String run_at;
    bool all_frames { false };
    bool match_about_blank { false };
    Vector<String> exclude_matches_pattern;
    
    ContentScript() : run_at("document_idle"_string) {}
};

struct BackgroundScript {
    Vector<String> scripts;
    String type;
    bool persistent { true }; // false for MV3
    String service_worker; // MV3 only
    
    BackgroundScript() : type("classic"_string) {}
};

struct ExtensionAction {
    String default_title;
    String default_icon;
    String default_popup;
    HashMap<String, String> icons; // size -> path
};

struct WebAccessibleResource {
    Vector<String> resources;
    Vector<String> matches;
    Vector<String> extension_ids;
    bool use_dynamic_url { false };
};

class ExtensionManifest {
public:
    static ErrorOr<ExtensionManifest> parse_from_json(JsonObject const& manifest_json);
    static ErrorOr<ExtensionManifest> parse_chrome_manifest(JsonObject const& manifest_json);
    static ErrorOr<ExtensionManifest> parse_mozilla_manifest(JsonObject const& manifest_json);

    ManifestVersion manifest_version() const { return m_manifest_version; }
    String const& name() const { return m_name; }
    String const& version() const { return m_version; }
    String const& description() const { return m_description; }
    String const& id() const { return m_id; }

    Vector<ExtensionPermission> const& permissions() const { return m_permissions; }
    Vector<ExtensionPermission> const& optional_permissions() const { return m_optional_permissions; }
    Vector<String> const& host_permissions() const { return m_host_permissions; }

    Vector<ContentScript> const& content_scripts() const { return m_content_scripts; }
    Optional<BackgroundScript> const& background() const { return m_background; }
    
    Optional<ExtensionAction> const& action() const { return m_action; }
    Optional<ExtensionAction> const& browser_action() const { return m_browser_action; }
    Optional<ExtensionAction> const& page_action() const { return m_page_action; }

    Vector<WebAccessibleResource> const& web_accessible_resources() const { return m_web_accessible_resources; }

    HashMap<String, String> const& icons() const { return m_icons; }
    
    String const& minimum_chrome_version() const { return m_minimum_chrome_version; }
    
    String const& content_security_policy() const { return m_content_security_policy; }

    bool is_valid() const { return m_is_valid; }
    String const& validation_error() const { return m_validation_error; }

    ExtensionPlatform platform() const { return m_platform; }
    void set_platform(ExtensionPlatform platform) { m_platform = platform; }
    
    // Mozilla-specific fields
    Optional<String> const& gecko_id() const { return m_gecko_id; }
    Optional<String> const& strict_min_version() const { return m_strict_min_version; }
    Optional<String> const& strict_max_version() const { return m_strict_max_version; }
    Vector<String> const& applications() const { return m_applications; }

    // Runtime accessors
    void set_id(String id) { m_id = move(id); }
    void set_base_url(URL::URL url) { m_base_url = move(url); }
    URL::URL const& base_url() const { return m_base_url; }

private:
    ExtensionManifest() = default;

    ErrorOr<void> parse_permissions(JsonArray const& permissions_array, Vector<ExtensionPermission>& target);
    ErrorOr<void> parse_content_scripts(JsonArray const& content_scripts_array);
    ErrorOr<void> parse_background(JsonObject const& background_object);
    ErrorOr<void> parse_action(JsonObject const& action_object, ExtensionAction& target);
    ErrorOr<void> parse_web_accessible_resources(JsonValue const& war_value);
    ErrorOr<void> parse_icons(JsonObject const& icons_object);
    
    // Mozilla-specific parsing
    ErrorOr<void> parse_mozilla_applications(JsonObject const& applications_object);
    ErrorOr<void> parse_mozilla_browser_specific_settings(JsonObject const& browser_settings);

    bool validate();
    bool validate_chrome_manifest();
    bool validate_mozilla_manifest();

    ManifestVersion m_manifest_version { ManifestVersion::V3 };
    ExtensionPlatform m_platform { ExtensionPlatform::Chrome };
    String m_name;
    String m_version;
    String m_description;
    String m_id;

    Vector<ExtensionPermission> m_permissions;
    Vector<ExtensionPermission> m_optional_permissions;
    Vector<String> m_host_permissions;

    Vector<ContentScript> m_content_scripts;
    Optional<BackgroundScript> m_background;

    Optional<ExtensionAction> m_action; // MV3
    Optional<ExtensionAction> m_browser_action; // MV2
    Optional<ExtensionAction> m_page_action; // MV2

    Vector<WebAccessibleResource> m_web_accessible_resources;
    HashMap<String, String> m_icons;

    String m_minimum_chrome_version;
    String m_content_security_policy;

    // Mozilla-specific fields
    Optional<String> m_gecko_id;
    Optional<String> m_strict_min_version;
    Optional<String> m_strict_max_version;
    Vector<String> m_applications;

    // Runtime data
    URL::URL m_base_url;

    // Validation
    bool m_is_valid { false };
    String m_validation_error;
};

} 
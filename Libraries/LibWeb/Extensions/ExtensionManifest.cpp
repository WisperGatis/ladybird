/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ExtensionManifest.h"
#include <AK/JsonParser.h>
#include <AK/StringBuilder.h>
#include <LibURL/URL.h>

namespace Web::Extensions {

ErrorOr<ExtensionManifest> ExtensionManifest::parse_from_json(JsonObject const& manifest_json)
{
    // Try to detect manifest type from distinctive fields
    bool has_applications = manifest_json.has("applications"sv) || manifest_json.has("browser_specific_settings"sv);
    bool has_gecko_id = false;
    
    if (has_applications) {
        auto applications_value = manifest_json.get("applications"sv);
        if (!applications_value.has_value()) {
            applications_value = manifest_json.get("browser_specific_settings"sv);
        }
        
        if (applications_value.has_value() && applications_value->is_object()) {
            has_gecko_id = applications_value->as_object().has("gecko"sv);
        }
    }
    
    // Determine platform based on manifest fields
    if (has_gecko_id) {
        return parse_mozilla_manifest(manifest_json);
    } else {
        return parse_chrome_manifest(manifest_json);
    }
}

ErrorOr<ExtensionManifest> ExtensionManifest::parse_chrome_manifest(JsonObject const& manifest_json)
{
    ExtensionManifest manifest;
    manifest.m_platform = ExtensionPlatform::Chrome;
    
    // Parse manifest version (required)
    auto manifest_version_value = manifest_json.get("manifest_version"sv);
    if (!manifest_version_value.has_value() || !manifest_version_value->is_number()) {
        manifest.m_validation_error = "manifest_version is required and must be a number"_string;
        return manifest;
    }
    
    auto version_number = manifest_version_value->as_number();
    if (version_number.get<double>() == 2.0) {
        manifest.m_manifest_version = ManifestVersion::V2;
    } else if (version_number.get<double>() == 3.0) {
        manifest.m_manifest_version = ManifestVersion::V3;
    } else {
        manifest.m_validation_error = "manifest_version must be 2 or 3"_string;
        return manifest;
    }
    
    // Parse name (required)
    auto name_value = manifest_json.get("name"sv);
    if (!name_value.has_value() || !name_value->is_string()) {
        manifest.m_validation_error = "name is required and must be a string"_string;
        return manifest;
    }
    manifest.m_name = name_value->as_string();
    
    // Parse version (required)
    auto version_value = manifest_json.get("version"sv);
    if (!version_value.has_value() || !version_value->is_string()) {
        manifest.m_validation_error = "version is required and must be a string"_string;
        return manifest;
    }
    manifest.m_version = version_value->as_string();
    
    // Parse description (optional)
    auto description_value = manifest_json.get("description"sv);
    if (description_value.has_value() && description_value->is_string()) {
        manifest.m_description = description_value->as_string();
    }
    
    // Parse permissions (optional)
    auto permissions_value = manifest_json.get("permissions"sv);
    if (permissions_value.has_value() && permissions_value->is_array()) {
        TRY(manifest.parse_permissions(permissions_value->as_array(), manifest.m_permissions));
    }
    
    // Parse optional_permissions (optional, MV3)
    auto optional_permissions_value = manifest_json.get("optional_permissions"sv);
    if (optional_permissions_value.has_value() && optional_permissions_value->is_array()) {
        TRY(manifest.parse_permissions(optional_permissions_value->as_array(), manifest.m_optional_permissions));
    }
    
    // Parse host_permissions (MV3) or extract from permissions (MV2)
    auto host_permissions_value = manifest_json.get("host_permissions"sv);
    if (host_permissions_value.has_value() && host_permissions_value->is_array()) {
        for (auto const& permission : host_permissions_value->as_array().values()) {
            if (permission.is_string()) {
                manifest.m_host_permissions.append(permission.as_string());
            }
        }
    }
    
    // Parse content_scripts (optional)
    auto content_scripts_value = manifest_json.get("content_scripts"sv);
    if (content_scripts_value.has_value() && content_scripts_value->is_array()) {
        TRY(manifest.parse_content_scripts(content_scripts_value->as_array()));
    }
    
    // Parse background (optional)
    auto background_value = manifest_json.get("background"sv);
    if (background_value.has_value() && background_value->is_object()) {
        TRY(manifest.parse_background(background_value->as_object()));
    }
    
    // Parse action (MV3) or browser_action/page_action (MV2)
    auto action_value = manifest_json.get("action"sv);
    if (action_value.has_value() && action_value->is_object()) {
        manifest.m_action = ExtensionAction {};
        TRY(manifest.parse_action(action_value->as_object(), *manifest.m_action));
    }
    
    auto browser_action_value = manifest_json.get("browser_action"sv);
    if (browser_action_value.has_value() && browser_action_value->is_object()) {
        manifest.m_browser_action = ExtensionAction {};
        TRY(manifest.parse_action(browser_action_value->as_object(), *manifest.m_browser_action));
    }
    
    auto page_action_value = manifest_json.get("page_action"sv);
    if (page_action_value.has_value() && page_action_value->is_object()) {
        manifest.m_page_action = ExtensionAction {};
        TRY(manifest.parse_action(page_action_value->as_object(), *manifest.m_page_action));
    }
    
    // Parse web_accessible_resources (optional)
    auto war_value = manifest_json.get("web_accessible_resources"sv);
    if (war_value.has_value()) {
        TRY(manifest.parse_web_accessible_resources(*war_value));
    }
    
    // Parse icons (optional)
    auto icons_value = manifest_json.get("icons"sv);
    if (icons_value.has_value() && icons_value->is_object()) {
        TRY(manifest.parse_icons(icons_value->as_object()));
    }
    
    // Parse minimum_chrome_version (optional)
    auto min_chrome_value = manifest_json.get("minimum_chrome_version"sv);
    if (min_chrome_value.has_value() && min_chrome_value->is_string()) {
        manifest.m_minimum_chrome_version = min_chrome_value->as_string();
    }
    
    // Parse content_security_policy (optional)
    auto csp_value = manifest_json.get("content_security_policy"sv);
    if (csp_value.has_value()) {
        if (csp_value->is_string()) {
            // MV2 format
            manifest.m_content_security_policy = csp_value->as_string();
        } else if (csp_value->is_object()) {
            // MV3 format - has extension_pages and sandbox properties
            auto extension_pages_csp = csp_value->as_object().get("extension_pages"sv);
            if (extension_pages_csp.has_value() && extension_pages_csp->is_string()) {
                manifest.m_content_security_policy = extension_pages_csp->as_string();
            }
        }
    }
    
    // Validate the manifest
    manifest.m_is_valid = manifest.validate();
    
    return manifest;
}

ErrorOr<void> ExtensionManifest::parse_permissions(JsonArray const& permissions_array, Vector<ExtensionPermission>& target)
{
    for (auto const& permission_value : permissions_array.values()) {
        if (!permission_value.is_string())
            continue;
            
        auto permission_string = permission_value.as_string();
        auto permission = ExtensionPermission::from_string(permission_string);
        if (permission.has_value()) {
            target.append(permission.release_value());
        } else {
            // Treat unknown permissions as host permissions for now
            ExtensionPermission host_permission;
            host_permission.type = ExtensionPermission::Type::Host;
            host_permission.value = permission_string;
            target.append(move(host_permission));
        }
    }
    return {};
}

ErrorOr<void> ExtensionManifest::parse_content_scripts(JsonArray const& content_scripts_array)
{
    for (auto const& content_script_value : content_scripts_array.values()) {
        if (!content_script_value.is_object())
            continue;
        
        auto const& content_script_obj = content_script_value.as_object();
        ContentScript content_script;
        
        // Parse matches (required)
        auto matches_value = content_script_obj.get("matches"sv);
        if (matches_value.has_value() && matches_value->is_array()) {
            for (auto const& match : matches_value->as_array().values()) {
                if (match.is_string()) {
                    content_script.matches.append(match.as_string());
                }
            }
        }
        
        // Parse exclude_matches (optional)
        auto exclude_matches_value = content_script_obj.get("exclude_matches"sv);
        if (exclude_matches_value.has_value() && exclude_matches_value->is_array()) {
            for (auto const& exclude_match : exclude_matches_value->as_array().values()) {
                if (exclude_match.is_string()) {
                    content_script.exclude_matches.append(exclude_match.as_string());
                }
            }
        }
        
        // Parse js files (optional)
        auto js_value = content_script_obj.get("js"sv);
        if (js_value.has_value() && js_value->is_array()) {
            for (auto const& js_file : js_value->as_array().values()) {
                if (js_file.is_string()) {
                    content_script.js.append(js_file.as_string());
                }
            }
        }
        
        // Parse css files (optional)
        auto css_value = content_script_obj.get("css"sv);
        if (css_value.has_value() && css_value->is_array()) {
            for (auto const& css_file : css_value->as_array().values()) {
                if (css_file.is_string()) {
                    content_script.css.append(css_file.as_string());
                }
            }
        }
        
        // Parse run_at (optional)
        auto run_at_value = content_script_obj.get("run_at"sv);
        if (run_at_value.has_value() && run_at_value->is_string()) {
            content_script.run_at = run_at_value->as_string();
        }
        
        // Parse all_frames (optional)
        auto all_frames_value = content_script_obj.get("all_frames"sv);
        if (all_frames_value.has_value() && all_frames_value->is_bool()) {
            content_script.all_frames = all_frames_value->as_bool();
        }
        
        // Parse match_about_blank (optional)
        auto match_about_blank_value = content_script_obj.get("match_about_blank"sv);
        if (match_about_blank_value.has_value() && match_about_blank_value->is_bool()) {
            content_script.match_about_blank = match_about_blank_value->as_bool();
        }
        
        m_content_scripts.append(move(content_script));
    }
    return {};
}

ErrorOr<void> ExtensionManifest::parse_background(JsonObject const& background_object)
{
    BackgroundScript background;
    
    if (m_manifest_version == ManifestVersion::V2) {
        // MV2: scripts array or page
        auto scripts_value = background_object.get("scripts"sv);
        if (scripts_value.has_value() && scripts_value->is_array()) {
            for (auto const& script : scripts_value->as_array().values()) {
                if (script.is_string()) {
                    background.scripts.append(script.as_string());
                }
            }
        }
        
        auto persistent_value = background_object.get("persistent"sv);
        if (persistent_value.has_value() && persistent_value->is_bool()) {
            background.persistent = persistent_value->as_bool();
        }
    } else {
        // MV3: service_worker
        auto service_worker_value = background_object.get("service_worker"sv);
        if (service_worker_value.has_value() && service_worker_value->is_string()) {
            background.service_worker = service_worker_value->as_string();
            background.persistent = false; // Always false for MV3
        }
    }
    
    auto type_value = background_object.get("type"sv);
    if (type_value.has_value() && type_value->is_string()) {
        background.type = type_value->as_string();
    }
    
    m_background = move(background);
    return {};
}

ErrorOr<void> ExtensionManifest::parse_action(JsonObject const& action_object, ExtensionAction& target)
{
    auto default_title_value = action_object.get("default_title"sv);
    if (default_title_value.has_value() && default_title_value->is_string()) {
        target.default_title = default_title_value->as_string();
    }
    
    auto default_icon_value = action_object.get("default_icon"sv);
    if (default_icon_value.has_value()) {
        if (default_icon_value->is_string()) {
            target.default_icon = default_icon_value->as_string();
        } else if (default_icon_value->is_object()) {
            // Parse icon sizes
            auto const& icon_object = default_icon_value->as_object();
            icon_object.for_each_member([&](auto const& key, auto const& path) {
                if (path.is_string()) {
                    target.icons.set(key, path.as_string());
                }
            });
        }
    }
    
    auto default_popup_value = action_object.get("default_popup"sv);
    if (default_popup_value.has_value() && default_popup_value->is_string()) {
        target.default_popup = default_popup_value->as_string();
    }
    
    return {};
}

ErrorOr<void> ExtensionManifest::parse_web_accessible_resources(JsonValue const& war_value)
{
    if (war_value.is_array()) {
        // MV2 format: simple array of strings
        for (auto const& resource : war_value.as_array().values()) {
            if (resource.is_string()) {
                WebAccessibleResource war;
                war.resources.append(resource.as_string());
                m_web_accessible_resources.append(move(war));
            }
        }
    } else if (war_value.is_object()) {
        // MV3 format: array of objects with resources, matches, etc.
        for (auto const& war_entry : war_value.as_array().values()) {
            if (!war_entry.is_object())
                continue;
            
            WebAccessibleResource war;
            auto const& war_obj = war_entry.as_object();
            
            auto resources_value = war_obj.get("resources"sv);
            if (resources_value.has_value() && resources_value->is_array()) {
                for (auto const& resource : resources_value->as_array().values()) {
                    if (resource.is_string()) {
                        war.resources.append(resource.as_string());
                    }
                }
            }
            
            auto matches_value = war_obj.get("matches"sv);
            if (matches_value.has_value() && matches_value->is_array()) {
                for (auto const& match : matches_value->as_array().values()) {
                    if (match.is_string()) {
                        war.matches.append(match.as_string());
                    }
                }
            }
            
            m_web_accessible_resources.append(move(war));
        }
    }
    return {};
}

ErrorOr<void> ExtensionManifest::parse_icons(JsonObject const& icons_object)
{
    icons_object.for_each_member([&](auto const& size, auto const& path) {
        if (path.is_string()) {
            m_icons.set(size, path.as_string());
        }
    });
    return {};
}

bool ExtensionManifest::validate()
{
    // Basic validation
    if (m_name.is_empty()) {
        m_validation_error = "Extension name cannot be empty"_string;
        return false;
    }
    
    if (m_version.is_empty()) {
        m_validation_error = "Extension version cannot be empty"_string;
        return false;
    }
    
    // Manifest version specific validation
    if (m_manifest_version == ManifestVersion::V3) {
        // MV3 requires action instead of browser_action/page_action
        if (m_browser_action.has_value() || m_page_action.has_value()) {
            m_validation_error = "Manifest V3 should use 'action' instead of 'browser_action' or 'page_action'"_string;
            return false;
        }
        
        // MV3 background scripts should be service workers
        if (m_background.has_value() && !m_background->scripts.is_empty() && m_background->service_worker.is_empty()) {
            m_validation_error = "Manifest V3 background should use 'service_worker' instead of 'scripts'"_string;
            return false;
        }
    }
    
    return true;
}

Optional<ExtensionPermission> ExtensionPermission::from_string(StringView permission_string)
{
    ExtensionPermission permission;
    
    // Handle host permissions (URLs)
    if (permission_string.contains("://"sv) || permission_string.starts_with("*"sv) || permission_string.starts_with("http"sv) || permission_string.starts_with("https"sv)) {
        permission.type = Type::Host;
        permission.value = String::from_utf8(permission_string).release_value_but_fixme_should_propagate_errors();
        return permission;
    }
    
    // Handle API permissions
    if (permission_string == "activeTab"sv) {
        permission.type = Type::ActiveTab;
    } else if (permission_string == "tabs"sv) {
        permission.type = Type::Tabs;
    } else if (permission_string == "storage"sv) {
        permission.type = Type::Storage;
    } else if (permission_string == "background"sv) {
        permission.type = Type::Background;
    } else if (permission_string == "scripting"sv) {
        permission.type = Type::Scripting;
    } else if (permission_string == "webRequest"sv) {
        permission.type = Type::WebRequest;
    } else if (permission_string == "webRequestBlocking"sv) {
        permission.type = Type::WebRequestBlocking;
    } else if (permission_string == "contextMenus"sv) {
        permission.type = Type::ContextMenus;
    } else if (permission_string == "cookies"sv) {
        permission.type = Type::Cookies;
    } else if (permission_string == "history"sv) {
        permission.type = Type::History;
    } else if (permission_string == "bookmarks"sv) {
        permission.type = Type::Bookmarks;
    } else if (permission_string == "downloads"sv) {
        permission.type = Type::Downloads;
    } else if (permission_string == "management"sv) {
        permission.type = Type::Management;
    } else if (permission_string == "notifications"sv) {
        permission.type = Type::Notifications;
    } else if (permission_string == "identity"sv) {
        permission.type = Type::Identity;
    } else if (permission_string == "webNavigation"sv) {
        permission.type = Type::WebNavigation;
    } else if (permission_string == "declarativeContent"sv) {
        permission.type = Type::DeclarativeContent;
    } else if (permission_string == "pageCapture"sv) {
        permission.type = Type::PageCapture;
    } else if (permission_string == "topSites"sv) {
        permission.type = Type::TopSites;
    } else if (permission_string == "fontSettings"sv) {
        permission.type = Type::FontSettings;
    } else if (permission_string == "privacy"sv) {
        permission.type = Type::Privacy;
    } else if (permission_string == "system.display"sv || permission_string == "system.storage"sv || permission_string == "system.cpu"sv || permission_string == "system.memory"sv) {
        permission.type = Type::System;
    } else if (permission_string == "tts"sv) {
        permission.type = Type::TTS;
    } else if (permission_string == "webAuthN"sv) {
        permission.type = Type::WebAuthN;
    } else if (permission_string == "alarms"sv) {
        permission.type = Type::Alarms;
    } else if (permission_string == "offScreen"sv) {
        permission.type = Type::OffScreen;
    } else if (permission_string == "sidePanel"sv) {
        permission.type = Type::SidePanel;
    } else {
        // Unknown permission
        return {};
    }
    
    permission.value = String::from_utf8(permission_string).release_value_but_fixme_should_propagate_errors();
    return permission;
}

ErrorOr<ExtensionManifest> ExtensionManifest::parse_mozilla_manifest(JsonObject const& manifest_json)
{
    ExtensionManifest manifest;
    manifest.m_platform = ExtensionPlatform::Mozilla;
    
    // Parse manifest version (required)
    auto manifest_version_value = manifest_json.get("manifest_version"sv);
    if (!manifest_version_value.has_value() || !manifest_version_value->is_number()) {
        manifest.m_validation_error = "manifest_version is required and must be a number"_string;
        return manifest;
    }
    
    auto version_number = manifest_version_value->as_number();
    if (version_number.get<double>() == 2.0) {
        manifest.m_manifest_version = ManifestVersion::V2;
    } else if (version_number.get<double>() == 3.0) {
        manifest.m_manifest_version = ManifestVersion::V3;
    } else {
        manifest.m_validation_error = "manifest_version must be 2 or 3"_string;
        return manifest;
    }
    
    // Parse name (required)
    auto name_value = manifest_json.get("name"sv);
    if (!name_value.has_value() || !name_value->is_string()) {
        manifest.m_validation_error = "name is required and must be a string"_string;
        return manifest;
    }
    manifest.m_name = name_value->as_string();
    
    // Parse version (required)
    auto version_value = manifest_json.get("version"sv);
    if (!version_value.has_value() || !version_value->is_string()) {
        manifest.m_validation_error = "version is required and must be a string"_string;
        return manifest;
    }
    manifest.m_version = version_value->as_string();
    
    // Parse description (optional)
    auto description_value = manifest_json.get("description"sv);
    if (description_value.has_value() && description_value->is_string()) {
        manifest.m_description = description_value->as_string();
    }
    
    // Parse Mozilla-specific applications field
    auto applications_value = manifest_json.get("applications"sv);
    if (!applications_value.has_value()) {
        applications_value = manifest_json.get("browser_specific_settings"sv);
    }
    
    if (applications_value.has_value() && applications_value->is_object()) {
        TRY(manifest.parse_mozilla_applications(applications_value->as_object()));
    }
    
    // Parse permissions (optional)
    auto permissions_value = manifest_json.get("permissions"sv);
    if (permissions_value.has_value() && permissions_value->is_array()) {
        TRY(manifest.parse_permissions(permissions_value->as_array(), manifest.m_permissions));
    }
    
    // Parse optional_permissions (optional)
    auto optional_permissions_value = manifest_json.get("optional_permissions"sv);
    if (optional_permissions_value.has_value() && optional_permissions_value->is_array()) {
        TRY(manifest.parse_permissions(optional_permissions_value->as_array(), manifest.m_optional_permissions));
    }
    
    // Parse content_scripts (optional)
    auto content_scripts_value = manifest_json.get("content_scripts"sv);
    if (content_scripts_value.has_value() && content_scripts_value->is_array()) {
        TRY(manifest.parse_content_scripts(content_scripts_value->as_array()));
    }
    
    // Parse background (optional)
    auto background_value = manifest_json.get("background"sv);
    if (background_value.has_value() && background_value->is_object()) {
        TRY(manifest.parse_background(background_value->as_object()));
    }
    
    // Parse browser_action (Mozilla equivalent to Chrome's action)
    auto browser_action_value = manifest_json.get("browser_action"sv);
    if (browser_action_value.has_value() && browser_action_value->is_object()) {
        manifest.m_browser_action = ExtensionAction {};
        TRY(manifest.parse_action(browser_action_value->as_object(), *manifest.m_browser_action));
    }
    
    // Parse page_action
    auto page_action_value = manifest_json.get("page_action"sv);
    if (page_action_value.has_value() && page_action_value->is_object()) {
        manifest.m_page_action = ExtensionAction {};
        TRY(manifest.parse_action(page_action_value->as_object(), *manifest.m_page_action));
    }
    
    // Parse web_accessible_resources (optional)
    auto war_value = manifest_json.get("web_accessible_resources"sv);
    if (war_value.has_value()) {
        TRY(manifest.parse_web_accessible_resources(*war_value));
    }
    
    // Parse icons (optional)
    auto icons_value = manifest_json.get("icons"sv);
    if (icons_value.has_value() && icons_value->is_object()) {
        TRY(manifest.parse_icons(icons_value->as_object()));
    }
    
    // Parse content_security_policy (optional)
    auto csp_value = manifest_json.get("content_security_policy"sv);
    if (csp_value.has_value() && csp_value->is_string()) {
        manifest.m_content_security_policy = csp_value->as_string();
    }
    
    // Validate the manifest
    manifest.m_is_valid = manifest.validate_mozilla_manifest();
    
    return manifest;
}

ErrorOr<void> ExtensionManifest::parse_mozilla_applications(JsonObject const& applications_object)
{
    auto gecko_value = applications_object.get("gecko"sv);
    if (gecko_value.has_value() && gecko_value->is_object()) {
        auto const& gecko_obj = gecko_value->as_object();
        
        auto id_value = gecko_obj.get("id"sv);
        if (id_value.has_value() && id_value->is_string()) {
            m_gecko_id = id_value->as_string();
            // Use gecko ID as the main extension ID
            m_id = *m_gecko_id;
        }
        
        auto strict_min_version_value = gecko_obj.get("strict_min_version"sv);
        if (strict_min_version_value.has_value() && strict_min_version_value->is_string()) {
            m_strict_min_version = strict_min_version_value->as_string();
        }
        
        auto strict_max_version_value = gecko_obj.get("strict_max_version"sv);
        if (strict_max_version_value.has_value() && strict_max_version_value->is_string()) {
            m_strict_max_version = strict_max_version_value->as_string();
        }
    }
    
    return {};
}

ErrorOr<void> ExtensionManifest::parse_mozilla_browser_specific_settings(JsonObject const& browser_settings)
{
    // This is the newer name for the applications field in newer Mozilla manifest
    return parse_mozilla_applications(browser_settings);
}

bool ExtensionManifest::validate_chrome_manifest()
{
    return validate(); // Use existing validation for Chrome
}

bool ExtensionManifest::validate_mozilla_manifest()
{
    // Basic validation first
    if (!validate()) {
        return false;
    }
    
    // Mozilla-specific validation
    if (m_platform == ExtensionPlatform::Mozilla) {
        // Mozilla extensions should have a gecko ID for distribution
        if (!m_gecko_id.has_value() || m_gecko_id->is_empty()) {
            // For development, this might be okay, but warn
            dbgln("Mozilla extension missing gecko ID - this may cause issues with distribution");
        }
        
        // Validate gecko ID format if present
        if (m_gecko_id.has_value()) {
            auto const& id = *m_gecko_id;
            // Gecko IDs can be email-like or UUID-like
            if (!id.contains('@') && !id.contains('-')) {
                m_validation_error = "Mozilla extension ID should be in email or UUID format"_string;
                return false;
            }
        }
    }
    
    return true;
}

} 
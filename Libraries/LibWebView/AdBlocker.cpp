/*
 * Copyright (c) 2024, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/ScopeGuard.h>
#include <AK/StringBuilder.h>
#include <AK/StringUtils.h>
#include <LibCore/Resource.h>
#include <LibWebView/AdBlocker.h>

#ifndef ADBLOCK_DEBUG
#    define ADBLOCK_DEBUG 0
#endif

namespace WebView {

AdBlocker* s_the = nullptr;

AdBlocker& AdBlocker::the()
{
    if (!s_the)
        s_the = new AdBlocker;
    return *s_the;
}

AdBlocker::AdBlocker() = default;
AdBlocker::~AdBlocker() = default;

ErrorOr<void> AdBlocker::load_filter_list(StringView name, StringView content)
{
    dbgln_if(ADBLOCK_DEBUG, "AdBlocker: Loading filter list '{}'", name);
    
    auto lines = content.split_view('\n');
    size_t parsed_count = 0;
    size_t error_count = 0;
    
    for (auto const& line : lines) {
        auto trimmed_line = line.trim_whitespace();
        
        // Skip empty lines and comments
        if (trimmed_line.is_empty() || trimmed_line.starts_with('!'))
            continue;
            
        auto result = parse_filter_line(trimmed_line);
        if (result.is_error()) {
            dbgln_if(ADBLOCK_DEBUG, "AdBlocker: Failed to parse filter line: {}", trimmed_line);
            error_count++;
        } else {
            parsed_count++;
        }
    }
    
    dbgln("AdBlocker: Loaded {} filters from '{}' ({} errors)", parsed_count, name, error_count);
    return {};
}

ErrorOr<void> AdBlocker::load_default_filter_lists()
{
    dbgln("AdBlocker: Loading default filter lists");
    
    // For now, load a basic set of ad blocking rules
    // In a real implementation, these would be downloaded from the internet
    // Be conservative to avoid breaking sites like YouTube
    String basic_filters = "||doubleclick.net/gampad/^\n"
                          "||googleadservices.com/pagead/^\n"
                          "||googlesyndication.com/pagead/^\n"
                          "||amazon-adsystem.com/aax2/^\n"
                          "||facebook.com/tr^\n"
                          "||twitter.com/i/analytics^\n"
                          "##.ad:not(.youtube-ad)\n"
                          "##.ads:not(.content-ads)\n"
                          "##.advertisement:not(.site-content)\n"
                          "##.advert:not(.article-advert)\n"
                          "##div[id*=\"google_ads\"]:not([id*=\"youtube\"])\n"
                          "##div[class*=\"banner\"]:not(.site-banner)\n"_string;

    TRY(load_filter_list("Basic AdBlock"sv, basic_filters));
    return {};
}

void AdBlocker::clear_filter_lists()
{
    m_network_filters.clear();
    m_cosmetic_filters.clear();
    m_scriptlet_filters.clear();
    reset_statistics();
}

bool AdBlocker::should_block_request(URL::URL const& url, RequestType type, StringView origin_domain) const
{
    if (!m_enabled)
        return false;
        
    auto url_string = url.to_byte_string();
    bool is_third_party = !origin_domain.is_empty() && is_third_party_request(url_string, origin_domain);
    
    // Check for exceptions first (@@rules)
    for (auto const& filter : m_network_filters) {
        if (!filter.is_exception)
            continue;
            
        if (!filter.matches_request_type(type))
            continue;
            
        // Check third party option
        if ((filter.options & static_cast<u32>(FilterOption::ThirdParty)) && !is_third_party)
            continue;
        if ((filter.options & static_cast<u32>(FilterOption::FirstParty)) && is_third_party)
            continue;
            
        if (!filter.matches_domain(origin_domain, url.serialized_host().to_byte_string()))
            continue;
            
        if (filter.matches_url(url_string)) {
            dbgln_if(ADBLOCK_DEBUG, "AdBlocker: Exception rule matched for {}", url_string);
            return false; // Exception rule matched, don't block
        }
    }
    
    // Check for blocking rules
    for (auto const& filter : m_network_filters) {
        if (filter.is_exception)
            continue;
            
        if (!filter.matches_request_type(type))
            continue;
            
        // Check third party option
        if ((filter.options & static_cast<u32>(FilterOption::ThirdParty)) && !is_third_party)
            continue;
        if ((filter.options & static_cast<u32>(FilterOption::FirstParty)) && is_third_party)
            continue;
            
        if (!filter.matches_domain(origin_domain, url.serialized_host().to_byte_string()))
            continue;
            
        if (filter.matches_url(url_string)) {
            dbgln_if(ADBLOCK_DEBUG, "AdBlocker: Blocking rule matched for {}", url_string);
            // Don't modify mutable members in const method - this was causing race conditions
            return true; // Block this request
        }
    }
    
    return false;
}

Optional<String> AdBlocker::get_redirect_resource(URL::URL const& url, RequestType type, StringView origin_domain) const
{
    if (!m_enabled)
        return {};
        
    auto url_string = url.to_byte_string();
    bool is_third_party = !origin_domain.is_empty() && is_third_party_request(url_string, origin_domain);
    
    for (auto const& filter : m_network_filters) {
        if (filter.is_exception || !filter.redirect_resource.has_value())
            continue;
            
        if (!filter.matches_request_type(type))
            continue;
            
        if ((filter.options & static_cast<u32>(FilterOption::ThirdParty)) && !is_third_party)
            continue;
        if ((filter.options & static_cast<u32>(FilterOption::FirstParty)) && is_third_party)
            continue;
            
        if (!filter.matches_domain(origin_domain, url.serialized_host().to_byte_string()))
            continue;
            
        if (filter.matches_url(url_string)) {
            dbgln_if(ADBLOCK_DEBUG, "AdBlocker: Redirect rule matched for {} -> {}", url_string, filter.redirect_resource.value());
            return filter.redirect_resource.value();
        }
    }
    
    return {};
}

Vector<String> AdBlocker::get_remove_params(URL::URL const& url, RequestType type, StringView origin_domain) const
{
    Vector<String> remove_params;
    
    if (!m_enabled)
        return remove_params;
        
    auto url_string = url.to_byte_string();
    bool is_third_party = !origin_domain.is_empty() && is_third_party_request(url_string, origin_domain);
    
    for (auto const& filter : m_network_filters) {
        if (filter.is_exception || filter.remove_params.is_empty())
            continue;
            
        if (!filter.matches_request_type(type))
            continue;
            
        if ((filter.options & static_cast<u32>(FilterOption::ThirdParty)) && !is_third_party)
            continue;
        if ((filter.options & static_cast<u32>(FilterOption::FirstParty)) && is_third_party)
            continue;
            
        if (!filter.matches_domain(origin_domain, url.serialized_host().to_byte_string()))
            continue;
            
        if (filter.matches_url(url_string)) {
            for (auto const& param : filter.remove_params) {
                if (!remove_params.contains_slow(param))
                    remove_params.append(param);
            }
        }
    }
    
    return remove_params;
}

Vector<String> AdBlocker::get_cosmetic_filters_for_domain(StringView domain) const
{
    Vector<String> selectors;
    
    if (!m_enabled)
        return selectors;
    
    for (auto const& filter : m_cosmetic_filters) {
        if (filter.is_exception)
            continue;
            
        if (filter.applies_to_domain(domain)) {
            selectors.append(filter.selector);
            // Don't modify mutable members in const method - this was causing race conditions
        }
    }
    
    return selectors;
}

Vector<String> AdBlocker::get_script_filters_for_domain(StringView domain) const
{
    Vector<String> scripts;
    
    if (!m_enabled)
        return scripts;
    
    // Return scriptlet filters for the domain
    for (auto const& [key, script] : m_scriptlet_filters) {
        // Simple domain matching for now
        if (domain.contains(key) || key.contains(domain)) {
            scripts.append(script);
        }
    }
    
    return scripts;
}

void AdBlocker::increment_blocked_request_count()
{
    m_blocked_requests_count++;
}

void AdBlocker::increment_blocked_element_count()
{
    m_blocked_elements_count++;
}

void AdBlocker::reset_statistics()
{
    m_blocked_requests_count = 0;
    m_blocked_elements_count = 0;
}

ErrorOr<void> AdBlocker::parse_filter_line(StringView line)
{
    // Handle cosmetic filters
    if (line.contains("##"sv) || line.contains("#@#"sv) || line.contains("#?#"sv)) {
        auto filter = TRY(parse_cosmetic_filter(line));
        m_cosmetic_filters.append(move(filter));
        return {};
    }
    
    // Handle scriptlet filters
    if (line.contains("#+js("sv) || line.contains("#@+js("sv)) {
        // For now, store as simple key-value pairs
        auto parts = line.split_view('#');
        if (parts.size() >= 2) {
            auto domain = parts[0];
            auto script = parts[1];
            m_scriptlet_filters.set(MUST(String::from_utf8(domain)), MUST(String::from_utf8(script)));
        }
        return {};
    }
    
    // Handle network filters
    auto filter = TRY(parse_network_filter(line));
    m_network_filters.append(move(filter));
    return {};
}

ErrorOr<NetworkFilter> AdBlocker::parse_network_filter(StringView line)
{
    NetworkFilter filter;
    
    auto working_line = line;
    
    // Check if it's an exception rule (starts with @@)
    if (working_line.starts_with("@@"sv)) {
        filter.is_exception = true;
        working_line = working_line.substring_view(2);
    }
    
    // Split on $ to separate options
    auto parts = working_line.split_view('$', SplitBehavior::KeepEmpty);
    filter.pattern = MUST(String::from_utf8(parts[0]));
    
    if (parts.size() > 1) {
        auto options_string = parts[1];
        filter.options = parse_filter_options(options_string);
        
        // Parse domain options
        auto option_parts = options_string.split_view(',');
        for (auto const& option : option_parts) {
            if (option.starts_with("domain="sv)) {
                auto domain_list = option.substring_view(7); // Skip "domain="
                auto domains = domain_list.split_view('|');
                StringBuilder include_domains;
                StringBuilder exclude_domains;
                
                for (auto const& domain : domains) {
                    if (domain.starts_with("~"sv)) {
                        if (!exclude_domains.is_empty())
                            exclude_domains.append(',');
                        exclude_domains.append(domain.substring_view(1));
                    } else {
                        if (!include_domains.is_empty())
                            include_domains.append(',');
                        include_domains.append(domain);
                    }
                }
                
                filter.domains_include = MUST(include_domains.to_string());
                filter.domains_exclude = MUST(exclude_domains.to_string());
            } else if (option.starts_with("redirect="sv)) {
                filter.redirect_resource = MUST(String::from_utf8(option.substring_view(9)));
            } else if (option.starts_with("removeparam="sv)) {
                auto params = option.substring_view(12).split_view('|');
                for (auto const& param : params) {
                    filter.remove_params.append(MUST(String::from_utf8(param)));
                }
            }
        }
    }
    
    // Check if it's a regex pattern
    if (filter.pattern.starts_with("/"sv) && filter.pattern.ends_with("/"sv)) {
        filter.is_regex = true;
        filter.pattern = filter.pattern.substring(1, filter.pattern.length() - 2);
    }
    
    // Check case sensitivity
    filter.is_case_sensitive = (filter.options & static_cast<u32>(FilterOption::MatchCase)) != 0;
    
    return filter;
}

ErrorOr<CosmeticFilter> AdBlocker::parse_cosmetic_filter(StringView line)
{
    CosmeticFilter filter;
    
    auto working_line = line;
    
    // Check if it's an exception rule
    if (working_line.contains("#@#"sv)) {
        filter.is_exception = true;
        auto parts = working_line.split_view("#@#"sv);
        if (parts.size() >= 2) {
            filter.domains_include = MUST(String::from_utf8(parts[0]));
            filter.selector = MUST(String::from_utf8(parts[1]));
        }
    } else if (working_line.contains("##"sv)) {
        auto parts = working_line.split_view("##"sv);
        if (parts.size() >= 2) {
            filter.domains_include = MUST(String::from_utf8(parts[0]));
            filter.selector = MUST(String::from_utf8(parts[1]));
        }
    } else if (working_line.contains("#?#"sv)) {
        // Procedural cosmetic filter
        auto parts = working_line.split_view("#?#"sv);
        if (parts.size() >= 2) {
            filter.domains_include = MUST(String::from_utf8(parts[0]));
            filter.selector = MUST(String::from_utf8(parts[1]));
        }
    }
    
    // Check if it's generic
    filter.is_generic = filter.domains_include.is_empty();
    
    return filter;
}

RequestType AdBlocker::request_type_from_string(StringView type) const
{
    if (type == "document"sv) return RequestType::Document;
    if (type == "subdocument"sv) return RequestType::Subdocument;
    if (type == "stylesheet"sv) return RequestType::Stylesheet;
    if (type == "script"sv) return RequestType::Script;
    if (type == "image"sv) return RequestType::Image;
    if (type == "font"sv) return RequestType::Font;
    if (type == "object"sv) return RequestType::Object;
    if (type == "xmlhttprequest"sv || type == "xhr"sv) return RequestType::XMLHttpRequest;
    if (type == "ping"sv) return RequestType::Ping;
    if (type == "csp"sv) return RequestType::CSP;
    if (type == "media"sv) return RequestType::Media;
    if (type == "websocket"sv) return RequestType::WebSocket;
    return RequestType::Other;
}

u32 AdBlocker::parse_filter_options(StringView options_string) const
{
    u32 options = 0;
    auto option_parts = options_string.split_view(',');
    
    for (auto const& option : option_parts) {
        auto trimmed = option.trim_whitespace();
        
        if (trimmed == "script"sv) options |= static_cast<u32>(FilterOption::Script);
        else if (trimmed == "image"sv) options |= static_cast<u32>(FilterOption::Image);
        else if (trimmed == "stylesheet"sv) options |= static_cast<u32>(FilterOption::Stylesheet);
        else if (trimmed == "object"sv) options |= static_cast<u32>(FilterOption::Object);
        else if (trimmed == "xmlhttprequest"sv || trimmed == "xhr"sv) options |= static_cast<u32>(FilterOption::XMLHttpRequest);
        else if (trimmed == "subdocument"sv) options |= static_cast<u32>(FilterOption::SubDocument);
        else if (trimmed == "document"sv) options |= static_cast<u32>(FilterOption::Document);
        else if (trimmed == "font"sv) options |= static_cast<u32>(FilterOption::Font);
        else if (trimmed == "media"sv) options |= static_cast<u32>(FilterOption::Media);
        else if (trimmed == "websocket"sv) options |= static_cast<u32>(FilterOption::WebSocket);
        else if (trimmed == "ping"sv) options |= static_cast<u32>(FilterOption::Ping);
        else if (trimmed == "csp"sv) options |= static_cast<u32>(FilterOption::CSP);
        else if (trimmed == "third-party"sv || trimmed == "3p"sv) options |= static_cast<u32>(FilterOption::ThirdParty);
        else if (trimmed == "first-party"sv || trimmed == "1p"sv) options |= static_cast<u32>(FilterOption::FirstParty);
        else if (trimmed == "match-case"sv) options |= static_cast<u32>(FilterOption::MatchCase);
        else if (trimmed == "important"sv) options |= static_cast<u32>(FilterOption::Important);
        else if (trimmed == "popup"sv) options |= static_cast<u32>(FilterOption::Popup);
        else if (trimmed == "generichide"sv) options |= static_cast<u32>(FilterOption::GenericHide);
        else if (trimmed == "genericblock"sv) options |= static_cast<u32>(FilterOption::GenericBlock);
        else if (trimmed == "inline-script"sv) options |= static_cast<u32>(FilterOption::InlineScript);
        else if (trimmed == "inline-font"sv) options |= static_cast<u32>(FilterOption::InlineFont);
        else if (trimmed == "badfilter"sv) options |= static_cast<u32>(FilterOption::Badfilter);
    }
    
    return options;
}

bool AdBlocker::is_third_party_request(StringView url, StringView origin_domain) const
{
    // Parse domain from URL
    auto url_obj = URL::URL::create_with_url_or_path(url);
    if (!url_obj.is_valid())
        return false;
        
    auto request_domain = url_obj.serialized_host().to_byte_string();
    
    // Simple domain comparison - can be improved
    return !request_domain.ends_with(origin_domain) && !origin_domain.ends_with(request_domain);
}

// NetworkFilter implementation
bool NetworkFilter::matches_request_type(RequestType type) const
{
    // If no specific type options are set, match all types
    u32 type_mask = static_cast<u32>(FilterOption::Script) | static_cast<u32>(FilterOption::Image) | 
                   static_cast<u32>(FilterOption::Stylesheet) | static_cast<u32>(FilterOption::Object) |
                   static_cast<u32>(FilterOption::XMLHttpRequest) | static_cast<u32>(FilterOption::SubDocument) |
                   static_cast<u32>(FilterOption::Document) | static_cast<u32>(FilterOption::Font) |
                   static_cast<u32>(FilterOption::Media) | static_cast<u32>(FilterOption::WebSocket) |
                   static_cast<u32>(FilterOption::Ping) | static_cast<u32>(FilterOption::CSP);
    
    if ((options & type_mask) == 0)
        return true; // No type restrictions
    
    switch (type) {
    case RequestType::Script:
        return (options & static_cast<u32>(FilterOption::Script)) != 0;
    case RequestType::Image:
        return (options & static_cast<u32>(FilterOption::Image)) != 0;
    case RequestType::Stylesheet:
        return (options & static_cast<u32>(FilterOption::Stylesheet)) != 0;
    case RequestType::Object:
        return (options & static_cast<u32>(FilterOption::Object)) != 0;
    case RequestType::XMLHttpRequest:
        return (options & static_cast<u32>(FilterOption::XMLHttpRequest)) != 0;
    case RequestType::Subdocument:
        return (options & static_cast<u32>(FilterOption::SubDocument)) != 0;
    case RequestType::Document:
        return (options & static_cast<u32>(FilterOption::Document)) != 0;
    case RequestType::Font:
        return (options & static_cast<u32>(FilterOption::Font)) != 0;
    case RequestType::Media:
        return (options & static_cast<u32>(FilterOption::Media)) != 0;
    case RequestType::WebSocket:
        return (options & static_cast<u32>(FilterOption::WebSocket)) != 0;
    case RequestType::Ping:
        return (options & static_cast<u32>(FilterOption::Ping)) != 0;
    case RequestType::CSP:
        return (options & static_cast<u32>(FilterOption::CSP)) != 0;
    default:
        return (options & type_mask) == 0; // Match if no specific types set
    }
}

bool NetworkFilter::matches_domain(StringView domain, StringView request_domain) const
{
    // Check excluded domains first
    if (!domains_exclude.is_empty()) {
        auto excluded = domains_exclude.split_view(',');
        for (auto const& excluded_domain : excluded) {
            if (domain.ends_with(excluded_domain.trim_whitespace()) || 
                request_domain.ends_with(excluded_domain.trim_whitespace())) {
                return false;
            }
        }
    }
    
    // Check included domains
    if (!domains_include.is_empty()) {
        auto included = domains_include.split_view(',');
        for (auto const& included_domain : included) {
            auto trimmed = included_domain.trim_whitespace();
            if (domain.ends_with(trimmed) || request_domain.ends_with(trimmed)) {
                return true;
            }
        }
        return false; // Had include list but no match
    }
    
    return true; // No domain restrictions
}

bool NetworkFilter::matches_url(StringView url) const
{
    if (is_regex) {
        // TODO: Implement regex matching
        return false;
    }
    
    auto search_url = is_case_sensitive ? url : url.to_lowercase();
    auto search_pattern = is_case_sensitive ? pattern.bytes_as_string_view() : pattern.to_lowercase();
    
    // Handle wildcards and anchors
    if (pattern.starts_with("||"sv)) {
        // Domain anchor
        auto domain_pattern = pattern.substring_view(2);
        auto url_obj = URL::URL::create_with_url_or_path(url);
        if (url_obj.is_valid()) {
            auto host = url_obj.serialized_host().to_byte_string();
            return host.contains(domain_pattern) || url.contains(domain_pattern);
        }
    } else if (pattern.starts_with("|"sv) && pattern.ends_with("|"sv)) {
        // Exact match
        auto exact_pattern = pattern.substring_view(1, pattern.length() - 2);
        return search_url == exact_pattern;
    } else if (pattern.starts_with("|"sv)) {
        // Start anchor
        auto start_pattern = pattern.substring_view(1);
        return search_url.starts_with(start_pattern);
    } else if (pattern.ends_with("|"sv)) {
        // End anchor
        auto end_pattern = pattern.substring_view(0, pattern.length() - 1);
        return search_url.ends_with(end_pattern);
    } else {
        // Simple substring match with wildcard support
        if (pattern.contains("*"sv)) {
            // TODO: Implement proper wildcard matching
            auto parts = pattern.split_view('*');
            size_t pos = 0;
            for (auto const& part : parts) {
                if (part.is_empty())
                    continue;
                auto found = search_url.find(part, pos);
                if (!found.has_value())
                    return false;
                pos = found.value() + part.length();
            }
            return true;
        } else {
            return search_url.contains(search_pattern);
        }
    }
    
    return false;
}

// CosmeticFilter implementation
bool CosmeticFilter::applies_to_domain(StringView domain) const
{
    // Check excluded domains first
    if (!domains_exclude.is_empty()) {
        auto excluded = domains_exclude.split_view(',');
        for (auto const& excluded_domain : excluded) {
            if (domain.ends_with(excluded_domain.trim_whitespace())) {
                return false;
            }
        }
    }
    
    // Check included domains
    if (!domains_include.is_empty()) {
        auto included = domains_include.split_view(',');
        for (auto const& included_domain : included) {
            auto trimmed = included_domain.trim_whitespace();
            if (domain.ends_with(trimmed)) {
                return true;
            }
        }
        return false; // Had include list but no match
    }
    
    return is_generic; // Apply generic filters to all domains
}

} 
// SPDX-License-Identifier: BSD-2-Clause

#include <AK/Debug.h>
#include <AK/Vector.h>
#include <AK/StringView.h>
#include <LibWeb/Loader/ContentFilter.h>
#include <LibURL/URL.h>

namespace Web {

static ContentFilter* s_the;

ContentFilter& ContentFilter::the()
{
    if (!s_the)
        s_the = new ContentFilter;
    return *s_the;
}

ContentFilter::ContentFilter() = default;
ContentFilter::~ContentFilter() = default;

bool ContentFilter::is_filtered(URL::URL const& url) const
{
    return should_block_request(url, RequestType::Other, ""sv);
}

bool ContentFilter::should_block_request(URL::URL const& url, RequestType type, StringView origin_domain) const
{
    if (!m_filtering_enabled)
        return false;
    
    // Optimize filters if not already done
    if (!m_filters_optimized) {
        const_cast<ContentFilter*>(this)->optimize_filters();
    }
    
    auto url_string = url.serialize();
    
    // Check cache first for performance
    if (auto cached_result = check_url_cache(url_string); cached_result.has_value()) {
        return cached_result.value();
    }
    
    (void)origin_domain; // Unused for now
    
    // Extract domain for optimized lookup
    auto domain = url.serialized_host();
    
    bool should_block = false;
    
    // Check domain-specific filters first (much faster)
    if (auto domain_filters_iter = m_domain_filter_map.find(domain); domain_filters_iter != m_domain_filter_map.end()) {
        auto const& domain_filters = domain_filters_iter->value;
        for (auto filter_idx : domain_filters) {
            auto const& filter = m_network_filters[filter_idx];
            if (filter.matches_url(url_string) && filter.matches_request_type(type)) {
                should_block = !filter.is_exception;
                goto cache_and_return;
            }
        }
    }
    
    // Check generic filters (apply to all domains)
    for (auto filter_idx : m_generic_filter_indices) {
        auto const& filter = m_network_filters[filter_idx];
        if (filter.matches_url(url_string) && filter.matches_request_type(type)) {
            should_block = !filter.is_exception;
            goto cache_and_return;
        }
    }

cache_and_return:
    // Cache the result for future lookups
    cache_url_result(url_string, should_block);
    return should_block;
}

Vector<String> ContentFilter::get_cosmetic_filters_for_domain(StringView domain) const
{
    if (!m_filtering_enabled)
        return {};
    
    // Check cache first
    if (auto cached_result = check_domain_cache(domain); cached_result.has_value()) {
        // If cached as "no filters", return empty
        if (!cached_result.value())
            return {};
    }
    
    Vector<String> matching_filters;
    
    for (auto const& filter : m_cosmetic_filters) {
        if (filter.applies_to_domain(domain)) {
            matching_filters.append(filter.selector);
        }
    }
    
    // Cache whether this domain has any cosmetic filters
    cache_domain_result(domain, !matching_filters.is_empty());
    
    return matching_filters;
}

void ContentFilter::increment_blocked_request_count()
{
    m_blocked_requests_count++;
}

void ContentFilter::increment_blocked_element_count()
{
    m_blocked_elements_count++;
}

void ContentFilter::clear_caches()
{
    m_url_cache.clear();
    m_domain_cache.clear();
}

void ContentFilter::optimize_filters()
{
    if (m_filters_optimized)
        return;
    
    dbgln("ContentFilter: Optimizing {} network filters for performance", m_network_filters.size());
    
    // Clear existing optimization data
    m_domain_filter_map.clear();
    m_generic_filter_indices.clear();
    
    // Preprocess all network filters
    for (size_t i = 0; i < m_network_filters.size(); ++i) {
        auto& filter = m_network_filters[i];
        filter.preprocess_pattern();
        
        if (filter.is_domain_filter && filter.cached_domain_pattern.has_value()) {
            // Group domain-specific filters for faster lookup
            auto domain = filter.cached_domain_pattern.value();
            if (!m_domain_filter_map.contains(domain)) {
                m_domain_filter_map.set(domain, Vector<size_t>());
            }
            auto domain_filters_iter = m_domain_filter_map.find(domain);
            domain_filters_iter->value.append(i);
        } else {
            // Generic filters that apply to all domains
            m_generic_filter_indices.append(i);
        }
    }
    
    dbgln("ContentFilter: Optimization complete - {} domain-specific, {} generic filters", 
          m_domain_filter_map.size(), m_generic_filter_indices.size());
    
    m_filters_optimized = true;
}

Optional<bool> ContentFilter::check_url_cache(StringView url) const
{
    if (m_url_cache.size() > MAX_CACHE_SIZE) {
        // Clear cache when it gets too large to prevent memory bloat
        m_url_cache.clear();
        return {};
    }
    
    if (auto it = m_url_cache.find(url); it != m_url_cache.end()) {
        return it->value;
    }
    return {};
}

void ContentFilter::cache_url_result(StringView url, bool blocked) const
{
    if (m_url_cache.size() < MAX_CACHE_SIZE) {
        m_url_cache.set(String::from_utf8(url).release_value_but_fixme_should_propagate_errors(), blocked);
    }
}

Optional<bool> ContentFilter::check_domain_cache(StringView domain) const
{
    if (m_domain_cache.size() > MAX_CACHE_SIZE) {
        m_domain_cache.clear();
        return {};
    }
    
    if (auto it = m_domain_cache.find(domain); it != m_domain_cache.end()) {
        return it->value;
    }
    return {};
}

void ContentFilter::cache_domain_result(StringView domain, bool has_filters) const
{
    if (m_domain_cache.size() < MAX_CACHE_SIZE) {
        m_domain_cache.set(String::from_utf8(domain).release_value_but_fixme_should_propagate_errors(), has_filters);
    }
}

ErrorOr<void> ContentFilter::set_patterns(ReadonlySpan<String> patterns)
{
    m_patterns.clear();
    for (auto const& pattern : patterns) {
        m_patterns.append({ pattern });
    }
    
    // Reset optimization state
    m_filters_optimized = false;
    clear_caches();
    
    return {};
}

ErrorOr<void> ContentFilter::load_filter_list(StringView name, StringView content)
{
    (void)name;
    
    auto lines = content.split_view('\n');
    
    for (auto line : lines) {
        line = line.trim_whitespace();
        
        if (line.is_empty() || line.starts_with('!'))
            continue;
            
        TRY(parse_filter_line(line));
    }
    
    // Reset optimization state when filters change
    m_filters_optimized = false;
    clear_caches();
    
    return {};
}

ErrorOr<void> ContentFilter::parse_filter_line(StringView line)
{
    if (auto hash_pos = line.find("##"sv); hash_pos.has_value()) {
        auto filter = TRY(parse_cosmetic_filter(line));
        m_cosmetic_filters.append(move(filter));
        return {};
    }
    
    auto filter = TRY(parse_network_filter(line));
    m_network_filters.append(move(filter));
    
    return {};
}

ErrorOr<NetworkFilter> ContentFilter::parse_network_filter(StringView line)
{
    NetworkFilter filter;
    
    if (line.starts_with("@@"sv)) {
        filter.is_exception = true;
        line = line.substring_view(2);
    }
    
    filter.pattern = TRY(String::from_utf8(line));
    filter.options = 0;
    
    if (auto dollar_pos = line.find('$'); dollar_pos.has_value()) {
        auto pattern_part = line.substring_view(0, dollar_pos.value());
        auto options_part = line.substring_view(dollar_pos.value() + 1);
        
        filter.pattern = TRY(String::from_utf8(pattern_part));
        filter.options = parse_filter_options(options_part);
    }
    
    return filter;  
}

ErrorOr<CosmeticFilter> ContentFilter::parse_cosmetic_filter(StringView line)
{
    CosmeticFilter filter;
    
    auto hash_pos = line.find("##"sv);
    if (!hash_pos.has_value()) {
        return Error::from_string_literal("Invalid cosmetic filter");
    }
    
    auto domains_part = line.substring_view(0, hash_pos.value());
    auto selector_part = line.substring_view(hash_pos.value() + 2);
    
    filter.selector = TRY(String::from_utf8(selector_part));
    
    if (!domains_part.is_empty()) {
        filter.domains_include = TRY(String::from_utf8(domains_part));
        filter.is_generic = false;
    }
    
    return filter;
}

u32 ContentFilter::parse_filter_options(StringView options_text) const
{
    u32 options = 0;
    auto option_parts = options_text.split_view(',');
    
    for (auto option : option_parts) {
        option = option.trim_whitespace();
        
        if (option == "script"sv) {
            options |= static_cast<u32>(FilterOption::Script);
        } else if (option == "image"sv) {
            options |= static_cast<u32>(FilterOption::Image);
        } else if (option == "stylesheet"sv) {
            options |= static_cast<u32>(FilterOption::Stylesheet);
        } else if (option == "third-party"sv) {
            options |= static_cast<u32>(FilterOption::ThirdParty);
        }
    }
    
    return options;
}

ErrorOr<void> ContentFilter::load_default_adblock_filters()
{
    dbgln("ContentFilter: Loading default ad blocking filters");
    
    auto default_filters = R"(
||doubleclick.net/gampad/
||googleadservices.com/pagead/
||googlesyndication.com/pagead/
||amazon-adsystem.com/aax2/
##.advertisement:not(.youtube-ad)
##.banner-ad:not(.site-banner)
##.popup-ad
##div[id*="google_ads"]:not([id*="youtube"])
)"sv;

    TRY(load_filter_list("default"sv, default_filters));
    return {};
}

RequestType ContentFilter::request_type_from_string(StringView type) const
{
    if (type == "script"sv) return RequestType::Script;
    if (type == "image"sv) return RequestType::Image;
    if (type == "stylesheet"sv) return RequestType::Stylesheet;
    return RequestType::Other;
}

bool ContentFilter::is_third_party_request(StringView url, StringView origin_domain) const
{
    (void)url;
    (void)origin_domain;
    return false;
}

void ContentFilter::reset_statistics()
{
    m_blocked_requests_count = 0;
    m_blocked_elements_count = 0;
}

bool NetworkFilter::matches_request_type(RequestType type) const
{
    if (options == 0)
        return true;
        
    switch (type) {
    case RequestType::Script:
        return options & static_cast<u32>(FilterOption::Script);
    case RequestType::Image:
        return options & static_cast<u32>(FilterOption::Image);
    case RequestType::Stylesheet:
        return options & static_cast<u32>(FilterOption::Stylesheet);
    default:
        return true;
    }
}

bool NetworkFilter::matches_domain(StringView domain, StringView request_domain) const
{
    (void)domain;
    (void)request_domain;
    return true;
}

bool NetworkFilter::matches_url(StringView url) const
{
    // Use cached preprocessing for performance
    if (!cached_domain_pattern.has_value()) {
        preprocess_pattern();
    }
    
    auto pattern_view = pattern.bytes_as_string_view();
    
    if (is_domain_filter) {
        auto domain_pattern = cached_domain_pattern.value().bytes_as_string_view();
        
        // Fast domain matching using cached pattern
        auto domain_pos = url.find(domain_pattern);
        if (!domain_pos.has_value())
            return false;
            
        auto pos = domain_pos.value();
        
        // Check that it's actually a domain boundary
        if (pos > 0) {
            auto prev_char = url[pos - 1];
            if (prev_char != '/' && prev_char != '.')
                return false;
        }
        
        auto end_pos = pos + domain_pattern.length();
        if (end_pos < url.length()) {
            auto next_char = url[end_pos];
            if (next_char != '/' && next_char != ':' && next_char != '?' && next_char != '#')
                return false;
        }
        
        return true;
    }
    
    // For non-domain patterns, use simple substring matching
    return url.contains(pattern_view);
}

bool CosmeticFilter::applies_to_domain(StringView domain) const
{
    if (is_generic)
        return true;
        
    if (domains_include.is_empty())
        return true;
        
    return domains_include.bytes_as_string_view().contains(domain);
}

void NetworkFilter::preprocess_pattern() const
{
    if (cached_domain_pattern.has_value())
        return; // Already preprocessed
    
    auto pattern_view = pattern.bytes_as_string_view();
    
    if (pattern_view.starts_with("||"sv)) {
        is_domain_filter = true;
        auto domain_pattern = pattern_view.substring_view(2);
        
        // Remove trailing ^ if present
        if (domain_pattern.ends_with("^"sv)) {
            domain_pattern = domain_pattern.substring_view(0, domain_pattern.length() - 1);
        }
        
        // Extract just the domain part (before any path)
        auto slash_pos = domain_pattern.find('/');
        if (slash_pos.has_value()) {
            domain_pattern = domain_pattern.substring_view(0, slash_pos.value());
        }
        
        cached_domain_pattern = String::from_utf8(domain_pattern).release_value_but_fixme_should_propagate_errors();
    } else {
        is_domain_filter = false;
    }
}

} 
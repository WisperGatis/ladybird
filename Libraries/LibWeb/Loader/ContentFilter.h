/*
 * Copyright (c) 2021, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/HashTable.h>
#include <LibURL/URL.h>

namespace Web {

enum class RequestType {
    Document,
    Subdocument,
    Stylesheet,
    Script,
    Image,
    Font,
    Object,
    XMLHttpRequest,
    Ping,
    CSP,
    Media,
    WebSocket,
    Other
};

enum class FilterOption {
    None = 0,
    Script = 1 << 0,
    Image = 1 << 1,
    Stylesheet = 1 << 2,
    Object = 1 << 3,
    XMLHttpRequest = 1 << 4,
    SubDocument = 1 << 5,
    Document = 1 << 6,
    Font = 1 << 7,
    Media = 1 << 8,
    WebSocket = 1 << 9,
    Ping = 1 << 10,
    CSP = 1 << 11,
    ThirdParty = 1 << 12,
    MatchCase = 1 << 13,
    Important = 1 << 14,
    Popup = 1 << 15,
    GenericHide = 1 << 16,
    GenericBlock = 1 << 17,
    InlineScript = 1 << 18,
    InlineFont = 1 << 19,
    Badfilter = 1 << 20,
    Redirect = 1 << 21,
    RedirectRule = 1 << 22,
    RemoveParam = 1 << 23,
    Header = 1 << 24,
    FirstParty = 1 << 25
};

struct NetworkFilter {
    String pattern;
    String domains_include;
    String domains_exclude;
    u32 options { 0 };
    bool is_exception { false };
    bool is_regex { false };
    bool is_case_sensitive { false };
    Optional<String> redirect_resource;
    Vector<String> remove_params;
    
    // Performance optimization: cache processed patterns
    mutable Optional<String> cached_domain_pattern;
    mutable bool is_domain_filter { false };
    
    bool matches_request_type(RequestType type) const;
    bool matches_domain(StringView domain, StringView request_domain) const;
    bool matches_url(StringView url) const;
    void preprocess_pattern() const; // Cache domain pattern for faster lookup
};

struct CosmeticFilter {
    String selector;
    String domains_include;
    String domains_exclude;
    bool is_exception { false };
    bool is_generic { true };
    
    bool applies_to_domain(StringView domain) const;
};

class ContentFilter {
public:
    static ContentFilter& the();

    bool filtering_enabled() const { return m_filtering_enabled; }
    void set_filtering_enabled(bool const enabled) { 
        m_filtering_enabled = enabled; 
        if (!enabled) {
            // Clear caches when disabling to save memory
            m_url_cache.clear();
            m_domain_cache.clear();
        }
    }

    bool is_filtered(const URL::URL&) const;
    bool should_block_request(URL::URL const& url, RequestType type, StringView origin_domain = {}) const;
    
    ErrorOr<void> set_patterns(ReadonlySpan<String>);
    ErrorOr<void> load_filter_list(StringView name, StringView content);
    ErrorOr<void> load_default_adblock_filters();
    
    // Cosmetic filtering
    Vector<String> get_cosmetic_filters_for_domain(StringView domain) const;
    
    // Statistics - thread-safe methods
    u64 blocked_requests_count() const { return m_blocked_requests_count; }
    u64 blocked_elements_count() const { return m_blocked_elements_count; }
    void increment_blocked_request_count();
    void increment_blocked_element_count();
    void reset_statistics();
    
    // Performance optimization methods
    void clear_caches();
    void optimize_filters(); // Pre-process filters for faster matching

private:
    ContentFilter();
    ~ContentFilter();

    ErrorOr<void> parse_filter_line(StringView line);
    ErrorOr<NetworkFilter> parse_network_filter(StringView line);
    ErrorOr<CosmeticFilter> parse_cosmetic_filter(StringView line);
    
    RequestType request_type_from_string(StringView type) const;
    u32 parse_filter_options(StringView options_string) const;
    bool is_third_party_request(StringView url, StringView origin_domain) const;
    
    // Fast path caching
    Optional<bool> check_url_cache(StringView url) const;
    void cache_url_result(StringView url, bool blocked) const;
    Optional<bool> check_domain_cache(StringView domain) const;
    void cache_domain_result(StringView domain, bool has_filters) const;

    struct Pattern {
        String text;
    };
    Vector<Pattern> m_patterns;
    Vector<NetworkFilter> m_network_filters;
    Vector<CosmeticFilter> m_cosmetic_filters;
    HashMap<String, String> m_scriptlet_filters;
    
    // Performance optimization: domain-based filter grouping
    HashMap<String, Vector<size_t>> m_domain_filter_map; // domain -> filter indices
    Vector<size_t> m_generic_filter_indices; // indices of filters that apply to all domains
    
    // Caching for frequently checked URLs and domains
    mutable HashMap<String, bool> m_url_cache;
    mutable HashMap<String, bool> m_domain_cache;
    static constexpr size_t MAX_CACHE_SIZE = 1000; // Limit cache size to prevent memory bloat
    
    bool m_filtering_enabled { true };
    u64 m_blocked_requests_count { 0 };
    u64 m_blocked_elements_count { 0 };
    mutable bool m_filters_optimized { false };
};

}

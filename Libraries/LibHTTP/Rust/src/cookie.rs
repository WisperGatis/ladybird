/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Set-Cookie string parsing (RFC 6265bis, 5.6), shared with HTTP::Cookie::parse_cookie in
// Libraries/LibHTTP/Cookie/ParsedCookie.cpp. Expires, Max-Age and Path need the clock or the request URL, so their
// values are handed back to C++ unparsed.

use std::ffi::c_void;
use std::ops::Range;
use std::ptr;

use crate::{bytes_from_raw, is_tab_or_space};

const MAX_NAME_AND_VALUE_LENGTH: usize = 4096;
const MAX_ATTRIBUTE_VALUE_LENGTH: usize = 1024;

// NB: Matches HTTP::Cookie::SameSite in Libraries/LibHTTP/Cookie/Cookie.h.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
enum SameSite {
    Default = 0,
    None = 1,
    Strict = 2,
    Lax = 3,
}

// NB: Must match the attribute kinds in Libraries/LibHTTP/Cookie/ParsedCookie.cpp.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
enum AttributeKind {
    Expires = 0,
    MaxAge = 1,
    Domain = 2,
    Path = 3,
}

#[derive(Debug, PartialEq, Eq)]
struct Attribute {
    kind: AttributeKind,
    value: Vec<u8>,
}

#[derive(Debug, PartialEq, Eq)]
struct CookieParts {
    name: Range<usize>,
    value: Range<usize>,
    same_site: SameSite,
    secure: bool,
    http_only: bool,
    attributes: Vec<Attribute>,
}

// Matches the CTL characters excluding HTAB: %x00-08 / %x0A-1F / %x7F.
fn contains_invalid_control_character(input: &[u8]) -> bool {
    input.iter().any(|&byte| byte <= 0x08 || (0x0a..=0x1f).contains(&byte) || byte == 0x7f)
}

// Removes leading and trailing WSP from the range, returning the trimmed range within `input`.
fn trim_range(input: &[u8], range: Range<usize>) -> Range<usize> {
    let mut start = range.start;
    let mut end = range.end;
    while start < end && is_tab_or_space(input[start]) {
        start += 1;
    }
    while end > start && is_tab_or_space(input[end - 1]) {
        end -= 1;
    }
    start..end
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.3
fn normalize_domain(value: &[u8]) -> Option<Vec<u8>> {
    // The domain-value is a subdomain as defined by RFC 1034, so it must be ASCII.
    if !value.is_ascii() {
        return None;
    }

    // A leading "." is removed, but a lone "." is kept. See the note in ParsedCookie.cpp.
    let domain = if value.len() > 1 && value[0] == b'.' { &value[1..] } else { value };
    Some(domain.to_ascii_lowercase())
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.7
fn same_site_from_string(value: &[u8]) -> SameSite {
    if value.eq_ignore_ascii_case(b"None") {
        SameSite::None
    } else if value.eq_ignore_ascii_case(b"Strict") {
        SameSite::Strict
    } else if value.eq_ignore_ascii_case(b"Lax") {
        SameSite::Lax
    } else {
        SameSite::Default
    }
}

// Returns None when the Set-Cookie string must be ignored entirely.
fn parse_cookie_string(input: &[u8]) -> Option<CookieParts> {
    // 1. If the set-cookie-string contains a CTL character (excluding HTAB), abort and ignore the string.
    if contains_invalid_control_character(input) {
        return None;
    }

    // 2. The name-value-pair runs up to the first ";", and the unparsed-attributes start at that ";".
    let pair_end = input.iter().position(|&byte| byte == b';').unwrap_or(input.len());

    // 3. A pair without "=" has an empty name, and the whole pair is the value.
    // 4. Remove leading and trailing WSP from the name and value.
    let (name, value) = match input[..pair_end].iter().position(|&byte| byte == b'=') {
        Some(equals) => (trim_range(input, 0..equals), trim_range(input, equals + 1..pair_end)),
        None => (0..0, trim_range(input, 0..pair_end)),
    };

    // 5. If the name and value together are more than 4096 octets, abort and ignore the string.
    if name.len() + value.len() > MAX_NAME_AND_VALUE_LENGTH {
        return None;
    }

    let mut parts = CookieParts {
        name,
        value,
        same_site: SameSite::Default,
        secure: false,
        http_only: false,
        attributes: Vec::new(),
    };

    // 8. Each cookie-av runs from after a ";" up to the next ";" or the end of the input.
    let mut cursor = pair_end;
    while cursor < input.len() {
        let attribute_start = cursor + 1;
        let attribute_end = input[attribute_start..]
            .iter()
            .position(|&byte| byte == b';')
            .map_or(input.len(), |offset| attribute_start + offset);
        cursor = attribute_end;

        // 4-5. Split the cookie-av at the first "=" and remove leading and trailing WSP from both halves.
        let (name, value) = match input[attribute_start..attribute_end].iter().position(|&byte| byte == b'=') {
            Some(equals) => (
                trim_range(input, attribute_start..attribute_start + equals),
                trim_range(input, attribute_start + equals + 1..attribute_end),
            ),
            None => (trim_range(input, attribute_start..attribute_end), attribute_end..attribute_end),
        };

        // 6. If the attribute-value is longer than 1024 octets, ignore the cookie-av.
        if value.len() > MAX_ATTRIBUTE_VALUE_LENGTH {
            continue;
        }

        // 7. Process the attribute. Unrecognized names are ignored.
        let attribute_name = &input[name];
        let attribute_value = &input[value];

        if attribute_name.eq_ignore_ascii_case(b"Expires") {
            parts.attributes.push(Attribute { kind: AttributeKind::Expires, value: attribute_value.to_vec() });
        } else if attribute_name.eq_ignore_ascii_case(b"Max-Age") {
            parts.attributes.push(Attribute { kind: AttributeKind::MaxAge, value: attribute_value.to_vec() });
        } else if attribute_name.eq_ignore_ascii_case(b"Domain") {
            // A non-ASCII domain makes the whole Set-Cookie string invalid.
            parts.attributes.push(Attribute { kind: AttributeKind::Domain, value: normalize_domain(attribute_value)? });
        } else if attribute_name.eq_ignore_ascii_case(b"Path") {
            parts.attributes.push(Attribute { kind: AttributeKind::Path, value: attribute_value.to_vec() });
        } else if attribute_name.eq_ignore_ascii_case(b"Secure") {
            parts.secure = true;
        } else if attribute_name.eq_ignore_ascii_case(b"HttpOnly") {
            parts.http_only = true;
        } else if attribute_name.eq_ignore_ascii_case(b"SameSite") {
            // Each SameSite attribute overrides the previous one, even when its value is unrecognized.
            parts.same_site = same_site_from_string(attribute_value);
        }
    }

    Some(parts)
}

#[repr(C)]
pub struct LadybirdHttpCookieParts {
    pub name_offset: usize,
    pub name_length: usize,
    pub value_offset: usize,
    pub value_length: usize,
    pub same_site: u8,
    pub secure: bool,
    pub http_only: bool,
}

/// Returns true when the bytes contain a CTL character other than HTAB.
///
/// # Safety
/// `value` must be valid for reads of `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_contains_invalid_cookie_control_character(value: *const u8, length: usize) -> bool {
    match unsafe { bytes_from_raw(value, length) } {
        Some(bytes) => contains_invalid_control_character(bytes),
        None => false,
    }
}

/// Lowercases a Domain attribute value, removing a single leading ".". Returns false when the value is not ASCII.
///
/// The normalized value is written to `normalized`, which must have room for `length` bytes.
///
/// # Safety
/// `value` must be valid for reads of `length` bytes, `normalized` must be valid for writes of `length` bytes, and
/// `normalized_length` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_normalize_cookie_domain(
    value: *const u8,
    length: usize,
    normalized: *mut u8,
    normalized_length: *mut usize,
) -> bool {
    if normalized_length.is_null() {
        return false;
    }
    let Some(value) = (unsafe { bytes_from_raw(value, length) }) else {
        return false;
    };
    let Some(domain) = normalize_domain(value) else {
        return false;
    };

    if !domain.is_empty() {
        if normalized.is_null() {
            return false;
        }
        unsafe { ptr::copy_nonoverlapping(domain.as_ptr(), normalized, domain.len()) };
    }
    unsafe { ptr::write(normalized_length, domain.len()) };
    true
}

/// Parses a Set-Cookie string. Returns false when the string must be ignored.
///
/// Otherwise writes the locations of the name and value within `input`, along with the SameSite, Secure and HttpOnly
/// state, to `parts`. Then calls `on_attribute` once for each Expires (kind 0), Max-Age (1), Domain (2, lowercased)
/// and Path (3) attribute, in the order they appear. The value bytes are only valid during the call.
///
/// # Safety
/// `input` must be valid for reads of `length` bytes, `parts` must be valid for writes, and `on_attribute` must be
/// safe to call with `context`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_parse_cookie_string(
    input: *const u8,
    length: usize,
    context: *mut c_void,
    on_attribute: unsafe extern "C" fn(*mut c_void, u8, *const u8, usize),
    parts: *mut LadybirdHttpCookieParts,
) -> bool {
    if parts.is_null() {
        return false;
    }
    let Some(input) = (unsafe { bytes_from_raw(input, length) }) else {
        return false;
    };
    let Some(cookie) = parse_cookie_string(input) else {
        return false;
    };

    for attribute in &cookie.attributes {
        unsafe { on_attribute(context, attribute.kind as u8, attribute.value.as_ptr(), attribute.value.len()) };
    }

    unsafe {
        ptr::write(parts, LadybirdHttpCookieParts {
            name_offset: cookie.name.start,
            name_length: cookie.name.len(),
            value_offset: cookie.value.start,
            value_length: cookie.value.len(),
            same_site: cookie.same_site as u8,
            secure: cookie.secure,
            http_only: cookie.http_only,
        });
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(input: &str) -> Option<CookieParts> {
        parse_cookie_string(input.as_bytes())
    }

    fn text(input: &str, range: Range<usize>) -> &str {
        std::str::from_utf8(&input.as_bytes()[range]).unwrap()
    }

    fn attribute(kind: AttributeKind, value: &str) -> Attribute {
        Attribute { kind, value: value.as_bytes().to_vec() }
    }

    #[test]
    fn name_and_value_are_split_and_trimmed() {
        let input = " name = value ; Secure";
        let parts = parse(input).unwrap();
        assert_eq!(text(input, parts.name.clone()), "name");
        assert_eq!(text(input, parts.value.clone()), "value");
        assert!(parts.secure);
    }

    #[test]
    fn pair_without_equals_has_empty_name() {
        let input = "novalue";
        let parts = parse(input).unwrap();
        assert_eq!(parts.name.len(), 0);
        assert_eq!(text(input, parts.value.clone()), "novalue");
    }

    #[test]
    fn empty_name_with_equals_is_allowed() {
        let input = "=value";
        let parts = parse(input).unwrap();
        assert_eq!(parts.name.len(), 0);
        assert_eq!(text(input, parts.value.clone()), "value");
    }

    #[test]
    fn empty_value_is_allowed() {
        let input = "name=";
        let parts = parse(input).unwrap();
        assert_eq!(text(input, parts.name.clone()), "name");
        assert_eq!(parts.value.len(), 0);
    }

    #[test]
    fn control_characters_are_rejected() {
        assert!(parse("a=b\u{1}").is_none());
        assert!(parse("a=\u{7f}").is_none());
        assert!(parse("a=b\nc").is_none());
        assert!(parse("a=b; Path=/\r").is_none());
    }

    #[test]
    fn tab_is_allowed() {
        let input = "a=\tb";
        let parts = parse(input).unwrap();
        assert_eq!(text(input, parts.value.clone()), "b");
    }

    #[test]
    fn name_and_value_size_is_limited() {
        let within = format!("a={}", "x".repeat(4095));
        assert!(parse(&within).is_some());

        let too_long = format!("a={}", "x".repeat(4096));
        assert!(parse(&too_long).is_none());
    }

    #[test]
    fn attribute_value_size_is_limited() {
        let within = format!("a=b; Path={}", "x".repeat(1024));
        assert_eq!(parse(&within).unwrap().attributes, vec![attribute(AttributeKind::Path, &"x".repeat(1024))]);

        let too_long = format!("a=b; Path={}", "x".repeat(1025));
        assert!(parse(&too_long).unwrap().attributes.is_empty());
    }

    #[test]
    fn attributes_are_reported_in_order() {
        let parts = parse("a=b; Expires=Wed; Max-Age=5; Path=/x; Domain=example.org").unwrap();
        assert_eq!(parts.attributes, vec![
            attribute(AttributeKind::Expires, "Wed"),
            attribute(AttributeKind::MaxAge, "5"),
            attribute(AttributeKind::Path, "/x"),
            attribute(AttributeKind::Domain, "example.org"),
        ]);
    }

    #[test]
    fn attribute_names_are_case_insensitive() {
        let parts = parse("a=b; EXPIRES=x; max-AGE=1; PATH=/; secure; httponly").unwrap();
        assert_eq!(parts.attributes.len(), 3);
        assert!(parts.secure);
        assert!(parts.http_only);
    }

    #[test]
    fn unknown_and_empty_attributes_are_ignored() {
        let parts = parse("a=b;;Foo=bar;Secure;").unwrap();
        assert!(parts.attributes.is_empty());
        assert!(parts.secure);
        assert!(!parts.http_only);
    }

    #[test]
    fn domain_is_lowercased_and_leading_dot_removed() {
        let parts = parse("a=b; Domain=.Example.COM").unwrap();
        assert_eq!(parts.attributes, vec![attribute(AttributeKind::Domain, "example.com")]);

        let parts = parse("a=b; Domain=..x").unwrap();
        assert_eq!(parts.attributes, vec![attribute(AttributeKind::Domain, ".x")]);
    }

    #[test]
    fn lone_dot_domain_is_kept() {
        let parts = parse("a=b; Domain=.").unwrap();
        assert_eq!(parts.attributes, vec![attribute(AttributeKind::Domain, ".")]);
    }

    #[test]
    fn empty_domain_is_kept() {
        let parts = parse("a=b; Domain=").unwrap();
        assert_eq!(parts.attributes, vec![attribute(AttributeKind::Domain, "")]);
    }

    #[test]
    fn non_ascii_domain_rejects_the_cookie() {
        assert!(parse("a=b; Domain=\u{e9}xample.com").is_none());
    }

    #[test]
    fn same_site_values_are_mapped() {
        assert_eq!(parse("a=b; SameSite=strict").unwrap().same_site, SameSite::Strict);
        assert_eq!(parse("a=b; SameSite=LAX").unwrap().same_site, SameSite::Lax);
        assert_eq!(parse("a=b; SameSite=None").unwrap().same_site, SameSite::None);
        assert_eq!(parse("a=b; SameSite=bogus").unwrap().same_site, SameSite::Default);
    }

    #[test]
    fn last_same_site_attribute_wins() {
        assert_eq!(parse("a=b; SameSite=Lax; SameSite=bogus").unwrap().same_site, SameSite::Default);
    }

    #[test]
    fn flags_default_to_absent() {
        let parts = parse("a=b").unwrap();
        assert!(!parts.secure);
        assert!(!parts.http_only);
        assert_eq!(parts.same_site, SameSite::Default);
    }
}

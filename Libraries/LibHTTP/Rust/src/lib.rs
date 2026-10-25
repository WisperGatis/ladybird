/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Strict-Transport-Security header parsing (RFC 6797, 6.1), shared with HTTP::HSTS::parse_header in
// Libraries/LibHTTP/HSTS/ParsedHSTSPolicy.cpp.

#[path = "../../../RustDemangle.rs"]
mod rust_demangle;

#[path = "../../../RustPanic.rs"]
mod rust_panic;

mod cookie;
mod content_range;
mod cookie_date;
mod date;
mod header;

use std::ptr;
use std::slice;

// Largest value AK::Duration::from_seconds can take without overflowing.
const MAX_MAX_AGE_SECONDS: u64 = i64::MAX as u64;

fn is_tab_or_space(byte: u8) -> bool {
    byte == b'\t' || byte == b' '
}

// Views a C buffer as a slice. An empty buffer is valid even when the pointer is null. Returns None for a null
// pointer with a non-zero length.
unsafe fn bytes_from_raw<'a>(data: *const u8, length: usize) -> Option<&'a [u8]> {
    if length == 0 {
        return Some(&[]);
    }
    if data.is_null() {
        return None;
    }
    Some(unsafe { slice::from_raw_parts(data, length) })
}

// RFC 7230, 3.2.6. tchar
fn is_token_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || b"!#$%&'*+-.^_`|~".contains(&byte)
}

fn is_token(bytes: &[u8]) -> bool {
    !bytes.is_empty() && bytes.iter().all(|&b| is_token_byte(b))
}

fn trim_tabs_and_spaces(bytes: &[u8]) -> &[u8] {
    let start = bytes.iter().position(|&b| !is_tab_or_space(b)).unwrap_or(bytes.len());
    let end = bytes.iter().rposition(|&b| !is_tab_or_space(b)).map_or(start, |i| i + 1);
    &bytes[start..end]
}

// RFC 7230, 3.2.6. quoted-string. Returns the unescaped contents and the position after the closing quote.
fn parse_quoted_string(input: &[u8], start: usize) -> Option<(Vec<u8>, usize)> {
    let mut value = Vec::new();
    let mut position = start + 1;
    while position < input.len() {
        let byte = input[position];
        position += 1;
        if byte == b'"' {
            return Some((value, position));
        }
        if byte == b'\\' {
            let escaped = *input.get(position)?;
            position += 1;
            value.push(escaped);
            continue;
        }
        value.push(byte);
    }
    None
}

/// Parses an STS header value. Returns the max-age in seconds (clamped to `MAX_MAX_AGE_SECONDS`) and whether
/// includeSubDomains was asserted, or `None` when the header must be ignored.
fn parse_header(input: &[u8]) -> Option<(u64, bool)> {
    let mut position = 0;
    let mut max_age: Option<u64> = None;
    let mut include_sub_domains = false;

    while position < input.len() {
        while position < input.len() && is_tab_or_space(input[position]) {
            position += 1;
        }
        if position >= input.len() {
            break;
        }

        // Directive names are everything up to '=' or ';'.
        let name_start = position;
        while position < input.len() && input[position] != b'=' && input[position] != b';' {
            position += 1;
        }
        let name = trim_tabs_and_spaces(&input[name_start..position]);
        let has_equals = position < input.len() && input[position] == b'=';

        if name.is_empty() {
            if has_equals {
                return None;
            }
        } else if !is_token(name) {
            return None;
        }

        let mut value: Option<Vec<u8>> = None;
        if has_equals {
            position += 1;
            while position < input.len() && is_tab_or_space(input[position]) {
                position += 1;
            }
            if position < input.len() && input[position] == b'"' {
                let (quoted, after) = parse_quoted_string(input, position)?;
                position = after;
                while position < input.len() && is_tab_or_space(input[position]) {
                    position += 1;
                }
                if position < input.len() && input[position] != b';' {
                    return None;
                }
                value = Some(quoted);
            } else {
                let value_start = position;
                while position < input.len() && input[position] != b';' {
                    position += 1;
                }
                let token = trim_tabs_and_spaces(&input[value_start..position]);
                if !is_token(token) {
                    return None;
                }
                value = Some(token.to_vec());
            }
        }

        if name.eq_ignore_ascii_case(b"max-age") {
            // max-age-value = delta-seconds = 1*DIGIT
            let digits = value?;
            if digits.is_empty() || !digits.iter().all(u8::is_ascii_digit) {
                return None;
            }
            if max_age.is_some() {
                return None;
            }
            // Values too large for u64 saturate, which the clamp below turns into the largest supported age.
            let seconds = digits
                .iter()
                .try_fold(0u64, |acc, &d| acc.checked_mul(10)?.checked_add((d - b'0') as u64))
                .unwrap_or(u64::MAX);
            max_age = Some(seconds);
        } else if name.eq_ignore_ascii_case(b"includeSubDomains") {
            // A valueless directive must not carry a value.
            if value.is_some() {
                return None;
            }
            if include_sub_domains {
                return None;
            }
            include_sub_domains = true;
        }

        if position < input.len() && input[position] == b';' {
            position += 1;
        }
    }

    // The max-age directive is REQUIRED.
    let max_age = max_age?;
    Some((max_age.min(MAX_MAX_AGE_SECONDS), include_sub_domains))
}

/// Parses a Strict-Transport-Security header value.
///
/// Returns false when the header must be ignored. Otherwise writes the clamped max-age in seconds to
/// `max_age_seconds` and whether includeSubDomains was asserted to `include_sub_domains`.
///
/// # Safety
/// `value` must be valid for reads of `length` bytes, and both out-pointers must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_parse_hsts_header(
    value: *const u8,
    length: usize,
    max_age_seconds: *mut u64,
    include_sub_domains: *mut bool,
) -> bool {
    if value.is_null() || max_age_seconds.is_null() || include_sub_domains.is_null() {
        return false;
    }
    let input = unsafe { slice::from_raw_parts(value, length) };
    match parse_header(input) {
        Some((seconds, include)) => {
            unsafe {
                ptr::write(max_age_seconds, seconds);
                ptr::write(include_sub_domains, include);
            }
            true
        }
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<(u64, bool)> {
        parse_header(s.as_bytes())
    }

    #[test]
    fn max_age_only() {
        assert_eq!(parse("max-age=31536000"), Some((31536000, false)));
    }

    #[test]
    fn include_sub_domains_and_quoted_max_age() {
        assert_eq!(parse("max-age=\"60\"; includeSubDomains"), Some((60, true)));
        assert_eq!(parse("includesubdomains; max-age=1"), Some((1, true)));
    }

    #[test]
    fn missing_max_age_is_rejected() {
        assert_eq!(parse("includeSubDomains"), None);
    }

    #[test]
    fn duplicate_directives_are_rejected() {
        assert_eq!(parse("max-age=1; max-age=2"), None);
        assert_eq!(parse("max-age=1; includeSubDomains; includeSubDomains"), None);
    }

    #[test]
    fn value_on_include_sub_domains_is_rejected() {
        assert_eq!(parse("max-age=1; includeSubDomains=yes"), None);
    }

    #[test]
    fn non_digit_max_age_is_rejected() {
        assert_eq!(parse("max-age=1a"), None);
        assert_eq!(parse("max-age="), None);
        assert_eq!(parse("max-age=\"\""), None);
    }

    #[test]
    fn huge_max_age_is_clamped() {
        assert_eq!(parse("max-age=99999999999999999999999999"), Some((MAX_MAX_AGE_SECONDS, false)));
    }

    #[test]
    fn unknown_directives_are_ignored() {
        assert_eq!(parse("max-age=5; preload; foo=bar"), Some((5, false)));
    }

    #[test]
    fn bad_quoted_string_is_rejected() {
        assert_eq!(parse("max-age=\"5"), None);
        assert_eq!(parse("max-age=\"5\"x"), None);
    }

    #[test]
    fn invalid_directive_name_is_rejected() {
        assert_eq!(parse("max age=5"), None);
        assert_eq!(parse("=5; max-age=1"), None);
    }
}

/// Parses an HTTP-date. Returns false when the value is not a valid date.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes, and `seconds` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_parse_http_date(data: *const u8, length: usize, seconds: *mut i64) -> bool {
    if data.is_null() || seconds.is_null() {
        return false;
    }
    let bytes = unsafe { slice::from_raw_parts(data, length) };
    match date::parse_http_date(bytes) {
        Some(value) => {
            unsafe { ptr::write(seconds, value) };
            true
        }
        None => false,
    }
}

/// Parses a cookie-date. Returns false when the value is not a valid cookie date.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes, and `seconds` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_parse_cookie_date(data: *const u8, length: usize, seconds: *mut i64) -> bool {
    if data.is_null() || seconds.is_null() {
        return false;
    }
    let bytes = unsafe { slice::from_raw_parts(data, length) };
    match cookie_date::parse_cookie_date(bytes) {
        Some(value) => {
            unsafe { ptr::write(seconds, value) };
            true
        }
        None => false,
    }
}

/// Parses a single-byte Content-Range value. Returns false when the value is not valid. `has_complete_length` is set
/// to false for "*", in which case `complete_length` is not written.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes. `first`, `last`, `has_complete_length` and `complete_length`
/// must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_parse_content_range(
    data: *const u8,
    length: usize,
    first: *mut u64,
    last: *mut u64,
    has_complete_length: *mut bool,
    complete_length: *mut u64,
) -> bool {
    if data.is_null() || first.is_null() || last.is_null() || has_complete_length.is_null() || complete_length.is_null() {
        return false;
    }
    let bytes = unsafe { slice::from_raw_parts(data, length) };
    match content_range::parse_content_range(bytes) {
        Some(range) => {
            unsafe {
                ptr::write(first, range.first_byte_pos);
                ptr::write(last, range.last_byte_pos);
                match range.complete_length {
                    Some(value) => {
                        ptr::write(has_complete_length, true);
                        ptr::write(complete_length, value);
                    }
                    None => ptr::write(has_complete_length, false),
                }
            }
            true
        }
        None => false,
    }
}

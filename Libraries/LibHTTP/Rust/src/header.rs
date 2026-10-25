/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Header name and value validity checks (Fetch, 2.2), shared with HTTP::is_header_name and HTTP::is_header_value in
// Libraries/LibHTTP/Header.cpp.

use crate::{bytes_from_raw, is_token};

// https://fetch.spec.whatwg.org/#header-value
fn is_header_value(value: &[u8]) -> bool {
    // A header value has no leading or trailing HTTP tab or space bytes, and contains no NUL or HTTP newline bytes.
    if let (Some(&first), Some(&last)) = (value.first(), value.last())
        && (first == b'\t' || first == b' ' || last == b'\t' || last == b' ')
    {
        return false;
    }

    !value.iter().any(|&byte| byte == 0x00 || byte == 0x0a || byte == 0x0d)
}

/// Returns true when the bytes are a header name, which is a token per the Fetch standard.
///
/// # Safety
/// `value` must be valid for reads of `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_is_header_name(value: *const u8, length: usize) -> bool {
    match unsafe { bytes_from_raw(value, length) } {
        Some(name) => is_token(name),
        None => false,
    }
}

/// Returns true when the bytes are a valid header value.
///
/// # Safety
/// `value` must be valid for reads of `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_http_is_header_value(value: *const u8, length: usize) -> bool {
    match unsafe { bytes_from_raw(value, length) } {
        Some(value) => is_header_value(value),
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn header_names_are_tokens() {
        assert!(is_token(b"Content-Type"));
        assert!(is_token(b"X-Custom_Header"));
        assert!(!is_token(b""));
        assert!(!is_token(b"Content Type"));
        assert!(!is_token(b"Content:Type"));
    }

    #[test]
    fn header_values_reject_edge_whitespace_and_newlines() {
        assert!(is_header_value(b""));
        assert!(is_header_value(b"a b"));
        assert!(is_header_value(b"a\tb"));
        assert!(!is_header_value(b" a"));
        assert!(!is_header_value(b"a "));
        assert!(!is_header_value(b"\ta"));
        assert!(!is_header_value(b"a\x00b"));
        assert!(!is_header_value(b"a\nb"));
        assert!(!is_header_value(b"a\rb"));
    }
}

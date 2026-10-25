// Cookie-date parsing (RFC 6265 5.1.1), ported from LibHTTP/Cookie/ParsedCookie.cpp. The result is seconds since the
// Unix epoch in UTC; the C++ side builds the UnixDateTime from it.

use crate::date::days_from_civil;

const SHORT_MONTH_NAMES: [&[u8]; 12] = [b"Jan", b"Feb", b"Mar", b"Apr", b"May", b"Jun", b"Jul", b"Aug", b"Sep", b"Oct", b"Nov", b"Dec"];

// RFC 6265 5.1.1 delimiter: TAB, the space-to-slash range, the colon-to-at range, the bracket-to-backtick range,
// and the brace-to-tilde range.
fn is_delimiter(byte: u8) -> bool {
    byte == 0x09 || (0x20..=0x2f).contains(&byte) || (0x3b..=0x40).contains(&byte) || (0x5b..=0x60).contains(&byte) || (0x7b..=0x7e).contains(&byte)
}

fn all_digits(token: &[u8]) -> Option<u32> {
    if token.is_empty() || !token.iter().all(u8::is_ascii_digit) {
        return None;
    }
    token.iter().try_fold(0u32, |acc, &d| acc.checked_mul(10)?.checked_add((d - b'0') as u32))
}

fn parse_time(token: &[u8]) -> Option<(u32, u32, u32)> {
    let parts: Vec<&[u8]> = token.split(|&b| b == b':').collect();
    if parts.len() != 3 || parts.iter().any(|p| p.is_empty() || p.len() > 2) {
        return None;
    }
    Some((all_digits(parts[0])?, all_digits(parts[1])?, all_digits(parts[2])?))
}

fn parse_day_of_month(token: &[u8]) -> Option<u32> {
    if token.is_empty() || token.len() > 2 {
        return None;
    }
    all_digits(token)
}

fn parse_month(token: &[u8]) -> Option<u32> {
    SHORT_MONTH_NAMES.iter().position(|name| token.eq_ignore_ascii_case(name)).map(|i| i as u32 + 1)
}

fn parse_year(token: &[u8]) -> Option<u32> {
    if token.len() != 2 && token.len() != 4 {
        return None;
    }
    all_digits(token)
}

fn is_leap_year(year: i64) -> bool {
    (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
}

fn days_in_month(year: i64, month: i64) -> i64 {
    match month {
        2 if is_leap_year(year) => 29,
        2 => 28,
        4 | 6 | 9 | 11 => 30,
        _ => 31,
    }
}

/// Parses a cookie-date into seconds since the Unix epoch, or `None` when the value is not a valid cookie date.
pub fn parse_cookie_date(input: &[u8]) -> Option<i64> {
    let mut time: Option<(u32, u32, u32)> = None;
    let mut day_of_month: Option<u32> = None;
    let mut month: Option<u32> = None;
    let mut year: Option<u32> = None;

    // 2. Process each date-token in order. The checks run in the order the spec gives, so a token that matches more
    //    than one production is taken by the first production still unfilled.
    for token in input.split(|&b| is_delimiter(b)) {
        if token.is_empty() {
            continue;
        }
        if time.is_none() {
            if let Some(value) = parse_time(token) {
                time = Some(value);
                continue;
            }
        }
        if day_of_month.is_none() {
            if let Some(value) = parse_day_of_month(token) {
                day_of_month = Some(value);
                continue;
            }
        }
        if month.is_none() {
            if let Some(value) = parse_month(token) {
                month = Some(value);
                continue;
            }
        }
        if year.is_none() {
            if let Some(value) = parse_year(token) {
                year = Some(value);
            }
        }
    }

    let (hour, minute, second) = time?;
    let day_of_month = day_of_month?;
    let month = month?;
    let mut year = year? as i64;

    // 3. Two-digit years: 70 to 99 map to 1970 to 1999, and 0 to 69 map to 2000 to 2069.
    if (70..=99).contains(&year) {
        year += 1900;
    }
    if year <= 69 {
        year += 2000;
    }

    // 5. Range checks.
    if !(1..=31).contains(&day_of_month) || year < 1601 || hour > 23 || minute > 59 || second > 59 {
        return None;
    }
    if day_of_month as i64 > days_in_month(year, month as i64) {
        return None;
    }

    let days = days_from_civil(year, month as i64, 1) + (day_of_month as i64 - 1);
    Some(days * 86400 + hour as i64 * 3600 + minute as i64 * 60 + second as i64)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<i64> {
        parse_cookie_date(s.as_bytes())
    }

    #[test]
    fn rfc_example() {
        // Sun, 06 Nov 1994 08:49:37 GMT
        assert_eq!(parse("Sun, 06 Nov 1994 08:49:37 GMT"), Some(784111777));
    }

    #[test]
    fn two_digit_years() {
        assert_eq!(parse("06 Nov 94 08:49:37"), Some(784111777));
        assert_eq!(parse("06 Nov 70 00:00:00"), Some(26697600));
        assert_eq!(parse("01 Jan 69 00:00:00"), Some(3124224000));
    }

    #[test]
    fn rejects_missing_fields() {
        assert_eq!(parse("06 Nov 1994 08:49"), None);
        assert_eq!(parse("06 Nov 1994"), None);
    }

    #[test]
    fn rejects_out_of_range_values() {
        assert_eq!(parse("32 Nov 1994 08:49:37"), None);
        assert_eq!(parse("31 Feb 1994 08:49:37"), None);
        assert_eq!(parse("06 Nov 1994 24:00:00"), None);
        assert_eq!(parse("06 Nov 1994 08:60:00"), None);
        assert_eq!(parse("06 Nov 1994 08:00:60"), None);
        assert_eq!(parse("06 Nov 1600 08:00:00"), None);
    }

    #[test]
    fn leap_day_is_accepted_in_leap_years_only() {
        assert!(parse("29 Feb 2000 00:00:00").is_some());
        assert_eq!(parse("29 Feb 1900 00:00:00"), None);
    }

    #[test]
    fn rejects_bad_time_components() {
        assert_eq!(parse("06 Nov 1994 8:4:37x"), None);
        assert_eq!(parse("06 Nov 1994 008:49:37"), None);
    }
}

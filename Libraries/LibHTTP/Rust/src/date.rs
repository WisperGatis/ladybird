// HTTP-date parsing for "%a, %d %b %Y %T GMT" (RFC 9110, 5.6.7), matching UnixDateTime::parse(..., from_gmt = true)
// in AK/Time.cpp, including its quirks: signed decimal numbers are accepted, names match case-insensitively, the
// literal "GMT" is case-sensitive, and an instant of exactly -1 is rejected as timegm() reports it.

const SHORT_DAY_NAMES: [&[u8]; 7] = [b"Sun", b"Mon", b"Tue", b"Wed", b"Thu", b"Fri", b"Sat"];
const SHORT_MONTH_NAMES: [&[u8]; 12] = [b"Jan", b"Feb", b"Mar", b"Apr", b"May", b"Jun", b"Jul", b"Aug", b"Sep", b"Oct", b"Nov", b"Dec"];

struct Cursor<'a> {
    data: &'a [u8],
    position: usize,
}

impl Cursor<'_> {
    fn consume_char(&mut self, expected: u8) -> Option<()> {
        if self.data.get(self.position) == Some(&expected) {
            self.position += 1;
            Some(())
        } else {
            None
        }
    }

    fn consume_name(&mut self, names: &[&[u8]]) -> Option<usize> {
        for (index, name) in names.iter().enumerate() {
            let end = self.position + name.len();
            if let Some(candidate) = self.data.get(self.position..end) {
                if candidate.eq_ignore_ascii_case(name) {
                    self.position = end;
                    return Some(index);
                }
            }
        }
        None
    }

    // AK's consume_decimal_integer<int>: an optional sign, then one or more digits, within the range of int.
    fn consume_number(&mut self) -> Option<i64> {
        let start = self.position;
        let negative = match self.data.get(self.position) {
            Some(b'-') => {
                self.position += 1;
                true
            }
            Some(b'+') => {
                self.position += 1;
                false
            }
            _ => false,
        };

        let digits_start = self.position;
        let mut magnitude: i64 = 0;
        while let Some(&digit) = self.data.get(self.position) {
            if !digit.is_ascii_digit() {
                break;
            }
            magnitude = magnitude.checked_mul(10)?.checked_add((digit - b'0') as i64)?;
            if magnitude > (i32::MAX as i64) + 1 {
                return None;
            }
            self.position += 1;
        }
        if self.position == digits_start {
            self.position = start;
            return None;
        }

        let value = if negative { -magnitude } else { magnitude };
        if value < i32::MIN as i64 || value > i32::MAX as i64 {
            return None;
        }
        Some(value)
    }
}

// Days from 1970-01-01 to the given proleptic Gregorian date (month 1..=12, day 1).
pub(crate) fn days_from_civil(year: i64, month: i64, day: i64) -> i64 {
    let year = if month <= 2 { year - 1 } else { year };
    let era = year.div_euclid(400);
    let year_of_era = year - era * 400;
    let month_index = if month > 2 { month - 3 } else { month + 9 };
    let day_of_year = (153 * month_index + 2) / 5 + day - 1;
    let day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    era * 146097 + day_of_era - 719468
}

/// Parses an HTTP-date into seconds since the Unix epoch.
pub fn parse_http_date(input: &[u8]) -> Option<i64> {
    let mut cursor = Cursor { data: input, position: 0 };

    cursor.consume_name(&SHORT_DAY_NAMES)?;
    cursor.consume_char(b',')?;
    cursor.consume_char(b' ')?;
    let day = cursor.consume_number()?;
    cursor.consume_char(b' ')?;
    let month = cursor.consume_name(&SHORT_MONTH_NAMES)? as i64;
    cursor.consume_char(b' ')?;
    let year = cursor.consume_number()?;
    cursor.consume_char(b' ')?;
    let hour = cursor.consume_number()?;
    cursor.consume_char(b':')?;
    let minute = cursor.consume_number()?;
    cursor.consume_char(b':')?;
    let second = cursor.consume_number()?;
    cursor.consume_char(b' ')?;
    for expected in b"GMT" {
        cursor.consume_char(*expected)?;
    }
    if cursor.position != input.len() {
        return None;
    }

    // timegm() normalizes out-of-range fields linearly, so the sum below yields the same instant.
    let days = days_from_civil(year, month + 1, 1);
    let seconds = days * 86400 + (day - 1) * 86400 + hour * 3600 + minute * 60 + second;
    if seconds == -1 {
        return None;
    }
    Some(seconds)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<i64> {
        parse_http_date(s.as_bytes())
    }

    #[test]
    fn rfc_example() {
        assert_eq!(parse("Sun, 06 Nov 1994 08:49:37 GMT"), Some(784111777));
    }

    #[test]
    fn epoch() {
        assert_eq!(parse("Thu, 01 Jan 1970 00:00:00 GMT"), Some(0));
    }

    #[test]
    fn minus_one_is_rejected_like_timegm() {
        assert_eq!(parse("Wed, 31 Dec 1969 23:59:59 GMT"), None);
    }

    #[test]
    fn day_and_month_names_are_case_insensitive() {
        assert_eq!(parse("sun, 06 NOV 1994 08:49:37 GMT"), Some(784111777));
    }

    #[test]
    fn gmt_literal_is_case_sensitive() {
        assert_eq!(parse("Sun, 06 Nov 1994 08:49:37 gmt"), None);
    }

    #[test]
    fn rejects_other_zones_and_trailing_data() {
        assert_eq!(parse("Sun, 06 Nov 1994 08:49:37 UTC"), None);
        assert_eq!(parse("Sun, 06 Nov 1994 08:49:37 GMT x"), None);
    }

    #[test]
    fn rejects_bad_names_and_separators() {
        assert_eq!(parse("Sxn, 06 Nov 1994 08:49:37 GMT"), None);
        assert_eq!(parse("Sun 06 Nov 1994 08:49:37 GMT"), None);
        assert_eq!(parse("Sun, 06 Foo 1994 08:49:37 GMT"), None);
    }

    #[test]
    fn rejects_truncated() {
        assert_eq!(parse("Sun, 06 Nov 1994 08:49"), None);
    }
}

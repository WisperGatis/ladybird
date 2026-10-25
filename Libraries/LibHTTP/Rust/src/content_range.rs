// Single-byte Content-Range parsing (WICG Background Fetch, single-byte-content-range), ported from
// LibHTTP/HeaderList.cpp. Integers follow AK's GenericLexer::consume_decimal_integer<u64>: an optional sign, then
// digits; a minus sign is only accepted on zero, and values beyond u64 are rejected.

pub struct ContentRange {
    pub first_byte_pos: u64,
    pub last_byte_pos: u64,
    pub complete_length: Option<u64>,
}

struct Cursor<'a> {
    data: &'a [u8],
    position: usize,
}

impl Cursor<'_> {
    fn consume_literal(&mut self, expected: &[u8]) -> Option<()> {
        let end = self.position + expected.len();
        if self.data.get(self.position..end)? == expected {
            self.position = end;
            Some(())
        } else {
            None
        }
    }

    fn consume_char(&mut self, expected: u8) -> Option<()> {
        self.consume_literal(&[expected])
    }

    fn consume_u64(&mut self) -> Option<u64> {
        let mut negative = false;
        match self.data.get(self.position) {
            Some(b'+') => self.position += 1,
            Some(b'-') => {
                negative = true;
                self.position += 1;
            }
            _ => {}
        }

        let digits_start = self.position;
        let mut value: u64 = 0;
        while let Some(&digit) = self.data.get(self.position) {
            if !digit.is_ascii_digit() {
                break;
            }
            value = value.checked_mul(10)?.checked_add((digit - b'0') as u64)?;
            self.position += 1;
        }
        if self.position == digits_start {
            return None;
        }

        if negative && value != 0 {
            return None;
        }
        Some(value)
    }
}

/// Parses a single-byte Content-Range value such as "bytes 0-499/1234" or "bytes 0-499/*".
pub fn parse_content_range(input: &[u8]) -> Option<ContentRange> {
    let mut cursor = Cursor { data: input, position: 0 };

    // AD-HOC: The spec wants an '=', but the RFC requires a space (WICG/background-fetch#154).
    cursor.consume_literal(b"bytes ")?;

    let first_byte_pos = cursor.consume_u64()?;
    cursor.consume_char(b'-')?;
    let last_byte_pos = cursor.consume_u64()?;
    cursor.consume_char(b'/')?;

    let complete_length = if cursor.data.get(cursor.position) == Some(&b'*') {
        cursor.position += 1;
        None
    } else {
        Some(cursor.consume_u64()?)
    };

    if cursor.position != input.len() {
        return None;
    }

    Some(ContentRange { first_byte_pos, last_byte_pos, complete_length })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &str) -> Option<(u64, u64, Option<u64>)> {
        parse_content_range(s.as_bytes()).map(|r| (r.first_byte_pos, r.last_byte_pos, r.complete_length))
    }

    #[test]
    fn complete_length() {
        assert_eq!(parse("bytes 0-499/1234"), Some((0, 499, Some(1234))));
    }

    #[test]
    fn unknown_length() {
        assert_eq!(parse("bytes 500-999/*"), Some((500, 999, None)));
    }

    #[test]
    fn plus_sign_and_negative_zero_are_accepted() {
        assert_eq!(parse("bytes +1-2/3"), Some((1, 2, Some(3))));
        assert_eq!(parse("bytes -0-2/3"), Some((0, 2, Some(3))));
    }

    #[test]
    fn negative_nonzero_is_rejected() {
        assert_eq!(parse("bytes -1-2/3"), None);
    }

    #[test]
    fn requires_space_not_equals() {
        assert_eq!(parse("bytes=0-499/1234"), None);
    }

    #[test]
    fn rejects_overflow_and_trailing_data() {
        assert_eq!(parse("bytes 0-18446744073709551616/1"), None);
        assert_eq!(parse("bytes 0-499/1234 "), None);
    }

    #[test]
    fn rejects_missing_parts() {
        assert_eq!(parse("bytes 0-499"), None);
        assert_eq!(parse("bytes 0/1234"), None);
        assert_eq!(parse("bytes "), None);
    }
}

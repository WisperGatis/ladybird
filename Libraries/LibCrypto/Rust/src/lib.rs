/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// DER tag, length and primitive value checks (ITU-T X.690), shared with LibCrypto/ASN1/DER.cpp. Every function here
// reads only the bytes it is given, so malformed certificate or key input cannot reach memory outside it.

#[path = "../../../RustDemangle.rs"]
mod rust_demangle;

#[path = "../../../RustPanic.rs"]
mod rust_panic;

use std::ptr;
use std::slice;

#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Asn1Status {
    Ok = 0,
    // The input ended before the requested bytes.
    Truncated = 1,
    // Length uses an indefinite or reserved form, or does not fit in the target type.
    InvalidLength = 2,
    // A primitive value has the wrong size or encoding.
    InvalidValue = 3,
    // A caller-provided buffer is too small for the decoded value.
    OutputTooSmall = 4,
}

fn read_byte(data: &[u8], position: &mut usize) -> Result<u8, Asn1Status> {
    let byte = *data.get(*position).ok_or(Asn1Status::Truncated)?;
    *position += 1;
    Ok(byte)
}

// X.690 8.1.2. Identifier octets: class (bits 8-7), constructed flag (bit 6), tag number (bits 5-1, or base-128
// continuation when all set).
fn parse_tag(data: &[u8], position: &mut usize) -> Result<(u8, u8, u32), Asn1Status> {
    let byte = read_byte(data, position)?;
    let class = byte & 0xc0;
    let constructed = byte & 0x20;
    let mut kind = (byte & 0x1f) as u32;

    // High-tag-number form: the tag number follows in base-128 octets, high bit set on all but the last.
    if kind == 0x1f {
        kind = 0;
        loop {
            let current = read_byte(data, position)?;
            kind = kind.checked_mul(128).ok_or(Asn1Status::InvalidLength)? | (current & 0x7f) as u32;
            if current & 0x80 == 0 {
                break;
            }
        }
    }
    Ok((class, constructed, kind))
}

// X.690 8.1.3. Length: short form, or long form with 1 to 126 length octets. The indefinite form (0x80) and the
// reserved 0xff are refused, as in the C++ decoder.
fn parse_length(data: &[u8], position: &mut usize) -> Result<usize, Asn1Status> {
    let byte = read_byte(data, position)?;
    if byte & 0x80 == 0 {
        return Ok(byte as usize);
    }

    let count = (byte & 0x7f) as usize;
    if count == 0 || count == 0x7f {
        return Err(Asn1Status::InvalidLength);
    }
    if count > std::mem::size_of::<usize>() {
        return Err(Asn1Status::InvalidLength);
    }
    let bytes = data.get(*position..*position + count).ok_or(Asn1Status::Truncated)?;
    *position += count;

    Ok(bytes.iter().fold(0usize, |acc, &b| (acc << 8) | b as usize))
}

/// Reads the identifier octets from `data` at `*position`, advancing it past them.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes. `position`, `class`, `constructed` and `kind` must be valid for
/// reads and writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_read_tag(
    data: *const u8,
    length: usize,
    position: *mut usize,
    class: *mut u8,
    constructed: *mut u8,
    kind: *mut u32,
) -> Asn1Status {
    if data.is_null() || position.is_null() || class.is_null() || constructed.is_null() || kind.is_null() {
        return Asn1Status::Truncated;
    }
    let data = unsafe { slice::from_raw_parts(data, length) };
    let mut cursor = unsafe { *position };
    let (parsed_class, parsed_constructed, parsed_kind) = match parse_tag(data, &mut cursor) {
        Ok(tag) => tag,
        Err(status) => return status,
    };
    unsafe {
        ptr::write(position, cursor);
        ptr::write(class, parsed_class);
        ptr::write(constructed, parsed_constructed);
        ptr::write(kind, parsed_kind);
    }
    Asn1Status::Ok
}

/// Reads the length octets from `data` at `*position`, advancing it past them and writing the content length.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes. `position` and `content_length` must be valid for reads and
/// writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_read_length(
    data: *const u8,
    length: usize,
    position: *mut usize,
    content_length: *mut usize,
) -> Asn1Status {
    if data.is_null() || position.is_null() || content_length.is_null() {
        return Asn1Status::Truncated;
    }
    let data = unsafe { slice::from_raw_parts(data, length) };
    let mut cursor = unsafe { *position };
    let parsed_length = match parse_length(data, &mut cursor) {
        Ok(len) => len,
        Err(status) => return status,
    };
    unsafe {
        ptr::write(position, cursor);
        ptr::write(content_length, parsed_length);
    }
    Asn1Status::Ok
}

/// Checks that `data` is a DER boolean value (one octet, zero or nonzero).
#[unsafe(no_mangle)]
pub extern "C" fn ladybird_asn1_check_boolean(_data: *const u8, length: usize) -> Asn1Status {
    if length == 1 { Asn1Status::Ok } else { Asn1Status::InvalidValue }
}

/// Checks that `data` is a DER null value (empty).
#[unsafe(no_mangle)]
pub extern "C" fn ladybird_asn1_check_null(length: usize) -> Asn1Status {
    if length == 0 { Asn1Status::Ok } else { Asn1Status::InvalidValue }
}

/// Checks a DER INTEGER used as an unsigned value: non-empty, minimally encoded, and not negative.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_check_unsigned_integer(data: *const u8, length: usize) -> Asn1Status {
    if data.is_null() || length < 1 {
        return Asn1Status::InvalidValue;
    }
    let data = unsafe { slice::from_raw_parts(data, length) };
    if data.len() > 1 && ((data[0] == 0xff && data[1] & 0x80 != 0) || (data[0] == 0x00 && data[1] & 0x80 == 0)) {
        return Asn1Status::InvalidValue;
    }
    if data[0] & 0x80 != 0 {
        return Asn1Status::InvalidValue;
    }
    Asn1Status::Ok
}

/// Checks a DER printable string, which the decoder requires to be valid UTF-8.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_check_printable_string(data: *const u8, length: usize) -> Asn1Status {
    if data.is_null() && length != 0 {
        return Asn1Status::InvalidValue;
    }
    let bytes: &[u8] = if length == 0 { &[] } else { unsafe { slice::from_raw_parts(data, length) } };
    match std::str::from_utf8(bytes) {
        Ok(_) => Asn1Status::Ok,
        Err(_) => Asn1Status::InvalidValue,
    }
}

/// Checks a DER bit string and reports its unused-bit count.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes and `unused_bits` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_check_bit_string(data: *const u8, length: usize, unused_bits: *mut u8) -> Asn1Status {
    if data.is_null() || unused_bits.is_null() || length < 1 {
        return Asn1Status::InvalidValue;
    }
    let data = unsafe { slice::from_raw_parts(data, length) };
    let unused = data[0] as usize;
    let total_bits = (data.len() - 1) * 8;
    if unused > total_bits {
        return Asn1Status::InvalidValue;
    }
    unsafe { ptr::write(unused_bits, data[0]) };
    Asn1Status::Ok
}

/// Decodes a DER object identifier into `out`, first the two combined arcs split as X.690 8.19.4 describes, then the
/// rest. Writes the number of components to `out_count`.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes, `out` for writes of `capacity` values, and `out_count` for
/// writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_asn1_decode_object_identifier(
    data: *const u8,
    length: usize,
    out: *mut u32,
    capacity: usize,
    out_count: *mut usize,
) -> Asn1Status {
    if data.is_null() || out.is_null() || out_count.is_null() || length == 0 {
        return Asn1Status::InvalidValue;
    }
    let data = unsafe { slice::from_raw_parts(data, length) };
    let out = unsafe { slice::from_raw_parts_mut(out, capacity) };

    // Subidentifiers must be minimally encoded and no wider than 28 bits, as the C++ encoder requires.
    const MAX_SUBIDENTIFIER: u64 = 0x0fff_ffff;
    let mut subidentifiers: Vec<u64> = Vec::new();
    let mut value: u64 = 0;
    for &byte in data {
        if value == 0 && byte == 0x80 {
            return Asn1Status::InvalidValue;
        }
        value = (value << 7) | (byte & 0x7f) as u64;
        if value > MAX_SUBIDENTIFIER {
            return Asn1Status::InvalidValue;
        }
        if byte & 0x80 == 0 {
            subidentifiers.push(value);
            value = 0;
        }
    }
    if value != 0 {
        return Asn1Status::InvalidValue;
    }

    let first = subidentifiers[0];
    if first >= 1600 {
        return Asn1Status::InvalidValue;
    }
    let count = 2 + subidentifiers.len() - 1;
    if count > capacity {
        return Asn1Status::OutputTooSmall;
    }
    out[0] = (first / 40) as u32;
    out[1] = (first % 40) as u32;
    for (index, &arc) in subidentifiers.iter().enumerate().skip(1) {
        out[index + 1] = arc as u32;
    }
    unsafe { ptr::write(out_count, count) };
    Asn1Status::Ok
}

#[cfg(test)]
mod tests {
    use super::*;

    fn header(data: &[u8]) -> Result<(u8, u8, u32, usize, usize), Asn1Status> {
        let mut position = 0;
        let (class, constructed, kind) = parse_tag(data, &mut position)?;
        let length = parse_length(data, &mut position)?;
        Ok((class, constructed, kind, length, position))
    }

    #[test]
    fn short_form_header() {
        assert_eq!(header(&[0x02, 0x01, 0x05]), Ok((0, 0, 2, 1, 2)));
    }

    #[test]
    fn long_form_header() {
        assert_eq!(header(&[0x30, 0x82, 0x01, 0x00]), Ok((0, 0x20, 16, 256, 4)));
    }

    #[test]
    fn high_tag_number() {
        // Tag 0x1f 0x81 0x01 is tag number 129.
        assert_eq!(header(&[0x1f, 0x81, 0x01, 0x00]), Ok((0, 0, 129, 0, 4)));
    }

    #[test]
    fn indefinite_and_reserved_lengths_are_rejected() {
        assert_eq!(header(&[0x30, 0x80]), Err(Asn1Status::InvalidLength));
        assert_eq!(header(&[0x30, 0xff]), Err(Asn1Status::InvalidLength));
    }

    #[test]
    fn truncated_length_is_rejected() {
        assert_eq!(header(&[0x30, 0x82, 0x01]), Err(Asn1Status::Truncated));
    }

    #[test]
    fn oversized_length_is_rejected() {
        // Nine length octets cannot fit in a usize, which is at most eight on this platform.
        let mut data = vec![0x30, 0x89];
        data.extend([0xff; 9]);
        assert_eq!(header(&data), Err(Asn1Status::InvalidLength));
    }

    #[test]
    fn unsigned_integer_checks() {
        let check = |bytes: &[u8]| unsafe { ladybird_asn1_check_unsigned_integer(bytes.as_ptr(), bytes.len()) };
        assert_eq!(check(&[0x05]), Asn1Status::Ok);
        assert_eq!(check(&[0x00, 0x80]), Asn1Status::Ok);
        assert_eq!(check(&[0x80]), Asn1Status::InvalidValue);
        assert_eq!(check(&[0x00, 0x05]), Asn1Status::InvalidValue);
        assert_eq!(check(&[0xff, 0x80]), Asn1Status::InvalidValue);
        assert_eq!(check(&[]), Asn1Status::InvalidValue);
    }

    #[test]
    fn boolean_and_null_checks() {
        assert_eq!(ladybird_asn1_check_boolean([0xff].as_ptr(), 1), Asn1Status::Ok);
        assert_eq!(ladybird_asn1_check_boolean([0xff, 0x00].as_ptr(), 2), Asn1Status::InvalidValue);
        assert_eq!(ladybird_asn1_check_null(0), Asn1Status::Ok);
        assert_eq!(ladybird_asn1_check_null(1), Asn1Status::InvalidValue);
    }

    #[test]
    fn bit_string_unused_bits() {
        let mut unused = 0u8;
        let data = [0x03, 0xaa];
        assert_eq!(unsafe { ladybird_asn1_check_bit_string(data.as_ptr(), 2, &mut unused) }, Asn1Status::Ok);
        assert_eq!(unused, 3);
        let data = [0x09, 0xaa];
        assert_eq!(unsafe { ladybird_asn1_check_bit_string(data.as_ptr(), 2, &mut unused) }, Asn1Status::InvalidValue);
    }

    #[test]
    fn printable_string_must_be_utf8() {
        assert_eq!(unsafe { ladybird_asn1_check_printable_string(b"ok".as_ptr(), 2) }, Asn1Status::Ok);
        assert_eq!(unsafe { ladybird_asn1_check_printable_string([0xff].as_ptr(), 1) }, Asn1Status::InvalidValue);
    }

    #[test]
    fn object_identifier_rsa_encryption() {
        // 1.2.840.113549.1.1.1 (rsaEncryption)
        let data = [0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01];
        let mut out = [0u32; 16];
        let mut count = 0usize;
        let status = unsafe { ladybird_asn1_decode_object_identifier(data.as_ptr(), data.len(), out.as_mut_ptr(), out.len(), &mut count) };
        assert_eq!(status, Asn1Status::Ok);
        assert_eq!(&out[..count], &[1, 2, 840, 113549, 1, 1, 1]);
    }

    #[test]
    fn object_identifier_rejects_non_minimal_and_unterminated() {
        let mut out = [0u32; 8];
        let mut count = 0usize;
        let non_minimal = [0x2a, 0x80, 0x01];
        assert_eq!(unsafe { ladybird_asn1_decode_object_identifier(non_minimal.as_ptr(), non_minimal.len(), out.as_mut_ptr(), out.len(), &mut count) }, Asn1Status::InvalidValue);
        let unterminated = [0x2a, 0x86];
        assert_eq!(unsafe { ladybird_asn1_decode_object_identifier(unterminated.as_ptr(), unterminated.len(), out.as_mut_ptr(), out.len(), &mut count) }, Asn1Status::InvalidValue);
    }
}

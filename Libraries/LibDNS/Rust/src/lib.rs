/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Wire-format domain name decoding (RFC 1035, 4.1.4. Message compression), shared with the C++ parser in
// Libraries/LibDNS/Message.cpp. The decoder only reads the message bytes it is given, so it cannot touch memory
// outside the message, and it bounds every pointer jump so that compression loops terminate.

#[path = "../../../RustDemangle.rs"]
mod rust_demangle;

#[path = "../../../RustPanic.rs"]
mod rust_panic;

use std::ptr;
use std::slice;

// RFC 1035, 2.3.4. Size limits: names are 255 octets or less, including the root label's terminating zero.
const MAX_NAME_WIRE_LENGTH: usize = 255;
// A name cannot legitimately contain more pointers than the message has bytes, but a small fixed cap keeps
// pathological messages cheap to reject.
const MAX_POINTER_HOPS: usize = 128;

const POINTER_MARKER_MASK: u8 = 0b1100_0000;
const POINTER_MARKER: u8 = 0b1100_0000;
const RESERVED_LABEL_MASK: u8 = 0b1100_0000;

#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DnsNameStatus {
    Ok = 0,
    // The name runs past the end of the message.
    Truncated = 1,
    // A pointer targets a position at or after itself, or outside the message.
    InvalidPointer = 2,
    // A pointer chain is longer than MAX_POINTER_HOPS.
    TooManyPointers = 3,
    // A label length uses the reserved 0b01 or 0b10 top bits, which also covers any label longer than 63 octets.
    ReservedLabelType = 4,
    // The name is longer than 255 octets on the wire.
    NameTooLong = 5,
    // The caller's output buffer is too small for the decoded name.
    OutputTooSmall = 6,
}

/// Decodes the domain name starting at `offset` in `message`.
///
/// On success, `out` receives the name in uncompressed wire form (length-prefixed labels followed by the root's zero
/// octet), `out_length` is its size, and `consumed` is the number of octets the name occupies at `offset` (a pointer
/// counts as its two octets, not the length of its target). The C++ caller advances its stream by `consumed`.
///
/// # Safety
/// `message` must be valid for reads of `message_length` bytes, `out` must be valid for writes of `out_capacity`
/// bytes, and `out_length` and `consumed` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_dns_decode_name(
    message: *const u8,
    message_length: usize,
    offset: usize,
    out: *mut u8,
    out_capacity: usize,
    out_length: *mut usize,
    consumed: *mut usize,
) -> DnsNameStatus {
    if message.is_null() || out.is_null() || out_length.is_null() || consumed.is_null() {
        return DnsNameStatus::Truncated;
    }
    let message = unsafe { slice::from_raw_parts(message, message_length) };
    let out = unsafe { slice::from_raw_parts_mut(out, out_capacity) };
    match decode_name(message, offset, out) {
        Ok((written, used)) => {
            unsafe {
                ptr::write(out_length, written);
                ptr::write(consumed, used);
            }
            DnsNameStatus::Ok
        }
        Err(status) => status,
    }
}

fn decode_name(message: &[u8], offset: usize, out: &mut [u8]) -> Result<(usize, usize), DnsNameStatus> {
    let mut position = offset;
    // Octets of the name in the original location; fixed once the first pointer is followed.
    let mut consumed: Option<usize> = None;
    let mut written = 0;
    let mut wire_length = 1;
    let mut hops = 0;

    loop {
        let length = *message.get(position).ok_or(DnsNameStatus::Truncated)?;

        if length == 0 {
            if written >= out.len() {
                return Err(DnsNameStatus::OutputTooSmall);
            }
            out[written] = 0;
            written += 1;
            let used = consumed.unwrap_or(position + 1 - offset);
            return Ok((written, used));
        }

        if length & POINTER_MARKER_MASK == POINTER_MARKER {
            let low = *message.get(position + 1).ok_or(DnsNameStatus::Truncated)?;
            let target = (((length & !POINTER_MARKER_MASK) as usize) << 8) | low as usize;
            if consumed.is_none() {
                consumed = Some(position + 2 - offset);
            }
            // A pointer must refer to an earlier octet, which also guarantees progress through the message.
            if target >= position {
                return Err(DnsNameStatus::InvalidPointer);
            }
            hops += 1;
            if hops > MAX_POINTER_HOPS {
                return Err(DnsNameStatus::TooManyPointers);
            }
            position = target;
            continue;
        }

        if length & RESERVED_LABEL_MASK != 0 {
            return Err(DnsNameStatus::ReservedLabelType);
        }

        let label_length = length as usize;
        wire_length += 1 + label_length;
        if wire_length > MAX_NAME_WIRE_LENGTH {
            return Err(DnsNameStatus::NameTooLong);
        }

        let label_start = position + 1;
        let label_end = label_start + label_length;
        let label = message.get(label_start..label_end).ok_or(DnsNameStatus::Truncated)?;
        if written + 1 + label_length > out.len() {
            return Err(DnsNameStatus::OutputTooSmall);
        }
        out[written] = length;
        out[written + 1..written + 1 + label_length].copy_from_slice(label);
        written += 1 + label_length;
        position = label_end;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn decode(message: &[u8], offset: usize) -> Result<(Vec<u8>, usize), DnsNameStatus> {
        let mut out = [0u8; MAX_NAME_WIRE_LENGTH];
        let (written, used) = decode_name(message, offset, &mut out)?;
        Ok((out[..written].to_vec(), used))
    }

    #[test]
    fn plain_name() {
        let message = b"\x03www\x07example\x03com\x00";
        let (name, used) = decode(message, 0).unwrap();
        assert_eq!(name, b"\x03www\x07example\x03com\x00");
        assert_eq!(used, message.len());
    }

    #[test]
    fn root_name() {
        let (name, used) = decode(b"\x00", 0).unwrap();
        assert_eq!(name, b"\x00");
        assert_eq!(used, 1);
    }

    #[test]
    fn pointer_suffix_consumes_two_octets() {
        // "example.com" at 0, then "www" + pointer to 0 at offset 13.
        let mut message = b"\x07example\x03com\x00".to_vec();
        let start = message.len();
        message.extend_from_slice(b"\x03www\xc0\x00");
        let (name, used) = decode(&message, start).unwrap();
        assert_eq!(name, b"\x03www\x07example\x03com\x00");
        assert_eq!(used, 6);
    }

    #[test]
    fn pointer_into_middle_of_earlier_name() {
        // "example.com" at 0; pointer to "com" (offset 8) from offset 11.
        let mut message = b"\x07example\x03com\x00".to_vec();
        let start = message.len();
        message.extend_from_slice(b"\xc0\x08");
        let (name, used) = decode(&message, start).unwrap();
        assert_eq!(name, b"\x03com\x00");
        assert_eq!(used, 2);
    }

    #[test]
    fn self_pointer_is_rejected() {
        let message = b"\xc0\x00";
        assert_eq!(decode(message, 0), Err(DnsNameStatus::InvalidPointer));
    }

    #[test]
    fn forward_pointer_is_rejected() {
        let message = b"\xc0\x05\x00\x00\x00\x00\x00";
        assert_eq!(decode(message, 0), Err(DnsNameStatus::InvalidPointer));
    }

    #[test]
    fn pointer_loop_is_rejected() {
        // A long chain of backward pointers, each pointing at the previous one, longer than MAX_POINTER_HOPS.
        let mut message = vec![0x00, 0x00];
        for _ in 0..(MAX_POINTER_HOPS + 2) {
            let end = message.len();
            message.extend_from_slice(&[0xc0 | ((end - 2) >> 8) as u8, (end - 2) as u8]);
        }
        let start = message.len() - 2;
        assert_eq!(decode(&message, start), Err(DnsNameStatus::TooManyPointers));
    }

    #[test]
    fn truncated_label_is_rejected() {
        assert_eq!(decode(b"\x05ab", 0), Err(DnsNameStatus::Truncated));
    }

    #[test]
    fn reserved_label_type_is_rejected() {
        assert_eq!(decode(b"\x40a\x00", 0), Err(DnsNameStatus::ReservedLabelType));
        assert_eq!(decode(b"\x80a\x00", 0), Err(DnsNameStatus::ReservedLabelType));
    }

    #[test]
    fn overlong_label_is_rejected() {
        // A 64-octet label length has the 0b01 top bits set, so it is reserved and rejected like the other forms.
        let mut message = vec![64u8];
        message.extend(std::iter::repeat_n(b'a', 64));
        message.push(0);
        assert_eq!(decode(&message, 0), Err(DnsNameStatus::ReservedLabelType));
    }

    #[test]
    fn overlong_name_is_rejected() {
        // Four 63-octet labels is 4 * 64 + 1 = 257 octets on the wire.
        let mut message = Vec::new();
        for _ in 0..4 {
            message.push(63);
            message.extend(std::iter::repeat_n(b'a', 63));
        }
        message.push(0);
        assert_eq!(decode(&message, 0), Err(DnsNameStatus::NameTooLong));
    }
}

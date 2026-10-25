/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Container header parsing for LibMedia, shared with LibMedia/Containers. Every function reads only the bytes it is
// given, so untrusted media cannot reach memory outside the input slice.

#[path = "../../../RustDemangle.rs"]
mod rust_demangle;

#[path = "../../../RustPanic.rs"]
mod rust_panic;

mod adts;
mod bits;
mod mp3;

use std::ptr;
use std::slice;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AdtsFrameHeader {
    pub version: u8,
    pub audio_object_type: u8,
    pub has_crc: bool,
    pub sample_rate: u32,
    pub channel_count: u8,
    pub block_count: u8,
    pub sample_count: u16,
    pub frame_byte_size: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Mp3FrameHeader {
    pub version: u8,
    pub layer: u8,
    pub has_crc: bool,
    pub sample_rate: u32,
    pub channel_count: u8,
    pub sample_count: u16,
    pub frame_byte_size: u16,
}

/// Parses an ADTS frame header from `data`. Returns false when the bytes are not a valid header.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes, and `out` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_media_parse_adts_frame_header(data: *const u8, length: usize, out: *mut AdtsFrameHeader) -> bool {
    if data.is_null() || out.is_null() {
        return false;
    }
    let bytes = unsafe { slice::from_raw_parts(data, length) };
    match adts::parse(bytes) {
        Some(header) => {
            unsafe {
                ptr::write(out, AdtsFrameHeader {
                    version: header.version,
                    audio_object_type: header.audio_object_type,
                    has_crc: header.has_crc,
                    sample_rate: header.sample_rate,
                    channel_count: header.channel_count,
                    block_count: header.block_count,
                    sample_count: header.sample_count,
                    frame_byte_size: header.frame_byte_size,
                });
            }
            true
        }
        None => false,
    }
}

/// Parses an MPEG audio frame header from `data`. Returns false when the bytes are not a valid header.
///
/// # Safety
/// `data` must be valid for reads of `length` bytes, and `out` must be valid for writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ladybird_media_parse_mp3_frame_header(data: *const u8, length: usize, out: *mut Mp3FrameHeader) -> bool {
    if data.is_null() || out.is_null() {
        return false;
    }
    let bytes = unsafe { slice::from_raw_parts(data, length) };
    match mp3::parse(bytes) {
        Some(header) => {
            unsafe {
                ptr::write(out, Mp3FrameHeader {
                    version: header.version,
                    layer: header.layer,
                    has_crc: header.has_crc,
                    sample_rate: header.sample_rate,
                    channel_count: header.channel_count,
                    sample_count: header.sample_count,
                    frame_byte_size: header.frame_byte_size,
                });
            }
            true
        }
        None => false,
    }
}

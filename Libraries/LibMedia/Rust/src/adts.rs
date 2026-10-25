// ADTS frame header (ISO/IEC 13818-7, 6.2), ported from LibMedia/Containers/ADTS/FrameHeader.cpp.

use crate::bits::BitReader;

const SAMPLING_RATES: [u32; 16] = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0];

// Channel configuration zero leaves the channels to a configuration ADTS does not carry, so it is rejected.
const CHANNEL_COUNTS: [u8; 8] = [0, 1, 2, 3, 4, 5, 6, 8];

pub const SIZE: usize = 7;
pub const CRC_SIZE: usize = 2;
pub const SYNC_CODE_BIT_COUNT: u32 = 12;
pub const SYNC_CODE: u32 = 0xfff;
pub const SAMPLES_PER_BLOCK: u16 = 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameHeader {
    // 0 for MPEG-2, 1 for MPEG-4, matching ADTS::MPEGVersion.
    pub version: u8,
    pub audio_object_type: u8,
    pub has_crc: bool,
    pub sample_rate: u32,
    pub channel_count: u8,
    pub block_count: u8,
    pub sample_count: u16,
    pub frame_byte_size: u16,
}

pub fn parse(data: &[u8]) -> Option<FrameHeader> {
    let mut reader = BitReader::new(data);
    if reader.read_bits(SYNC_CODE_BIT_COUNT) != SYNC_CODE {
        return None;
    }

    let version = if reader.read_bit() { 0 } else { 1 };

    if reader.read_bits(2) != 0 {
        return None;
    }

    let protection_absent = reader.read_bit();
    let profile = reader.read_bits(2) as u8;

    let sample_rate = SAMPLING_RATES[reader.read_bits(4) as usize];
    if sample_rate == 0 {
        return None;
    }

    let _private_bit = reader.read_bit();

    let channel_count = CHANNEL_COUNTS[reader.read_bits(3) as usize];
    if channel_count == 0 {
        return None;
    }

    // originality, home, copyright identification bit and start: not used.
    reader.read_bits(4);

    let frame_byte_size = reader.read_bits(13) as u16;

    // buffer fullness: not used.
    reader.read_bits(11);

    let block_count = reader.read_bits(2) as u8 + 1;

    if reader.overrun() {
        return None;
    }

    let header_byte_size = if protection_absent { SIZE } else { SIZE + CRC_SIZE };
    if (frame_byte_size as usize) <= header_byte_size {
        return None;
    }

    Some(FrameHeader {
        version,
        audio_object_type: profile + 1,
        has_crc: !protection_absent,
        sample_rate,
        channel_count,
        block_count,
        sample_count: SAMPLES_PER_BLOCK * block_count as u16,
        frame_byte_size,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // A valid 48 kHz stereo AAC-LC header with no CRC, frame size 0x0100.
    fn valid_header() -> Vec<u8> {
        // sync 0xfff, MPEG-4 (bit 0), layer 0, protection_absent 1, profile 1 (AAC-LC),
        // sampling index 3 (48 kHz), private 0, channels 2, original/home/copyright 0, frame size 256, fullness 0x7ff, 1 block
        vec![0xff, 0xf1, 0x4c, 0x80, 0x10, 0x1f, 0xfc]
    }

    #[test]
    fn parses_valid_header() {
        let header = parse(&valid_header()).unwrap();
        assert_eq!(header.sample_rate, 48000);
        assert_eq!(header.channel_count, 2);
        assert_eq!(header.block_count, 1);
        assert_eq!(header.sample_count, 1024);
        assert!(!header.has_crc);
    }

    #[test]
    fn rejects_bad_sync() {
        let mut data = valid_header();
        data[0] = 0x00;
        assert_eq!(parse(&data), None);
    }

    #[test]
    fn rejects_truncated_header() {
        assert_eq!(parse(&valid_header()[..4]), None);
    }

    #[test]
    fn rejects_reserved_sampling_index() {
        let mut data = valid_header();
        // sampling index is bits 2-5 of byte 2; set to 13 (reserved, 0).
        data[2] = (data[2] & 0xc3) | (13 << 2);
        assert_eq!(parse(&data), None);
    }

    #[test]
    fn rejects_frame_size_not_larger_than_header() {
        let mut data = valid_header();
        // frame size is 13 bits spanning bytes 3-5; set it to 7 (equal to header size).
        data[3] = (data[3] & 0xfc) | 0x00;
        data[4] = 0x00;
        data[5] = (data[5] & 0x1f) | (7 << 5);
        assert_eq!(parse(&data), None);
    }
}

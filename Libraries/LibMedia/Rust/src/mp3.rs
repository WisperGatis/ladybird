// MPEG audio frame header (ISO/IEC 11172-3, 2.4.2.3), ported from LibMedia/Containers/MP3/FrameHeader.cpp.

use crate::bits::BitReader;

// Indexed [is_mpeg_version_2][layer_index][bitrate_index]; -1 marks a reserved value.
const BITRATES: [[[i16; 16]; 3]; 2] = [
    [
        [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1],
        [0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1],
        [0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1],
    ],
    [
        [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1],
        [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1],
        [0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1],
    ],
];

const SAMPLES_PER_FRAME: [[u16; 3]; 2] = [[1152, 1152, 384], [576, 1152, 384]];

// Indexed [mpeg_version_bits][sampling_index]; version bits are 0b00 (2.5), 0b01 (reserved), 0b10 (2), 0b11 (1).
const SAMPLING_RATES: [[u32; 4]; 4] = [[11025, 12000, 8000, 0], [0, 0, 0, 0], [22050, 24000, 16000, 0], [44100, 48000, 32000, 0]];

pub const SYNC_CODE_BIT_COUNT: u32 = 11;
pub const SYNC_CODE: u32 = 0x7ff;

const SINGLE_CHANNEL_MODE: u32 = 0b11;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameHeader {
    // 0 for Version1, 1 for Version2, 2 for Version2_5, matching MP3::MPEGVersion.
    pub version: u8,
    pub layer: u8,
    pub has_crc: bool,
    pub sample_rate: u32,
    pub channel_count: u8,
    pub sample_count: u16,
    pub frame_byte_size: u16,
}

pub fn parse(data: &[u8]) -> Option<FrameHeader> {
    let mut reader = BitReader::new(data);
    if reader.read_bits(SYNC_CODE_BIT_COUNT) != SYNC_CODE {
        return None;
    }

    let mpeg_version_bits = reader.read_bits(2) as usize;
    if mpeg_version_bits == 0b01 {
        return None;
    }
    let is_mpeg_version_2 = mpeg_version_bits != 0b11;
    let version = match mpeg_version_bits {
        0b10 => 1,
        0b11 => 0,
        _ => 2,
    };

    let layer_description = reader.read_bits(2) as usize;
    if layer_description == 0b00 {
        return None;
    }
    let is_layer_i = layer_description == 0b11;
    let layer_index = layer_description - 1;

    let protection_bit = reader.read_bit();

    let bitrate = BITRATES[is_mpeg_version_2 as usize][layer_index][reader.read_bits(4) as usize];
    if bitrate <= 0 {
        return None;
    }

    let sampling_frequency = SAMPLING_RATES[mpeg_version_bits][reader.read_bits(2) as usize];
    if sampling_frequency == 0 {
        return None;
    }

    let padding_bit = reader.read_bit() as u64;
    let _private_bit = reader.read_bit();
    let channel_mode = reader.read_bits(2);

    if reader.overrun() {
        return None;
    }

    let sample_count = SAMPLES_PER_FRAME[is_mpeg_version_2 as usize][layer_index];

    let slot_size: u64 = if is_layer_i { 4 } else { 1 };
    let bytes_per_kb: u64 = 1000 / 8;
    let slot_count = sample_count as u64 * bitrate as u64 * bytes_per_kb / (sampling_frequency as u64 * slot_size);

    Some(FrameHeader {
        version,
        layer: (4 - layer_description) as u8,
        has_crc: !protection_bit,
        sample_rate: sampling_frequency,
        channel_count: if channel_mode == SINGLE_CHANNEL_MODE { 1 } else { 2 },
        sample_count,
        frame_byte_size: ((slot_count + padding_bit) * slot_size) as u16,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_layer3_128k_44100_stereo() {
        // 0xFFFB: sync, MPEG-1, Layer III, no CRC; 0x90: bitrate 9 (128 kbps), 44.1 kHz, no padding; 0x00: stereo.
        let header = parse(&[0xff, 0xfb, 0x90, 0x00]).unwrap();
        assert_eq!(header.version, 0);
        assert_eq!(header.layer, 3);
        assert_eq!(header.sample_rate, 44100);
        assert_eq!(header.channel_count, 2);
        assert_eq!(header.sample_count, 1152);
        assert_eq!(header.frame_byte_size, 417);
    }

    #[test]
    fn rejects_reserved_version() {
        assert_eq!(parse(&[0xff, 0xe9, 0x90, 0x00]), None);
    }

    #[test]
    fn rejects_reserved_layer() {
        assert_eq!(parse(&[0xff, 0xf9, 0x90, 0x00]), None);
    }

    #[test]
    fn rejects_free_and_bad_bitrate() {
        assert_eq!(parse(&[0xff, 0xfb, 0x00, 0x00]), None);
        assert_eq!(parse(&[0xff, 0xfb, 0xf0, 0x00]), None);
    }

    #[test]
    fn rejects_truncated() {
        assert_eq!(parse(&[0xff, 0xfb]), None);
    }
}

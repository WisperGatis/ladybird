/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibMedia/Containers/ADTS/FrameHeader.h>
#include <RustFFI.h>

namespace Media::ADTS {

Optional<FrameHeader> FrameHeader::parse(ReadonlyBytes bytes)
{
    // The header is parsed by the Rust decoder in LibMedia/Rust, which rejects the same invalid frames.
    AdtsFrameHeader header;
    if (!ladybird_media_parse_adts_frame_header(bytes.data(), bytes.size(), &header))
        return {};

    return FrameHeader {
        .version = header.version == 0 ? MPEGVersion::Version2 : MPEGVersion::Version4,
        .audio_object_type = header.audio_object_type,
        .has_crc = header.has_crc,
        .sample_rate = header.sample_rate,
        .channel_count = header.channel_count,
        .block_count = header.block_count,
        .sample_count = header.sample_count,
        .frame_byte_size = header.frame_byte_size,
    };
}

}

/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibMedia/Containers/MP3/FrameHeader.h>
#include <RustFFI.h>

namespace Media::MP3 {

Optional<FrameHeader> FrameHeader::parse(ReadonlyBytes bytes)
{
    // The header is parsed by the Rust decoder in LibMedia/Rust, which rejects the same invalid frames.
    Mp3FrameHeader header;
    if (!ladybird_media_parse_mp3_frame_header(bytes.data(), bytes.size(), &header))
        return {};

    return FrameHeader {
        .version = static_cast<MPEGVersion>(header.version),
        .layer = header.layer,
        .has_crc = header.has_crc,
        .sample_rate = header.sample_rate,
        .channel_count = header.channel_count,
        .sample_count = header.sample_count,
        .frame_byte_size = header.frame_byte_size,
    };
}

}

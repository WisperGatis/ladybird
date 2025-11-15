/*
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <LibMedia/Demuxer.h>
#include <LibMedia/FFmpeg/FFmpegIOContext.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Media::FFmpeg {

class FFmpegDemuxer : public Demuxer {
public:
    static ErrorOr<NonnullOwnPtr<FFmpegDemuxer>> create(NonnullOwnPtr<SeekableStream> stream);
    static ErrorOr<NonnullOwnPtr<FFmpegDemuxer>> create_from_url(StringView url);

    FFmpegDemuxer(NonnullOwnPtr<SeekableStream> stream, Optional<NonnullOwnPtr<Media::FFmpeg::FFmpegIOContext>> io_context);
    FFmpegDemuxer(NonnullOwnPtr<SeekableStream> stream); // URL-based constructor
    virtual ~FFmpegDemuxer() override;

    virtual DecoderErrorOr<Vector<Track>> get_tracks_for_type(TrackType type) override;
    virtual DecoderErrorOr<Optional<Track>> get_preferred_track_for_type(TrackType type) override;

    virtual DecoderErrorOr<Optional<AK::Duration>> seek_to_most_recent_keyframe(Track track, AK::Duration timestamp, Optional<AK::Duration> earliest_available_sample = OptionalNone()) override;

    virtual DecoderErrorOr<AK::Duration> duration_of_track(Track const&) override;
    virtual DecoderErrorOr<AK::Duration> total_duration() override;

    virtual DecoderErrorOr<CodecID> get_codec_id_for_track(Track track) override;

    virtual DecoderErrorOr<ReadonlyBytes> get_codec_initialization_data_for_track(Track track) override;

    virtual DecoderErrorOr<CodedFrame> get_next_sample_for_track(Track track) override;

private:
    DecoderErrorOr<Track> get_track_for_stream_index(u32 stream_index);

    NonnullOwnPtr<SeekableStream> m_stream;
    AVCodecContext* m_codec_context { nullptr };
    AVFormatContext* m_format_context { nullptr };
    Optional<NonnullOwnPtr<Media::FFmpeg::FFmpegIOContext>> m_io_context;
    bool m_is_url_based { false };
    AVPacket* m_packet { nullptr };

public:
    bool is_hls_stream() const;
    bool is_seekable() const;
};

}

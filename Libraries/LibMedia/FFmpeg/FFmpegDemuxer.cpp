/*
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/MemoryStream.h>
#include <AK/Stream.h>
#include <AK/Time.h>
#include <LibMedia/FFmpeg/FFmpegDemuxer.h>
#include <LibMedia/FFmpeg/FFmpegHelpers.h>

namespace Media::FFmpeg {

FFmpegDemuxer::FFmpegDemuxer(NonnullOwnPtr<SeekableStream> stream, Optional<NonnullOwnPtr<Media::FFmpeg::FFmpegIOContext>> io_context)
    : m_stream(move(stream))
    , m_io_context(move(io_context))
    , m_is_url_based(!io_context.has_value())
{
}

FFmpegDemuxer::FFmpegDemuxer(NonnullOwnPtr<SeekableStream> stream)
    : m_stream(move(stream))
    , m_is_url_based(true)
{
}

FFmpegDemuxer::~FFmpegDemuxer()
{
    if (m_packet != nullptr)
        av_packet_free(&m_packet);
    if (m_codec_context != nullptr)
        avcodec_free_context(&m_codec_context);
    if (m_format_context != nullptr)
        avformat_close_input(&m_format_context);
}

ErrorOr<NonnullOwnPtr<FFmpegDemuxer>> FFmpegDemuxer::create(NonnullOwnPtr<SeekableStream> stream)
{
    auto io_context = TRY(Media::FFmpeg::FFmpegIOContext::create(*stream));
    auto demuxer = TRY(adopt_nonnull_own_or_enomem(new (nothrow) FFmpegDemuxer(move(stream), move(io_context))));

    // Open the container
    demuxer->m_format_context = avformat_alloc_context();
    if (demuxer->m_format_context == nullptr)
        return Error::from_string_literal("Failed to allocate format context");

    // For stream-based loading, set up the custom IO context
    if (demuxer->m_io_context.has_value())
        demuxer->m_format_context->pb = demuxer->m_io_context.value()->avio_context();

    if (avformat_open_input(&demuxer->m_format_context, nullptr, nullptr, nullptr) < 0)
        return Error::from_string_literal("Failed to open input for format parsing");

    // Read stream info; doing this is required for headerless formats like MPEG
    if (avformat_find_stream_info(demuxer->m_format_context, nullptr) < 0)
        return Error::from_string_literal("Failed to find stream info");

    demuxer->m_packet = av_packet_alloc();
    if (demuxer->m_packet == nullptr)
        return Error::from_string_literal("Failed to allocate packet");

    return demuxer;
}

ErrorOr<NonnullOwnPtr<FFmpegDemuxer>> FFmpegDemuxer::create_from_url(StringView url)
{
    // Create a dummy stream for the interface - it won't be used for URL-based loading
    auto dummy_stream = TRY(try_make<FixedMemoryStream>(ReadonlyBytes {}));
    auto demuxer = TRY(adopt_nonnull_own_or_enomem(new (nothrow) FFmpegDemuxer(move(dummy_stream), OptionalNone())));

    // Open the container directly from URL
    demuxer->m_format_context = avformat_alloc_context();
    if (demuxer->m_format_context == nullptr)
        return Error::from_string_literal("Failed to allocate format context");

    // Use FFmpeg's built-in URL handling instead of custom IO context
    if (avformat_open_input(&demuxer->m_format_context, url.to_byte_string().characters(), nullptr, nullptr) < 0)
        return Error::from_string_literal("Failed to open input from URL");

    // Read stream info; doing this is required for headerless formats like MPEG
    if (avformat_find_stream_info(demuxer->m_format_context, nullptr) < 0)
        return Error::from_string_literal("Failed to find stream info");

    demuxer->m_packet = av_packet_alloc();
    if (demuxer->m_packet == nullptr)
        return Error::from_string_literal("Failed to allocate packet");

    return demuxer;
}

static inline AK::Duration time_units_to_duration(i64 time_units, int numerator, int denominator)
{
    VERIFY(numerator != 0);
    VERIFY(denominator != 0);
    auto seconds = time_units * numerator / denominator;
    auto seconds_in_time_units = seconds * denominator / numerator;
    auto remainder_in_time_units = time_units - seconds_in_time_units;
    auto nanoseconds = ((remainder_in_time_units * 1'000'000'000 * numerator) + (denominator / 2)) / denominator;
    return AK::Duration::from_seconds(seconds) + AK::Duration::from_nanoseconds(nanoseconds);
}

static inline AK::Duration time_units_to_duration(i64 time_units, AVRational const& time_base)
{
    return time_units_to_duration(time_units, time_base.num, time_base.den);
}

DecoderErrorOr<AK::Duration> FFmpegDemuxer::total_duration()
{
    if (m_format_context->duration < 0) {
        return DecoderError::format(DecoderErrorCategory::Unknown, "Negative stream duration");
    }

    return time_units_to_duration(m_format_context->duration, 1, AV_TIME_BASE);
}

DecoderErrorOr<AK::Duration> FFmpegDemuxer::duration_of_track(Track const& track)
{
    VERIFY(track.identifier() < m_format_context->nb_streams);
    auto* stream = m_format_context->streams[track.identifier()];

    // For HLS streams, the duration is stored in the format context, not the stream.
    if (is_hls_stream()) {
        if (m_format_context->duration != AV_NOPTS_VALUE)
            return time_units_to_duration(m_format_context->duration, { 1, AV_TIME_BASE });
    }

    if (stream->duration >= 0) {
        return time_units_to_duration(stream->duration, stream->time_base);
    }

    // If the stream doesn't specify the duration, fallback to what the container says the duration is.
    return total_duration();
}

DecoderErrorOr<Track> FFmpegDemuxer::get_track_for_stream_index(u32 stream_index)
{
    VERIFY(stream_index < m_format_context->nb_streams);

    auto& stream = *m_format_context->streams[stream_index];
    auto type = track_type_from_ffmpeg_media_type(stream.codecpar->codec_type);
    Track track(type, stream_index);

    if (type == TrackType::Video) {
        track.set_video_data({
            .pixel_width = static_cast<u64>(stream.codecpar->width),
            .pixel_height = static_cast<u64>(stream.codecpar->height),
        });
    }

    return track;
}

DecoderErrorOr<Vector<Track>> FFmpegDemuxer::get_tracks_for_type(TrackType type)
{
    auto media_type = ffmpeg_media_type_from_track_type(type);
    Vector<Track> tracks = {};
    for (u32 i = 0; i < m_format_context->nb_streams; i++) {
        auto& stream = *m_format_context->streams[i];
        if (stream.codecpar->codec_type != media_type)
            continue;

        tracks.append(TRY(get_track_for_stream_index(i)));
    }
    return tracks;
}

DecoderErrorOr<Optional<Track>> FFmpegDemuxer::get_preferred_track_for_type(TrackType type)
{
    auto media_type = ffmpeg_media_type_from_track_type(type);
    auto best_stream_index = av_find_best_stream(m_format_context, media_type, -1, -1, nullptr, 0);
    if (best_stream_index < 0)
        return OptionalNone();

    return get_track_for_stream_index(best_stream_index);
}

DecoderErrorOr<Optional<AK::Duration>> FFmpegDemuxer::seek_to_most_recent_keyframe(Track track, AK::Duration timestamp, Optional<AK::Duration> earliest_available_sample)
{
    // FIXME: What do we do with this here?
    (void)earliest_available_sample;

    VERIFY(track.identifier() < m_format_context->nb_streams);
    auto* stream = m_format_context->streams[track.identifier()];

    // For HLS streams, seeking is handled by FFmpeg's internal HLS demuxer when we call av_read_frame.
    // We can just flush the context and FFmpeg will do the right thing.
    if (is_hls_stream()) {
        avformat_flush(m_format_context);
        return timestamp;
    }

    // Check if the stream is seekable
    if (!is_seekable()) {
        return DecoderError::format(DecoderErrorCategory::Unknown, "Stream is not seekable");
    }

    // Convert timestamp to stream time base using FFmpeg's av_rescale_q
    // First convert from milliseconds to AV_TIME_BASE units, then to stream time_base
    auto timestamp_in_av_time_base = static_cast<int64_t>((timestamp.to_milliseconds() * AV_TIME_BASE) / 1000);
    auto time_base_d = stream->time_base;
    if (time_base_d.num == 0 || time_base_d.den == 0) {
        return DecoderError::format(DecoderErrorCategory::Unknown, "Invalid time base for stream (num: {}, den: {})", time_base_d.num, time_base_d.den);
    }
    auto sample_timestamp = av_rescale_q(timestamp_in_av_time_base, AVRational { 1, AV_TIME_BASE }, time_base_d);

    // Try seeking with different strategies
    int seek_result = av_seek_frame(m_format_context, stream->index, sample_timestamp, AVSEEK_FLAG_BACKWARD);
    if (seek_result < 0) {
        // Fallback: try without flags
        seek_result = av_seek_frame(m_format_context, stream->index, sample_timestamp, 0);
        if (seek_result < 0) {
            // Final fallback: try nearest keyframe
            seek_result = av_seek_frame(m_format_context, stream->index, sample_timestamp, AVSEEK_FLAG_FRAME);
            if (seek_result < 0) {
                return DecoderError::format(DecoderErrorCategory::Unknown, "Failed to seek to timestamp {}ms (stream index: {}, sample timestamp: {})", timestamp.to_milliseconds(), stream->index, sample_timestamp);
            }
        }
    }

    return timestamp;
}

DecoderErrorOr<CodecID> FFmpegDemuxer::get_codec_id_for_track(Track track)
{
    VERIFY(track.identifier() < m_format_context->nb_streams);
    auto* stream = m_format_context->streams[track.identifier()];
    return media_codec_id_from_ffmpeg_codec_id(stream->codecpar->codec_id);
}

DecoderErrorOr<ReadonlyBytes> FFmpegDemuxer::get_codec_initialization_data_for_track(Track track)
{
    VERIFY(track.identifier() < m_format_context->nb_streams);
    auto* stream = m_format_context->streams[track.identifier()];
    return ReadonlyBytes { stream->codecpar->extradata, static_cast<size_t>(stream->codecpar->extradata_size) };
}

bool FFmpegDemuxer::is_hls_stream() const
{
    if (!m_format_context || !m_format_context->iformat)
        return false;

    // Check if the input format is HLS
    return strcmp(m_format_context->iformat->name, "hls") == 0 ||
           strcmp(m_format_context->iformat->name, "hls,applehttp") == 0;
}

bool FFmpegDemuxer::is_seekable() const
{
    if (!m_format_context)
        return false;

    // Check if the format context is marked as unseekable
    if (m_format_context->ctx_flags & AVFMTCTX_UNSEEKABLE)
        return false;

    // For HLS streams, seeking might be limited but possible
    if (is_hls_stream())
        return true; // HLS streams can seek within buffered segments

    return true; // Most formats are seekable
}

DecoderErrorOr<CodedFrame> FFmpegDemuxer::get_next_sample_for_track(Track track)
{
    VERIFY(track.identifier() < m_format_context->nb_streams);
    auto* stream = m_format_context->streams[track.identifier()];

    for (;;) {
        auto read_frame_error = av_read_frame(m_format_context, m_packet);
        if (read_frame_error < 0) {
            if (read_frame_error == AVERROR_EOF)
                return DecoderError::format(DecoderErrorCategory::EndOfStream, "End of stream");

            return DecoderError::format(DecoderErrorCategory::Unknown, "Failed to read frame");
        }
        if (m_packet->stream_index != stream->index) {
            av_packet_unref(m_packet);
            continue;
        }

        auto color_primaries = static_cast<ColorPrimaries>(stream->codecpar->color_primaries);
        auto transfer_characteristics = static_cast<TransferCharacteristics>(stream->codecpar->color_trc);
        auto matrix_coefficients = static_cast<MatrixCoefficients>(stream->codecpar->color_space);
        auto color_range = [stream] {
            switch (stream->codecpar->color_range) {
            case AVColorRange::AVCOL_RANGE_MPEG:
                return VideoFullRangeFlag::Studio;
            case AVColorRange::AVCOL_RANGE_JPEG:
                return VideoFullRangeFlag::Full;
            default:
                return VideoFullRangeFlag::Unspecified;
            }
        }();

        // Copy the packet data so that we have a permanent reference to it whilst the Sample is alive, which allows us
        // to wipe the packet afterwards.
        auto packet_data = DECODER_TRY_ALLOC(ByteBuffer::copy(m_packet->data, m_packet->size));

        auto sample = CodedFrame(
            time_units_to_duration(m_packet->pts, stream->time_base),
            move(packet_data),
            CodedVideoFrameData(CodingIndependentCodePoints(color_primaries, transfer_characteristics, matrix_coefficients, color_range)));

        // Wipe the packet now that the data is safe.
        av_packet_unref(m_packet);
        return sample;
    }
}

}

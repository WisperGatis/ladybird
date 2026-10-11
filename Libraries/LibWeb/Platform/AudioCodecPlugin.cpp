/*
 * Copyright (c) 2023, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Time.h>
#include <AK/Math.h>
#include <math.h>
#include <LibMedia/Audio/Loader.h>
#include <LibMedia/Audio/Sample.h>
#include <LibWeb/Platform/AudioCodecPlugin.h>

namespace Web::Platform {

static AudioCodecPlugin::AudioCodecPluginCreator s_creation_hook;

AudioCodecPlugin::AudioCodecPlugin() = default;
AudioCodecPlugin::~AudioCodecPlugin() = default;

void AudioCodecPlugin::install_creation_hook(AudioCodecPluginCreator creation_hook)
{
    VERIFY(!s_creation_hook);
    s_creation_hook = move(creation_hook);
}

ErrorOr<NonnullOwnPtr<AudioCodecPlugin>> AudioCodecPlugin::create(NonnullRefPtr<Audio::Loader> loader)
{
    VERIFY(s_creation_hook);
    return s_creation_hook(move(loader));
}

ErrorOr<FixedArray<Audio::Sample>> AudioCodecPlugin::read_samples_from_loader(Audio::Loader& loader, size_t samples_to_load)
{
    auto buffer_or_error = loader.get_more_samples(samples_to_load);
    if (buffer_or_error.is_error()) {
        dbgln("Error while loading samples: {}", buffer_or_error.error());
        return Error::from_string_literal("Error while loading samples");
    }

    return buffer_or_error.release_value();
}

AK::Duration AudioCodecPlugin::set_loader_position(Audio::Loader& loader, double position, AK::Duration duration)
{
    // A stream can expose metadata before its duration is known (notably live
    // HLS). Never divide by a zero/unknown duration or feed an invalid sample
    // index into the decoder.
    if (loader.total_samples() == 0 || duration.to_nanoseconds() <= 0 || !std::isfinite(position))
        return current_loader_position(loader);

    auto duration_value = static_cast<double>(duration.to_milliseconds()) / 1000.0;
    if (duration_value <= 0.0 || !std::isfinite(duration_value))
        return current_loader_position(loader);

    position = position / duration_value * static_cast<double>(loader.total_samples() - 1);
    position = clamp(position, 0.0, static_cast<double>(loader.total_samples() - 1));

    if (auto result = loader.seek(static_cast<int>(position)); result.is_error()) {
        // Some streams (notably live or partially buffered HLS streams) can
        // reject a seek. A failed media seek must not terminate the audio
        // thread through release_value_but_fixme_should_propagate_errors().
        dbgln("Error while seeking audio: {}", result.error());
        return current_loader_position(loader);
    }

    return current_loader_position(loader);
}

AK::Duration AudioCodecPlugin::current_loader_position(Audio::Loader const& loader)
{
    auto samples_played = static_cast<double>(loader.loaded_samples());
    auto sample_rate = static_cast<double>(loader.sample_rate());

    return AK::Duration::from_milliseconds(static_cast<i64>(samples_played / sample_rate * 1000.0));
}

}

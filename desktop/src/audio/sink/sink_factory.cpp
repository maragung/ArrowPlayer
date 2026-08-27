// SPDX-License-Identifier: MPL-2.0
#include "audio/sink/sink_factory.hpp"

#include "audio/sink/null_sink.hpp"

#if defined(ECLIPSE_HAVE_ALSA)
#include "audio/sink/alsa_sink.hpp"
#endif

namespace eclipse::audio {

Result<std::unique_ptr<IAudioSink>> make_sink(const SinkFactoryOptions options) {
    switch (options.preference) {
        case SinkPreference::Null:
            return std::unique_ptr<IAudioSink>{std::make_unique<NullSink>()};
        case SinkPreference::Alsa:
#if defined(ECLIPSE_HAVE_ALSA)
            return std::unique_ptr<IAudioSink>{std::make_unique<AlsaSink>()};
#else
            return err(ErrorCode::DeviceNotFound,
                       "The ALSA sink is not available in this build.");
#endif
        case SinkPreference::Automatic:
#if defined(ECLIPSE_HAVE_ALSA)
            return std::unique_ptr<IAudioSink>{std::make_unique<AlsaSink>()};
#else
            if (options.allow_null_fallback) {
                return std::unique_ptr<IAudioSink>{std::make_unique<NullSink>()};
            }
            return err(ErrorCode::DeviceNotFound, "No audio sink is available in this build.");
#endif
    }
    return err(ErrorCode::InvalidArgument, "The audio sink preference is invalid.");
}

}  // namespace eclipse::audio

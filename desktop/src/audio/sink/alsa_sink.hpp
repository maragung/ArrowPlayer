// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "audio/ports/audio_ports.hpp"

#if defined(ECLIPSE_HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace eclipse::audio {

class AlsaSink final : public IAudioSink {
  public:
    [[nodiscard]] Result<SinkConfig> open(const SinkConfig& requested) override;
    [[nodiscard]] Status start() override;
    void stop() noexcept override;
    void close() noexcept override;
    [[nodiscard]] Status write(PlanarFrames frames) noexcept override;

    [[nodiscard]] std::string_view device_name() const noexcept override { return "default"; }

  private:
#if defined(ECLIPSE_HAVE_ALSA)
    snd_pcm_t* handle_{nullptr};
#endif
    SinkConfig config_{};
    bool opened_{false};
};

}  // namespace eclipse::audio

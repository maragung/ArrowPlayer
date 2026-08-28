// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Internet radio client — spec §17.1, REQ-NET-010 .. REQ-NET-015.
//
// The implementation is a single-threaded demuxer that:
//   1. issues a GET through IHttpClient with `Icy-MetaData: 1` to provoke
//      the ICY handshake for raw streams, or fetches the .m3u8 master and
//      picks a variant for HLS streams (§17.1, REQ-NET-010);
//   2. walks the body bytes, splitting the audio payload from the inline
//      ICY metadata blocks at the icy-metaint boundary (REQ-NET-011);
//   3. surfaces StreamTitle changes as on_track events and station info
//      (icy-name, icy-genre, icy-br, icy-url) as station metadata;
//   4. applies the §17.1.4 / REQ-NET-013 reconnect ladder on I/O errors.
//
// Concurrency: the radio client is driven from one thread (the I/O thread
// the host scheduler assigns to it). The IHttpClient call is synchronous
// from this thread's point of view; cancellation comes from the
// CancellationToken the host passed to open().

#include "net/ports/radio_port.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "net/ports/hls_playlist.hpp"
#include "net/ports/http_port.hpp"
#include "net/ports/icy_metadata.hpp"

namespace arrow::net {

namespace {

}  // namespace

// ---------------------------------------------------------------------------
//  Station playlist parsing (.pls, .m3u)
// ---------------------------------------------------------------------------

Result<std::vector<RadioStation>> parse_station_playlist(
    std::string_view text, std::string_view content_type) {
    std::vector<RadioStation> out;
    if (text.empty()) {
        return err(ErrorCode::MalformedHeader,
                   "Empty station playlist",
                   "the body was empty");
    }
    // The content-type hint is best-effort; we sniff the leading text.
    const bool looks_like_pls = text.find("[playlist]") != std::string_view::npos ||
                                text.find("[PLAYLIST]") != std::string_view::npos ||
                                text.find("File1=") != std::string_view::npos;
    const bool looks_like_m3u = text.substr(0, 1) == "#" ||
                                text.find("#EXTM3U") != std::string_view::npos;
    if (looks_like_pls) {
        for (std::size_t i = 0; i < 100; ++i) {
            const std::string key = "File" + std::to_string(i + 1) + "=";
            const std::size_t pos = text.find(key);
            if (pos == std::string::npos) break;
            const std::size_t end = text.find('\n', pos);
            std::string_view line = text.substr(pos + key.size(),
                                                end == std::string_view::npos
                                                    ? std::string_view::npos
                                                    : end - pos - key.size());
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.remove_suffix(1);
            }
            RadioStation s;
            s.url.assign(line);
            const std::string title_key = "Title" + std::to_string(i + 1) + "=";
            const std::size_t tpos = text.find(title_key);
            if (tpos != std::string::npos) {
                const std::size_t tend = text.find('\n', tpos);
                std::string_view t = text.substr(tpos + title_key.size(),
                                                 tend == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : tend - tpos - title_key.size());
                while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) {
                    t.remove_suffix(1);
                }
                s.name.assign(t);
            }
            out.push_back(std::move(s));
        }
    } else if (looks_like_m3u) {
        for (std::size_t pos = 0; pos < text.size();) {
            const std::size_t nl = text.find('\n', pos);
            std::string_view line = text.substr(pos,
                                                nl == std::string_view::npos
                                                    ? std::string_view::npos
                                                    : nl - pos);
            pos = nl == std::string_view::npos ? text.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.remove_suffix(1);
            }
            if (line.empty() || line[0] == '#') continue;
            RadioStation s;
            s.url.assign(line);
            out.push_back(std::move(s));
        }
    } else {
        return err(ErrorCode::MalformedHeader,
                   "Unrecognised station playlist",
                   "expected .pls or .m3u content");
    }
    if (out.empty()) {
        return err(ErrorCode::MalformedHeader,
                   "Station playlist contains no entries",
                   "no File1= or URL line was found");
    }
    for (auto& s : out) {
        const std::size_t scheme_end = s.url.find("://");
        if (scheme_end != std::string::npos) {
            s.is_https = s.url.compare(0, scheme_end, "https") == 0;
        }
    }
    (void)content_type;
    return out;
}

// ---------------------------------------------------------------------------
//  IRadioClient implementation
// ---------------------------------------------------------------------------

class RadioClient final : public IRadioClient {
  public:
    RadioClient() = default;
    ~RadioClient() override {
        if (active_) {
            cancel_token_.cancel();
        }
    }

    Status open(const RadioStation& station, IHttpClient& http,
                const RadioCallbacks& callbacks) override {
        station_ = station;
        http_ = &http;
        callbacks_ = callbacks;
        cancel_token_ = CancellationToken{};
        active_ = true;
        attempt_ = 0;

        // The connect path depends on the URL: an HLS master is a small
        // playlist we fetch once and then iterate segments. Everything else
        // is a raw stream with the ICY handshake.
        if (station.url.size() >= 5 &&
            station.url.substr(station.url.size() - 5) == ".m3u8") {
            return run_hls();
        }
        return run_icy();
    }

    void close() noexcept override {
        if (active_) {
            active_ = false;
            cancel_token_.cancel();
        }
        set_state(RadioState::Idle);
    }

    void pause() noexcept override {
        if (state_ == RadioState::Playing) set_state(RadioState::Paused);
    }

    void resume() noexcept override {
        if (state_ == RadioState::Paused) set_state(RadioState::Playing);
    }

    [[nodiscard]] RadioState state() const noexcept override {
        return state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<RadioTrack> current_track() const noexcept override {
        std::lock_guard<std::mutex> lock(track_mutex_);
        if (last_track_.title.empty() && last_track_.artist.empty() &&
            last_track_.station_name.empty()) {
            return std::nullopt;
        }
        return last_track_;
    }

  private:
    void set_state(RadioState s) noexcept {
        const RadioState prev = state_.exchange(s, std::memory_order_acq_rel);
        if (prev != s && callbacks_.on_state) {
            try {
                callbacks_.on_state(s);
            } catch (...) {
                // Callback exceptions are never propagated to the radio
                // engine — the §1.3 robustness rule says an exception in
                // user code must not take down the audio path.
            }
        }
    }

    Status run_icy() {
        set_state(RadioState::Connecting);
        HttpRequest req;
        req.method = HttpMethod::Get;
        req.url = station_.url;
        // Allow http:// only when the URL itself was http; this gates
        // REQ-NET-003 together with the port's policy.
        req.allow_insecure = !station_.is_https;
        HttpHeader icy_header{"Icy-MetaData", "1"};
        req.headers.push_back(icy_header);
        req.timeout = std::chrono::seconds{15};

        // We use a streaming read by asking for a HEAD, no — libcurl is
        // fully buffered by the IHttpClient today. For a complete
        // implementation we'd switch to a chunked callback path, but
        // that would force the port to expose more of libcurl's surface
        // than it should. Instead, we read the body as a single buffer
        // and demux it in memory; the radio client is a demuxer, not a
        // byte pump, and the host pumps bytes in via the IHttpClient.
        // The connect timeout (REQ-NET-012) is honored by the port.
        auto result = http_->send(req, cancel_token_);
        if (cancel_token_.cancelled()) {
            return ok();
        }
        if (!result.has_value()) {
            return schedule_reconnect(result.error());
        }
        HttpResponse resp = std::move(result).value();
        // ICY responses come back without a real Content-Type; libcurl
        // sometimes maps it to application/octet-stream. Pull the station
        // headers straight from the response.
        for (const auto& hdr : resp.headers) {
            if (hdr.name == "icy-name" || hdr.name == "icy-name ") {
                station_.name = hdr.value;
            } else if (hdr.name == "icy-genre") {
                station_.genre = hdr.value;
            } else if (hdr.name == "icy-br") {
                int br = 0;
                for (char c : hdr.value) {
                    if (c < '0' || c > '9') break;
                    br = br * 10 + (c - '0');
                }
                station_.bitrate_kbps = br;
            } else if (hdr.name == "icy-url") {
                station_.home_page = hdr.value;
            } else if (hdr.name == "icy-metaint") {
                int n = 0;
                for (char c : hdr.value) {
                    if (c < '0' || c > '9') break;
                    n = n * 10 + (c - '0');
                }
                metaint_ = n;
            }
        }
        if (metaint_ <= 0) {
            // Not an ICY stream — push the body to the audio engine whole.
            set_state(RadioState::Buffering);
            if (callbacks_.on_audio && !resp.body.empty()) {
                callbacks_.on_audio(
                    reinterpret_cast<const std::uint8_t*>(resp.body.data()),
                    resp.body.size());
            }
            set_state(RadioState::Playing);
            return ok();
        }
        set_state(RadioState::Buffering);
        // Demux the body in place.
        const std::uint8_t* begin =
            reinterpret_cast<const std::uint8_t*>(resp.body.data());
        std::size_t remaining = resp.body.size();
        std::size_t next_meta = static_cast<std::size_t>(metaint_);
        std::size_t audio_offset = 0;
        while (remaining > 0 && !cancel_token_.cancelled()) {
            const std::size_t audio_len =
                std::min(remaining, next_meta - audio_offset);
            (void)audio_offset;
            if (audio_len > 0 && callbacks_.on_audio) {
                callbacks_.on_audio(begin, audio_len);
            }
            begin += audio_len;
            remaining -= audio_len;
            audio_offset = 0;
            next_meta = static_cast<std::size_t>(metaint_);
            if (remaining == 0) break;
            // The next byte is the metadata block length times 16.
            const std::uint8_t len_byte = begin[0];
            const std::size_t meta_len = static_cast<std::size_t>(len_byte) * 16U;
            --remaining;
            ++begin;
            if (remaining < meta_len) break;  // truncated stream
            std::string_view block(reinterpret_cast<const char*>(begin), meta_len);
            auto parsed = parse_icy_block(block);
            if (parsed.has_value()) {
                const IcyTrack t = split_icy_title(parsed->stream_title);
                std::lock_guard<std::mutex> lock(track_mutex_);
                last_track_.artist = t.artist;
                last_track_.title = t.title;
                last_track_.station_name = station_.name;
                if (callbacks_.on_track) {
                    try {
                        callbacks_.on_track(last_track_);
                    } catch (...) {
                    }
                }
            }
            begin += meta_len;
            remaining -= meta_len;
        }
        set_state(RadioState::Playing);
        return ok();
    }

    Status run_hls() {
        set_state(RadioState::Connecting);
        // Fetch the master playlist.
        HttpRequest master_req;
        master_req.method = HttpMethod::Get;
        master_req.url = station_.url;
        master_req.allow_insecure = !station_.is_https;
        auto master_result = http_->send(master_req, cancel_token_);
        if (cancel_token_.cancelled()) return ok();
        if (!master_result.has_value()) {
            return schedule_reconnect(master_result.error());
        }
        const HttpResponse master_resp = std::move(master_result).value();
        auto master = parse_hls_master(master_resp.body, station_.url);
        if (!master.has_value()) {
            if (callbacks_.on_error) callbacks_.on_error(master.error());
            return err(master.error().code(),
                       "HLS master playlist could not be parsed",
                       master.error().technical_detail());
        }
        const HlsVariant* variant = pick_hls_variant(master.value(), 256'000);
        if (variant == nullptr) {
            return err(ErrorCode::MalformedHeader,
                       "HLS master playlist has no usable variant",
                       "pick_hls_variant returned null");
        }
        set_state(RadioState::Buffering);
        for (const auto& seg : master.value().variants) {
            if (&seg != variant) continue;
            (void)seg;
            break;
        }
        // We do not actually fetch each segment here — that would require a
        // background thread and a sink, and both live in the audio engine.
        // The radio client surfaces the HLS variant URL as the active stream
        // and the audio engine fetches each segment. For this milestone we
        // stop at "the variant was selected" so callers can drive the loop.
        station_.url = variant->uri;
        set_state(RadioState::Playing);
        return ok();
    }

    Status schedule_reconnect(const Error& cause) {
        if (callbacks_.on_error) {
            try {
                callbacks_.on_error(cause);
            } catch (...) {
            }
        }
        // The ladder is 1, 2, 5, 10, 30 s; the spec does not name an upper
        // bound, so we cap by holding at 30 s.
        const std::size_t idx = std::min<std::size_t>(attempt_, kReconnectBackoff.size() - 1);
        const auto wait = kReconnectBackoff[idx];
        ++attempt_;
        set_state(RadioState::Reconnecting);
        (void)wait;  // the host's main loop owns the timer
        return err(cause.code(), "radio stream disconnected, will reconnect",
                   cause.technical_detail());
    }

    RadioStation station_{};
    IHttpClient* http_{nullptr};
    RadioCallbacks callbacks_{};
    CancellationToken cancel_token_{};
    std::atomic<RadioState> state_{RadioState::Idle};
    std::atomic<bool> active_{false};
    std::size_t attempt_{0};
    int metaint_{0};
    RadioTrack last_track_{};
    mutable std::mutex track_mutex_;
};

std::unique_ptr<IRadioClient> make_default_radio_client() {
    return std::make_unique<RadioClient>();
}

}  // namespace arrow::net

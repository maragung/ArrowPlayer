// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// HLS playlist parser — spec §17.1, REQ-NET-010 / REQ-NET-011.
//
// The parser implements the parts of RFC 8216 the radio client uses:
// master playlists to pick a variant, and media playlists to walk a list of
// segments. It does not implement EXT-X-KEY (encrypted variants are rejected
// with a typed error) or EXT-X-BYTERANGE (byte-range segments are also
// rejected), so the radio client can surface "this stream is encrypted" or
// "this stream uses byte ranges we cannot play" rather than guessing.

#include "net/ports/hls_playlist.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arrow::net {

namespace {

std::string_view strip(std::string_view s) noexcept {
    std::size_t begin = 0;
    while (begin < s.size() &&
           (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string_view strip_quoted(std::string_view s) noexcept {
    s = strip(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

/// Parse the comma-separated attribute list of a #EXT-X-STREAM-INF line.
struct StreamInfAttrs {
    std::int64_t bandwidth{0};
    std::int64_t average_bandwidth{0};
    std::string codecs;
    std::string resolution;
    std::optional<double> frame_rate;
};

std::optional<StreamInfAttrs> parse_stream_inf(std::string_view attrs) {
    StreamInfAttrs out;
    bool saw_bandwidth = false;
    while (!attrs.empty()) {
        const std::size_t comma = attrs.find(',');
        std::string_view part = strip(comma == std::string_view::npos
                                          ? attrs
                                          : attrs.substr(0, comma));
        attrs = comma == std::string_view::npos ? std::string_view{}
                                                : attrs.substr(comma + 1);
        if (part.empty()) continue;
        const std::size_t eq = part.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = strip(part.substr(0, eq));
        std::string_view value = strip_quoted(part.substr(eq + 1));
        if (ieq(key, "BANDWIDTH")) {
            out.bandwidth = 0;
            for (char c : value) {
                if (c < '0' || c > '9') return std::nullopt;
                out.bandwidth = out.bandwidth * 10 + (c - '0');
            }
            saw_bandwidth = true;
        } else if (ieq(key, "AVERAGE-BANDWIDTH")) {
            out.average_bandwidth = 0;
            for (char c : value) {
                if (c < '0' || c > '9') return std::nullopt;
                out.average_bandwidth = out.average_bandwidth * 10 + (c - '0');
            }
        } else if (ieq(key, "CODECS")) {
            out.codecs.assign(value);
        } else if (ieq(key, "RESOLUTION")) {
            out.resolution.assign(value);
        } else if (ieq(key, "FRAME-RATE")) {
            // Allow "30" or "29.97".
            double rate = 0.0;
            bool dot = false;
            double div = 1.0;
            for (char c : value) {
                if (c == '.') {
                    if (dot) return std::nullopt;
                    dot = true;
                    continue;
                }
                if (c < '0' || c > '9') return std::nullopt;
                if (!dot) {
                    rate = rate * 10.0 + (c - '0');
                } else {
                    div *= 10.0;
                    rate += (c - '0') / div;
                }
            }
            if (rate > 0.0) out.frame_rate = rate;
        }
    }
    if (!saw_bandwidth) return std::nullopt;
    return out;
}

/// Resolve a possibly-relative URI against a base URL per RFC 3986 §5.
/// We only need the basic cases: absolute URI in `ref` wins; otherwise the
/// last path component of `base` is replaced with `ref`; a leading "/" means
/// "swap the path of base" while a non-leading one means "append to the
/// directory of base".
std::string resolve_uri(std::string_view base, std::string_view ref) {
    if (ref.empty()) return std::string{base};
    // Absolute (has a scheme).
    if (ref.find("://") != std::string_view::npos) return std::string{ref};
    // Protocol-relative "//host/...": borrow scheme from base.
    if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/') {
        const std::size_t scheme_end = base.find("://");
        if (scheme_end == std::string_view::npos) return std::string{ref};
        std::string out{base.substr(0, scheme_end + 3)};
        out.append(ref.substr(2));
        return out;
    }
    // Find scheme in base.
    const std::size_t scheme_end = base.find("://");
    if (scheme_end == std::string_view::npos) return std::string{ref};
    const std::size_t host_start = scheme_end + 3;
    const std::size_t path_start = base.find('/', host_start);
    std::string out{base.substr(0, path_start == std::string_view::npos
                                       ? base.size()
                                       : path_start)};
    if (ref[0] == '/') {
        out.append(ref);
        return out;
    }
    // Append to the directory of base.
    if (path_start == std::string_view::npos) {
        out.push_back('/');
        out.append(ref);
        return out;
    }
    const std::size_t last_slash = base.find_last_of('/');
    if (last_slash < path_start) {
        out.push_back('/');
        out.append(ref);
        return out;
    }
    out.append(base.substr(path_start, last_slash - path_start + 1));
    out.append(ref);
    return out;
}

std::optional<std::int64_t> parse_int_attr(std::string_view value) {
    std::int64_t v = 0;
    if (value.empty()) return std::nullopt;
    for (char c : value) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + (c - '0');
    }
    return v;
}

std::optional<double> parse_double_attr(std::string_view value) {
    if (value.empty()) return std::nullopt;
    double v = 0.0;
    bool dot = false;
    double div = 1.0;
    for (char c : value) {
        if (c == '.') {
            if (dot) return std::nullopt;
            dot = true;
            continue;
        }
        if (c < '0' || c > '9') return std::nullopt;
        if (!dot) {
            v = v * 10.0 + (c - '0');
        } else {
            div *= 10.0;
            v += (c - '0') / div;
        }
    }
    return v;
}

}  // namespace

Result<HlsMasterPlaylist> parse_hls_master(std::string_view text,
                                           std::string_view base_url) {
    HlsMasterPlaylist out;
    const std::size_t header_end = text.find('\n');
    std::string_view first = strip(text.substr(0, header_end));
    if (!ieq(first, "#EXTM3U")) {
        return err(ErrorCode::MalformedHeader,
                   "HLS playlist is missing the #EXTM3U header",
                   "first line was not #EXTM3U");
    }
    if (header_end == std::string_view::npos) return out;
    text.remove_prefix(header_end + 1);

    std::optional<StreamInfAttrs> pending;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = strip(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty()) continue;

        if (line.substr(0, 12) == "#EXT-X-VERSION") {
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                if (auto v = parse_int_attr(strip(line.substr(colon + 1)))) {
                    out.version = static_cast<int>(*v);
                }
            }
            continue;
        }
        if (line.substr(0, 18) == "#EXT-X-STREAM-INF") {
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                return err(ErrorCode::MalformedHeader,
                           "HLS #EXT-X-STREAM-INF missing attribute list",
                           "expected colon after the tag");
            }
            auto attrs = parse_stream_inf(line.substr(colon + 1));
            if (!attrs) {
                return err(ErrorCode::MalformedHeader,
                           "HLS #EXT-X-STREAM-INF attributes malformed",
                           "could not parse BANDWIDTH");
            }
            pending = *attrs;
            continue;
        }
        if (!line.empty() && line[0] != '#') {
            if (!pending) {
                return err(ErrorCode::MalformedHeader,
                           "HLS media URI without preceding #EXT-X-STREAM-INF",
                           "variant URI must follow its STREAM-INF line");
            }
            HlsVariant v;
            v.bandwidth_bps = pending->bandwidth;
            v.average_bandwidth_bps = pending->average_bandwidth;
            v.codecs = std::move(pending->codecs);
            v.resolution = std::move(pending->resolution);
            v.frame_rate = pending->frame_rate;
            v.uri = resolve_uri(base_url, line);
            out.variants.push_back(std::move(v));
            pending.reset();
            continue;
        }
        // All other tags (EXT-X-MEDIA, EXT-X-I-FRAME-STREAM-INF, etc.) are
        // ignored. They are valid master playlist content but not needed to
        // pick a variant; the radio client falls back to bandwidth only.
    }
    if (out.variants.empty()) {
        return err(ErrorCode::MalformedHeader,
                   "HLS master playlist has no variants",
                   "no #EXT-X-STREAM-INF / URI pair was found");
    }
    return out;
}

Result<HlsMediaPlaylist> parse_hls_media(std::string_view text,
                                         std::string_view base_url) {
    HlsMediaPlaylist out;
    const std::size_t header_end = text.find('\n');
    std::string_view first = strip(text.substr(0, header_end));
    if (!ieq(first, "#EXTM3U")) {
        return err(ErrorCode::MalformedHeader,
                   "HLS playlist is missing the #EXTM3U header",
                   "first line was not #EXTM3U");
    }
    if (header_end == std::string_view::npos) return out;
    text.remove_prefix(header_end + 1);

    std::int64_t pending_duration_s = 0;
    std::optional<std::string> pending_title;
    bool have_pending = false;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = strip(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty()) continue;

        if (line.substr(0, 12) == "#EXT-X-VERSION") {
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                if (auto v = parse_int_attr(strip(line.substr(colon + 1)))) {
                    out.version = static_cast<int>(*v);
                }
            }
            continue;
        }
        if (line.substr(0, 21) == "#EXT-X-MEDIA-SEQUENCE") {
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                if (auto v = parse_int_attr(strip(line.substr(colon + 1)))) {
                    out.media_sequence = *v;
                }
            }
            continue;
        }
        if (line.substr(0, 19) == "#EXT-X-TARGETDURATION") {
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                if (auto v = parse_int_attr(strip(line.substr(colon + 1)))) {
                    out.target_duration = *v;
                }
            }
            continue;
        }
        if (line.substr(0, 8) == "#EXTINF:") {
            // Format: "#EXTINF:<duration>[,<title>]". Duration may be a float
            // in v3+ but for variant selection we only need an integer second
            // granularity; sub-second precision is silently truncated.
            const std::string_view value = strip(line.substr(8));
            const std::size_t comma = value.find(',');
            std::string_view dur = comma == std::string_view::npos
                                       ? value
                                       : value.substr(0, comma);
            if (auto d = parse_double_attr(dur)) {
                pending_duration_s = static_cast<std::int64_t>(*d);
            } else {
                pending_duration_s = 0;
            }
            if (comma != std::string_view::npos) {
                pending_title = std::string{strip(value.substr(comma + 1))};
            } else {
                pending_title.reset();
            }
            have_pending = true;
            continue;
        }
        if (line == "#EXT-X-ENDLIST") {
            out.endlist = true;
            have_pending = false;
            continue;
        }
        if (line.substr(0, 11) == "#EXT-X-KEY:") {
            return err(ErrorCode::NotImplemented,
                       "HLS encrypted variant is not supported",
                       "EXT-X-KEY indicates AES-128 or SAMPLE-AES, which the "
                       "radio client does not decode");
        }
        if (line.substr(0, 16) == "#EXT-X-BYTERANGE:") {
            return err(ErrorCode::NotImplemented,
                       "HLS byte-range segments are not supported",
                       "EXT-X-BYTERANGE indicates partial segments");
        }
        if (!line.empty() && line[0] != '#') {
            HlsSegment seg;
            seg.uri = resolve_uri(base_url, line);
            seg.duration = std::chrono::seconds{pending_duration_s};
            seg.title = pending_title;
            seg.sequence = out.media_sequence +
                           static_cast<std::int64_t>(out.segments.size());
            out.segments.push_back(std::move(seg));
            have_pending = false;
            continue;
        }
    }
    if (out.segments.empty()) {
        return err(ErrorCode::MalformedHeader,
                   "HLS media playlist has no segments",
                   "expected at least one #EXTINF followed by a URI");
    }
    (void)have_pending;
    return out;
}

const HlsVariant* pick_hls_variant(const HlsMasterPlaylist& master,
                                   std::int64_t max_bandwidth_bps) noexcept {
    if (master.variants.empty()) return nullptr;
    const HlsVariant* best = nullptr;
    for (const auto& v : master.variants) {
        if (v.bandwidth_bps > max_bandwidth_bps) continue;
        if (best == nullptr || v.bandwidth_bps > best->bandwidth_bps) {
            best = &v;
        }
    }
    if (best != nullptr) return best;
    // Every variant exceeds the cap: return the smallest one rather than
    // nothing, so the user gets a stream over no stream.
    const HlsVariant* smallest = &master.variants.front();
    for (const auto& v : master.variants) {
        if (v.bandwidth_bps < smallest->bandwidth_bps) smallest = &v;
    }
    return smallest;
}

}  // namespace arrow::net

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// ICY (Icecast / Shoutcast) metadata parser — spec §17.1, REQ-NET-011.
//
// The stream is a plain HTTP/1.1 byte pipe; metadata is interleaved every
// `icy-metaint` bytes as a length-prefixed block whose payload is
// "StreamTitle='Artist - Title';StreamURL='…';". This file decodes one
// block; the radio client is responsible for the byte-level framing.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.hpp"

namespace arrow::net {

/// A single ICY metadata block.
struct IcyMetadata final {
    std::string stream_title;       ///< raw "Artist - Title" (may be empty)
    std::optional<std::string> stream_url;
    /// Map of every other StreamXXX= key, lower-cased. "StreamTitle" and
    /// "StreamURL" are also present here, so callers never have to look in
    /// two places.
    std::string raw;
};

/// Parse one metadata block. `block` is the raw bytes after the length byte,
/// already split off the stream. Returns the decoded fields, or an Error
/// with code ErrorCode::MalformedHeader if the block cannot be decoded.
[[nodiscard]] Result<IcyMetadata> parse_icy_block(std::string_view block);

/// Pull "Artist - Title" out of a StreamTitle. The dash is the first ' - '
/// (U+0020, U+002D, U+0020) — ICY uses a fixed ASCII dash, not the em-dash
/// the spec uses in its prose. Returns the whole title if no separator is
/// present. Either side may be empty ("- Title", "Artist -").
struct IcyTrack final {
    std::string artist;
    std::string title;
};
[[nodiscard]] IcyTrack split_icy_title(std::string_view stream_title);

}  // namespace arrow::net

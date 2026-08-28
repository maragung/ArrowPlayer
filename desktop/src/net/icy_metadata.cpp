// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// ICY (Icecast / Shoutcast) metadata parser — spec §17.1, REQ-NET-011.
//
// The wire format the radio client hands us is the bytes after the single
// length byte in the metaint block: the text is "Key1='value with single
// quotes escaped as \\' ';Key2='…';…". The first character of the block is
// NOT the length byte — that is stripped by the caller, since the framing
// (read N bytes, read 1 byte, read M bytes where M is the value of the byte
// times 16) belongs to the network reader, not the metadata decoder.

#include "net/ports/icy_metadata.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace arrow::net {

namespace {

/// Skip whitespace per ICY convention: spaces and tabs (RFC 821 §2 spec
/// describes the metadata as "ASCII text" but the de-facto wire format uses
/// only those two).
constexpr bool is_icy_space(char c) noexcept {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view s) noexcept {
    std::size_t begin = 0;
    while (begin < s.size() && is_icy_space(s[begin])) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && is_icy_space(s[end - 1])) {
        --end;
    }
    return s.substr(begin, end - begin);
}

}  // namespace

Result<IcyMetadata> parse_icy_block(std::string_view block) {
    IcyMetadata out;
    out.raw.assign(block);

    while (!block.empty()) {
        // Each "Key='value';" segment ends at the next ';' that is not inside
        // a quoted value. We track quote state for the escape rule: a single
        // quote inside the value is escaped as "\\' \\'" per the SHOUTcast
        // DNAS extension. The official Icecast format does not allow the
        // quote at all, so this is a "be liberal in what you accept" path
        // for the rare DNAS deployment.
        std::size_t eq = std::string_view::npos;
        bool in_quote = false;
        std::size_t seg_end = std::string_view::npos;
        for (std::size_t i = 0; i < block.size(); ++i) {
            const char c = block[i];
            if (in_quote) {
                if (c == '\'' && i + 1 < block.size() && block[i + 1] == '\'') {
                    // Doubled quote: a literal apostrophe. Consume the second.
                    ++i;
                } else if (c == '\'') {
                    in_quote = false;
                }
                continue;
            }
            if (c == '=' && eq == std::string_view::npos) {
                eq = i;
            } else if (c == '\'') {
                in_quote = true;
            } else if (c == ';') {
                seg_end = i;
                break;
            }
        }
        if (eq == std::string_view::npos) {
            return err(ErrorCode::MalformedHeader,
                       "ICY metadata block missing '='",
                       "block did not contain any key=value segment");
        }
        if (seg_end == std::string_view::npos) {
            seg_end = block.size();
        }
        if (seg_end <= eq + 2 || block[eq + 1] != '\'') {
            return err(ErrorCode::MalformedHeader,
                       "ICY metadata value not quoted",
                       "expected Key='value' form");
        }
        std::string_view key = trim(block.substr(0, eq));
        std::string_view value = block.substr(eq + 2, seg_end - eq - 3);

        // Un-escape doubled single quotes inside the value.
        std::string unescaped;
        unescaped.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
                unescaped.push_back('\'');
                ++i;
            } else {
                unescaped.push_back(value[i]);
            }
        }

        // The two named fields go in their typed slots. Everything else is
        // surfaced only through `raw`, because the radio client cares about
        // the named ones and REQ-NET-011 names exactly those.
        if (key == "StreamTitle") {
            out.stream_title = std::move(unescaped);
        } else if (key == "StreamURL") {
            out.stream_url = std::move(unescaped);
        }
        if (seg_end >= block.size()) {
            break;
        }
        block.remove_prefix(seg_end + 1);
    }

    return out;
}

IcyTrack split_icy_title(std::string_view stream_title) {
    // The de-facto separator is exactly " - " (space, ASCII hyphen, space).
    // Searches that allow em-dash or en-dash would match nothing in practice
    // and would surprise any code that already accepts the ASCII form.
    constexpr std::string_view sep = " - ";
    const std::size_t pos = stream_title.find(sep);
    if (pos == std::string_view::npos) {
        return IcyTrack{std::string{}, std::string{stream_title}};
    }
    return IcyTrack{
        std::string{trim(stream_title.substr(0, pos))},
        std::string{trim(stream_title.substr(pos + sep.size()))},
    };
}

}  // namespace arrow::net

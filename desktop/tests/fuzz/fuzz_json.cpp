// SPDX-License-Identifier: MPL-2.0
/// Fuzz target — the hardened JSON parser (§21.6, REQ-SEC-011, REQ-SEC-012).
///
/// `fuzz_theme` and `fuzz_layout` in REQ-SEC-011's table both feed JSON through
/// this parser before a single schema keyword is consulted, so this target is
/// their shared foundation rather than a stand-in for either: neither can be
/// written until the theme engine exists, and both inherit whatever this finds.
///
/// Three invariants are asserted, not merely "does not crash":
///
///   1. A parse that succeeds yields a tree that can be walked in full without
///      reading out of bounds — the walk touches every node.
///   2. `dump()` of an accepted tree re-parses. The dumper decodes and re-encodes
///      UTF-8 through `text::decode_utf8`, replacing malformed sequences with
///      U+FFFD, so its output is well-formed by construction. If a round trip
///      ever fails, either the dumper emitted something the parser rejects or the
///      parser accepts something it cannot represent. Both are bugs.
///   3. `pointer()` resolves a pointer built from the tree's own keys back to a
///      node that exists — the escaping rules for `~0`/`~1` are easy to get
///      subtly wrong, and a wrong pointer is silent.
///
/// The limits are left at their defaults so the depth, element and byte guards
/// are part of what is under test (REQ-THM-017).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/json/json.hpp"

namespace {

using eclipse::json::Type;
using eclipse::json::Value;

[[noreturn]] void fail(const char* what) {
    (void)std::fprintf(stderr, "fuzz_json: invariant violated: %s\n", what);
    std::abort();
}

/// Escapes one object key into a JSON Pointer reference token (RFC 6901 §3).
std::string as_token(std::string_view key) {
    std::string token;
    token.reserve(key.size());
    for (const char c : key) {
        if (c == '~') {
            token += "~0";
        } else if (c == '/') {
            token += "~1";
        } else {
            token.push_back(c);
        }
    }
    return token;
}

/// Walks every node, and on the way down checks that each child is reachable by
/// the pointer that names it. `pointer` is threaded rather than recomputed so a
/// deep tree costs one append per level instead of a rebuild.
std::size_t walk(const Value& root, const Value& node, const std::string& pointer) {
    std::size_t count = 1;

    if (const Value* found = root.pointer(pointer); found != &node) {
        // A pointer built out of the tree's own keys must lead back to the node
        // it was built from. Anything else means pointer() and the tree disagree.
        fail("pointer() did not resolve to the node its own path names");
    }

    switch (node.type()) {
        case Type::Array: {
            std::size_t index = 0;
            for (const Value& child : node.as_array()) {
                count += walk(root, child, pointer + "/" + std::to_string(index));
                ++index;
            }
            break;
        }
        case Type::Object:
            for (const auto& [key, child] : node.as_object()) {
                count += walk(root, child, pointer + "/" + as_token(key));
            }
            break;
        case Type::String:
            (void)node.as_string();
            break;
        case Type::Number:
            (void)node.as_double();
            (void)node.as_int();
            (void)node.is_integer();
            break;
        case Type::Bool:
            (void)node.as_bool();
            break;
        case Type::Null:
            break;
    }
    return count;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    auto parsed = eclipse::json::parse(input);
    if (!parsed) {
        // A rejection must still carry a usable message: an error with an empty
        // user-facing string is how a validation failure becomes unreportable.
        if (parsed.error().user_message().empty()) {
            fail("a rejected document produced an error with no message");
        }
        return 0;
    }

    const Value& root = parsed.value();
    (void)walk(root, root, std::string{});

    const std::string dumped = root.dump();
    const eclipse::json::Limits limits;
    if (dumped.size() <= limits.max_bytes) {
        auto again = eclipse::json::parse(dumped);
        if (!again) {
            fail("dump() produced a document the parser rejects");
        }
    }
    return 0;
}

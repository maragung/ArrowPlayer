// SPDX-License-Identifier: MPL-2.0
//
// svg_sanitize.hpp — REQ-THM-042 SVG sanitisation.
//
// Spec: eclipse-player.md §11.5 (REQ-THM-040 step 7), §11.3 (REQ-THM-042).
//
// Every SVG in a skin is untrusted. The sanitiser rejects SVGs that
// contain ANY of:
//
//   * <script>                                                   (any form)
//   * <foreignObject>                                            (any form)
//   * <use> with an external reference (href / xlink:href)
//   * <image> with a non-data: href
//   * any on* event attribute
//   * any href / xlink:href that is not an internal fragment (#name)
//   * <style> with @import
//   * any external entity, or any DOCTYPE declaration
//   * more than 10,000 elements
//
// It also REFUSES to expand entities at all (the billion-laughs /
// quadratic-blowup defence). The "no entity expansion" rule is what
// REQ-THM-042 explicitly calls out: the requirement is that expansion
// never starts, not that we cap it after it has.
//
// We parse with expat (libexpat) — the same XML library Qt's SVG
// renderer uses — and reject the document on the first violation. The
// "on first violation" rule is part of the spec: a partial clean-up
// of a hostile document is not a defence, because the renderer's
// view of the document must match the sanitiser's view (REQ-THM-042
// note in the malicious/svg-not-well-formed.svg entry).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace arrow::skin {

enum class SvgError {
    None,
    ParseError,                 // not well-formed XML
    DoctypePresent,             // any DOCTYPE
    ExternalEntity,             // SYSTEM/PUBLIC external entity
    ScriptElement,              // <script>
    ForeignObject,              // <foreignObject>
    UseExternal,                // <use> with a non-fragment href
    ImageNonData,               // <image> with a non-data: href
    EventAttribute,             // any on* attribute
    StyleImport,                // <style> with @import
    NonFragmentHref,            // any href / xlink:href that is not a #fragment
    ElementCountExceeded,        // > 10,000
};

struct SvgResult {
    SvgError   error{SvgError::None};
    std::string why;
    std::size_t offending_line{0};
    std::size_t offending_column{0};
    std::string offending_element;   // element name, when relevant

    bool ok() const noexcept { return error == SvgError::None; }
};

class SvgSanitizer {
public:
    SvgSanitizer();
    ~SvgSanitizer();

    SvgSanitizer(const SvgSanitizer&)            = delete;
    SvgSanitizer& operator=(const SvgSanitizer&) = delete;

    // Maximum number of elements the sanitiser will allow (REQ-THM-042).
    static constexpr std::size_t kMaxElements = 10'000;

    // Sanitise the SVG document in `source`. On success, `clean` is
    // filled with the document bytes verbatim (this implementation
    // does not rewrite; it either accepts the whole document or
    // refuses it). On failure, the result names the violation.
    SvgResult sanitise(std::string_view source, std::string& clean);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace arrow::skin

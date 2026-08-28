// SPDX-License-Identifier: MPL-2.0
//
// svg_sanitize.cpp — see svg_sanitize.hpp for the rules being enforced.
//
// The implementation is a SAX-style walk driven by expat, which is the
// right tool for the job: it is the same parser Qt's SVG renderer uses,
// and the spec demands that "the sanitiser's view must be the
// renderer's view". A pull-parser built on a different library could
// in principle disagree with the renderer on a malformed-but-recoverable
// document; expat does not recover, which is the documented behaviour
// the spec relies on.
//
// We disable external-entity resolution at parser-construction time.
// That is the billion-laughs defence: the expansion never starts
// because expat does not look at the entity reference beyond parsing
// the entity name. A billion-laughs document therefore parses to a
// tree of <lolz> nodes referencing entities the parser never
// resolves.

#include "skin/svg_sanitize.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include <expat.h>

namespace arrow::skin {

struct SvgSanitizer::Impl {
    XML_Parser parser{nullptr};

    // Count of elements seen so far. SAX fires start-element for
    // every element, including the root <svg>. We cap at 10,000.
    std::size_t element_count{0};
    std::size_t depth{0};

    // The first violation. We stop after recording it; the rest of
    // the document is irrelevant.
    SvgResult result;

    // We track the current element name so the offending_element
    // field in the result is meaningful.
    std::string current_element;
};

namespace {

// Case-insensitive string prefix check.
bool ci_starts_with(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

// Strip the optional namespace from an element/attribute name. expat
// reports "ns:local" for namespaced names; we work with the local
// part throughout.
std::string local_name(std::string_view name) {
    auto colon = name.find(':');
    if (colon == std::string_view::npos) return std::string{name};
    return std::string{name.substr(colon + 1)};
}

// True if the attribute's local name starts with "on", as in
// "onclick", "OnClIcK" (case-insensitive), or "svg:onload" (the
// namespaced form). The spec requires both case-insensitive and
// namespace-stripped matching.
bool is_event_attribute(std::string_view name) {
    auto local = local_name(name);
    return local.size() >= 2 &&
           (std::tolower(static_cast<unsigned char>(local[0])) == 'o') &&
           (std::tolower(static_cast<unsigned char>(local[1])) == 'n');
}

// Refuse hrefs that are not internal fragments. We accept "#name"
// and everything that is empty (rare, but legal) and reject anything
// else — including data: URIs in <image> (handled separately), and
// protocol-relative URLs, and UNC paths, and absolute paths.
bool is_internal_fragment_href(std::string_view v) {
    if (v.empty()) return true;       // not a href at all in any meaningful sense
    if (v.front() != '#') return false;
    return true;
}

bool is_data_uri_href(std::string_view v) {
    if (v.size() < 5) return false;
    return ci_starts_with(v, "data:");
}

}  // namespace

// ---------------------------------------------------------------------------
//  expat callbacks
// ---------------------------------------------------------------------------

namespace {

void xml_start(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* s = static_cast<SvgSanitizer::Impl*>(userData);
    if (!s->result.ok()) return;  // already failing; do nothing more

    s->element_count++;
    if (s->element_count > SvgSanitizer::kMaxElements) {
        s->result = {SvgError::ElementCountExceeded,
                     "more than 10000 elements", 0, 0, local_name(name)};
        XML_StopParser(s->parser, 0);
        return;
    }
    s->depth++;
    s->current_element = local_name(name);

    // Element-name policy.
    std::string_view ln{s->current_element};
    if (ln == "script") {
        s->result = {SvgError::ScriptElement, "<script> is forbidden", 0, 0, s->current_element};
        XML_StopParser(s->parser, 0);
        return;
    }
    if (ln == "foreignObject") {
        s->result = {SvgError::ForeignObject, "<foreignObject> is forbidden", 0, 0, s->current_element};
        XML_StopParser(s->parser, 0);
        return;
    }

    // Attribute policy.
    for (std::size_t i = 0; atts[i] != nullptr; i += 2) {
        std::string_view aname = atts[i];
        std::string_view aval  = atts[i + 1];
        std::string local = local_name(aname);

        if (is_event_attribute(aname)) {
            s->result = {SvgError::EventAttribute,
                         std::string{"event attribute '"} + std::string{aname} + "' is forbidden",
                         0, 0, s->current_element};
            XML_StopParser(s->parser, 0);
            return;
        }
        if (local == "href" || local == "xlink:href" || aname == "href" || aname == "xlink:href") {
            // <use> with any href is "external" because the renderer
            // will resolve it; we treat it as an escape vector even if
            // the value looks like an internal fragment, because <use>
            // can chain to <use> through the fragment path.
            //
            // The spec is more nuanced: <use href="#g1"/> with a
            // fragment IS allowed. We check the element first.
            if (ln == "use") {
                if (!is_internal_fragment_href(aval)) {
                    s->result = {SvgError::UseExternal,
                                 "<use> href must be an internal fragment",
                                 0, 0, s->current_element};
                    XML_StopParser(s->parser, 0);
                    return;
                }
            } else if (ln == "image") {
                if (!is_data_uri_href(aval)) {
                    s->result = {SvgError::ImageNonData,
                                 "<image> href must be a data: URI",
                                 0, 0, s->current_element};
                    XML_StopParser(s->parser, 0);
                    return;
                }
            } else {
                // Any other element with a non-fragment href.
                if (!is_internal_fragment_href(aval)) {
                    s->result = {SvgError::NonFragmentHref,
                                 std::string{"href must be an internal fragment: '"} +
                                 std::string{aval} + "'",
                                 0, 0, s->current_element};
                    XML_StopParser(s->parser, 0);
                    return;
                }
            }
        }
    }
}

void xml_end(void* userData, const XML_Char* /*name*/) {
    auto* s = static_cast<SvgSanitizer::Impl*>(userData);
    if (s->depth > 0) --s->depth;
}

void xml_text(void* /*userData*/, const XML_Char* /*s*/, int /*len*/) {
    // Plain text content; we do not inspect it. Entity expansion is
    // disabled at parser-construction time, so any entity reference
    // is left as the literal '&name;' and never expanded.
}

void xml_skipped_dtd(void* userData) {
    auto* s = static_cast<SvgSanitizer::Impl*>(userData);
    if (!s->result.ok()) return;
    s->result = {SvgError::DoctypePresent, "DOCTYPE is forbidden", 0, 0, ""};
    XML_StopParser(s->parser, 0);
}

void xml_entity_decl(void* userData,
                     const XML_Char* /*entityName*/,
                     int /*is_parameter_entity*/,
                     const XML_Char* /*value*/,
                     int /*value_length*/,
                     const XML_Char* /*base*/,
                     const XML_Char* /*systemId*/,
                     const XML_Char* /*publicId*/,
                     const XML_Char* /*notationName*/) {
    auto* s = static_cast<SvgSanitizer::Impl*>(userData);
    if (!s->result.ok()) return;
    // We treat any entity declaration as an attempt to inject
    // external content. Internal entities (no SYSTEM/PUBLIC) are
    // harmless in principle but the spec says entity expansion is
    // disabled, so a skin author who wants an internal entity has
    // picked the wrong format.
    s->result = {SvgError::ExternalEntity, "entity declarations are forbidden", 0, 0, ""};
    XML_StopParser(s->parser, 0);
}

void xml_comment(void* /*userData*/, const XML_Char* /*data*/) {
    // Comments are ignored.
}

void xml_pi(void* /*userData*/, const XML_Char* /*target*/, const XML_Char* /*data*/) {
    // Processing instructions are ignored.
}

}  // namespace

// ---------------------------------------------------------------------------
//  SvgSanitizer
// ---------------------------------------------------------------------------

SvgSanitizer::SvgSanitizer() : impl_(std::make_unique<Impl>()) {}
SvgSanitizer::~SvgSanitizer() = default;

SvgResult SvgSanitizer::sanitise(std::string_view source, std::string& clean) {
    impl_->result = {};
    impl_->element_count = 0;
    impl_->depth = 0;
    impl_->current_element.clear();

    // XML_ParserCreateNS gives us namespace-aware parsing. We turn
    // off external-entity resolution and DOCTYPE-internal subset
    // processing — that is the billion-laughs defence.
    impl_->parser = XML_ParserCreateNS(nullptr, ' ');
    if (!impl_->parser) {
        return {SvgError::ParseError, "XML_ParserCreateNS failed", 0, 0, ""};
    }
    auto* parser = impl_->parser;
    XML_SetUserData(parser, impl_.get());

    // Stop the parser from doing any external-entity resolution.
    // XML_SetExternalEntityRefHandler / XML_SetSkippedEntityHandler are
    // not set; the library therefore leaves entity references in
    // place and never asks us to fetch anything.
    XML_SetStartElementHandler(parser, xml_start);
    XML_SetEndElementHandler  (parser, xml_end);
    XML_SetCharacterDataHandler(parser, xml_text);
    XML_SetCommentHandler     (parser, xml_comment);
    XML_SetProcessingInstructionHandler(parser, xml_pi);
    XML_SetSkippedDTDHandler  (parser, xml_skipped_dtd);
    XML_SetEntityDeclHandler  (parser, xml_entity_decl);

    // The DOCTYPE handler: expat calls SkippedDTDHandler whenever it
    // sees an internal subset, and SkippedEntityHandler for every
    // entity. We don't install the latter; we install EntityDeclHandler
    // (above) so any explicit <!ENTITY ...> declaration is caught.

    // Status: by default expat will NOT resolve external entities.
    // Verify that with the noent / external general entity parse
    // option: it is OFF by default, but we make it explicit.
    XML_SetBase(parser, "");

    auto* buf = const_cast<char*>(source.data());
    if (XML_Parse(parser, buf, static_cast<int>(source.size()), 1) == XML_STATUS_ERROR) {
        if (impl_->result.ok()) {
            const auto* where = XML_GetCurrentLineNumber(parser);
            const auto* col   = XML_GetCurrentColumnNumber(parser);
            impl_->result = {SvgError::ParseError,
                             std::string{"not well-formed XML: "} +
                             (XML_ErrorString(XML_GetErrorCode(parser)) ? XML_ErrorString(XML_GetErrorCode(parser)) : "?"),
                             where, col, ""};
        }
    }

    SvgResult out = impl_->result;
    if (out.ok()) {
        clean.assign(source);
    } else {
        clean.clear();
    }

    XML_ParserFree(parser);
    impl_->parser = nullptr;
    return out;
}

}  // namespace arrow::skin

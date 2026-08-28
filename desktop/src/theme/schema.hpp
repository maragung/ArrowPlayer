// SPDX-License-Identifier: MPL-2.0
//
// schema.hpp — JSON-Schema validator wrapper for theme and skin payloads.
//
// Spec: eclipse-player.md §11.5 (REQ-THM-040), §11.2 (REQ-THM-010),
//       §11.3 (REQ-THM-015 .. REQ-THM-019).
//
// We back the validator with the pboettch/json-schema-validator
// (https://github.com/pboettch/json-schema-validator, vendored under
// desktop/third_party/json-schema-validator-2.3.0). The library implements
// draft-07 plus a subset of 2019-09 keywords, which is enough for every
// schema under shared-spec/schemas/ today (all of which are draft 2020-12
// but use a vocabulary compatible with the 07 validator: required,
// additionalProperties:false, const, enum, pattern, format, minLength,
// maxLength, minimum, maximum, exclusiveMinimum, exclusiveMaximum,
// minItems, maxItems, minProperties, maxProperties, uniqueItems, $ref,
// oneOf, anyOf, allOf, if/then/else, $defs).
//
// The whole point of going through a real JSON-Schema library rather than
// hand-rolling a checker is that the library produces JSON Pointer
// locations for every violation. The spec calls those out explicitly:
//   "every error with a JSON Pointer to the offending node"
//   (REQ-THM-060 — tools/theme-validate).
// We do not soften that requirement anywhere in the theme engine.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Forward-declare to keep the heavyweight <nlohmann/json-schema.hpp> out of
// every translation unit that just needs the validation result.
namespace nlohmann { namespace json_schema { class json_validator; } }

namespace arrow::theme {

// ---------------------------------------------------------------------------
//  Schemas recognised by this engine. The numeric ids mirror the
//  `$id` declared in shared-spec/schemas/*.json.
// ---------------------------------------------------------------------------

enum class SchemaId : std::uint8_t {
    Theme,         // https://arrow-player.org/schemas/theme/v1
    SkinManifest,  // https://arrow-player.org/schemas/skin-manifest/v1
    Layout,        // https://arrow-player.org/schemas/layout/v1
};

// A single validation finding. `instance_pointer` is the JSON Pointer
// (RFC 6901) to the offending node in the document under validation;
// `schema_pointer` is the JSON Pointer to the schema constraint that
// rejected it. The library fills both in for us, and the contract this
// header exposes to callers is exactly the contract the spec requires.
struct SchemaError {
    std::string instance_pointer;  // e.g. "/color/text/primary"
    std::string schema_pointer;    // e.g. "/$defs/color"
    std::string message;           // human-readable, non-normative
};

// The outcome of one validation call. `errors` is empty on success.
// Non-zero `error_count` means the document is rejected; the loader
// surfaces the first error with its JSON Pointer to the caller.
struct SchemaResult {
    bool ok() const noexcept { return errors.empty(); }
    std::vector<SchemaError> errors;
};

// ---------------------------------------------------------------------------
//  SchemaValidator
//
//  A process-wide cache of compiled JSON Schemas. Compiling a schema is
//  expensive (the validator walks every keyword, resolves every $ref,
//  and pre-compiles the regexes for every `pattern`); the cache guarantees
//  we do that work once per SchemaId per process.
//
//  REQ-THM-040 (pipeline step 4) calls for repeated validation of the same
//  theme.json; a cache is the only way that stays fast enough to be done
//  on every keystroke in the skin editor (REQ-THM-051).
//
//  Thread-safety: get() is safe to call from any thread once the cache
//  has been initialised by SchemaValidator::init(). init() must be called
//  before the first validation.
// ---------------------------------------------------------------------------

class SchemaValidator {
public:
    SchemaValidator();
    ~SchemaValidator();

    SchemaValidator(const SchemaValidator&)            = delete;
    SchemaValidator& operator=(const SchemaValidator&) = delete;

    // Load (or re-load) the schema files from disk and pre-compile every
    // schema the engine knows about. Safe to call more than once — each
    // call replaces the cached compiled validators with a fresh set.
    //
    // On failure returns false and populates `error_out` with the reason.
    bool init(const std::filesystem::path& schemas_dir,
              std::string& error_out);

    // Validate a parsed JSON document. The document is taken as a UTF-8
    // string and parsed with the validator library's parser; this avoids
    // the second-parse cost of going through our own JSON Value first.
    SchemaResult validate(SchemaId id, std::string_view document) const;

    // Validate a parsed nlohmann::json. Provided for callers that have
    // already parsed the document (e.g. when it is bundled in a larger
    // structure).
    SchemaResult validate(SchemaId id, const class ::nlohmann::json& doc) const;

    // The default on-disk location of the schemas, relative to the
    // repository root. Used by tools/theme-validate and by tests.
    static std::filesystem::path default_schema_dir();

private:
    // The actual validator instance. We hold one per SchemaId.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace arrow::theme

// Allow callers that already include <nlohmann/json.hpp> to forward-declare
// without surprises. The full type lives in <nlohmann/json.hpp>; we only
// forward-declare the bare minimum above to keep this header light.
#include <nlohmann/json.hpp>

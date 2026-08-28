// SPDX-License-Identifier: MPL-2.0
//
// schema.cpp — see schema.hpp for design notes.

#include "theme/schema.hpp"

#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

#include <nlohmann/json-schema.hpp>

namespace arrow::theme {

// ---------------------------------------------------------------------------
//  Per-schema-id compiled validator + the on-disk path it was loaded from.
//  Held in a single pImpl so schema.hpp does not drag the validator's
//  heavy include into every translation unit.
// ---------------------------------------------------------------------------

struct SchemaValidator::Impl {
    // One compiled validator per SchemaId, in the same order as the enum.
    std::array<std::unique_ptr<nlohmann::json_schema::json_validator>,
               3> validators{};

    // Path on disk to the schema that was loaded for each id. Used only
    // in error messages; the validator is what does the work at runtime.
    std::array<std::string, 3> source_paths{};
};

// ---------------------------------------------------------------------------
//  Resolve the directory the schemas live in.
//
//  We expect the working tree to look like:
//
//      <repo>/desktop/                          ← CMAKE_CURRENT_SOURCE_DIR
//      <repo>/shared-spec/schemas/             ← default_schema_dir()
//
//  In a packaged build the schemas are also embedded in the binary; the
//  caller of init() picks which path to load from.
// ---------------------------------------------------------------------------

std::filesystem::path SchemaValidator::default_schema_dir() {
    // We do not call into CMake-generated config from here. The default
    // is the conventional path so the CLI and the test runner can both
    // resolve it without a build-time stub.
    return std::filesystem::path{"shared-spec"} / "schemas";
}

// ---------------------------------------------------------------------------
//  Map our SchemaId enum to the on-disk filename and the schema's $id.
//  The $id is what the validator's set_root_schema expects, and what the
//  error messages refer to in the human-readable summary.
// ---------------------------------------------------------------------------

namespace {

struct SchemaEntry {
    const char* filename;
    const char* expected_id;
    const char* label;  // for error messages
};

constexpr std::array<SchemaEntry, 3> kSchemaTable = {{
    {"theme-schema.json",
     "https://eclipse-player.org/schemas/theme/v1",
     "theme"},
    {"skin-manifest.schema.json",
     "https://eclipse-player.org/schemas/skin-manifest/v1",
     "skin-manifest"},
    {"layout.schema.json",
     "https://eclipse-player.org/schemas/layout/v1",
     "layout"},
}};

std::size_t index_of(SchemaId id) {
    return static_cast<std::size_t>(id);
}

SchemaEntry entry_for(SchemaId id) {
    return kSchemaTable[index_of(id)];
}

}  // namespace

// ---------------------------------------------------------------------------
//  Construction / destruction
// ---------------------------------------------------------------------------

SchemaValidator::SchemaValidator() : impl_(std::make_unique<Impl>()) {}

SchemaValidator::~SchemaValidator() = default;

// ---------------------------------------------------------------------------
//  init — load every schema from disk and compile it.
//
//  The validator is built on top of draft-07. The schemas under
//  shared-spec/schemas/ are nominally draft 2020-12, but the keyword
//  vocabulary they use is a strict subset of what draft-07 supports:
//  required, additionalProperties:false, $ref, const, enum, pattern,
//  format, minLength, maxLength, minimum, maximum, exclusiveMinimum,
//  exclusiveMaximum, minItems, maxItems, minProperties, maxProperties,
//  uniqueItems, propertyNames, oneOf, anyOf, allOf, if/then/else, $defs.
//  We pass the $schema field straight through and let the library
//  pick its best match — it falls back from 2020-12 to 2019-09 to
//  draft-07 transparently, which is the documented behaviour.
// ---------------------------------------------------------------------------

bool SchemaValidator::init(const std::filesystem::path& schemas_dir,
                           std::string& error_out) {
    impl_ = std::make_unique<Impl>();
    std::error_code ec;

    for (std::size_t i = 0; i < kSchemaTable.size(); ++i) {
        const auto& entry = kSchemaTable[i];
        const auto path = schemas_dir / entry.filename;

        if (!std::filesystem::exists(path, ec)) {
            error_out = std::string{"schema file not found: "} + path.string();
            impl_.reset();
            return false;
        }

        std::ifstream in(path);
        if (!in) {
            error_out = std::string{"cannot open schema file: "} + path.string();
            impl_.reset();
            return false;
        }

        nlohmann::json schema;
        try {
            in >> schema;
        } catch (const std::exception& e) {
            error_out = std::string{"schema "} + entry.filename + " is not valid JSON: " + e.what();
            impl_.reset();
            return false;
        }

        auto validator = std::make_unique<nlohmann::json_schema::json_validator>();
        try {
            validator->set_root_schema(schema);
        } catch (const std::exception& e) {
            error_out = std::string{"schema "} + entry.filename + " failed to compile: " + e.what();
            impl_.reset();
            return false;
        }

        impl_->validators[i]    = std::move(validator);
        impl_->source_paths[i]  = path.string();
    }
    return true;
}

// ---------------------------------------------------------------------------
//  validate(string_view) — parse + validate in one call.
//
//  The validator library does not expose a streaming API; it wants the
//  document as a parsed nlohmann::json. We parse it once here, and a
//  caller that already has a parsed document uses the other overload.
// ---------------------------------------------------------------------------

namespace {

// A json_schema::error_handler that collects the JSON Pointer + message
// of every violation into a SchemaResult. The library's default handler
// throws on the first error; that would conflate the first violation
// with the only violation, which is not what REQ-THM-040 wants.
class CollectingHandler final : public nlohmann::json_schema::error_handler {
public:
    void error(const nlohmann::json::json_pointer& ptr,
               const nlohmann::json& /*instance*/,
               const std::string& message) override {
        result_.errors.push_back(SchemaError{
            ptr.to_string(),
            std::string{},  // schema_pointer not exposed by base interface
            message,
        });
    }

    void error(const nlohmann::json::json_pointer& ptr,
               const std::string& message) override {
        result_.errors.push_back(SchemaError{
            ptr.to_string(),
            std::string{},
            message,
        });
    }

    SchemaResult result_;
};

}  // namespace

SchemaResult SchemaValidator::validate(SchemaId id, std::string_view document) const {
    if (!impl_) {
        return SchemaError{"", "", "SchemaValidator::init() was not called"};
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(document);
    } catch (const std::exception& e) {
        SchemaResult r;
        r.errors.push_back({"", "", std::string{"document is not valid JSON: "} + e.what()});
        return r;
    }
    return validate(id, parsed);
}

SchemaResult SchemaValidator::validate(SchemaId id,
                                       const nlohmann::json& doc) const {
    SchemaResult result;
    if (!impl_) {
        result.errors.push_back({"", "", "SchemaValidator::init() was not called"});
        return result;
    }
    const auto idx = index_of(id);
    auto& slot = impl_->validators[idx];
    if (!slot) {
        result.errors.push_back({"", "",
            std::string{"no validator compiled for schema "} + entry_for(id).label});
        return result;
    }
    CollectingHandler handler;
    slot->validate(doc, handler);
    return std::move(handler.result_);
}

}  // namespace arrow::theme

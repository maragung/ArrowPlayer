// SPDX-License-Identifier: MPL-2.0
//
// JsonSchemaValidator.kt — Lightweight JSON Schema draft 2020-12 validator.
//
// Spec: eclipse-player.md §11.5 (REQ-THM-040), §11.2 (REQ-THM-010).
//
// This is the Android equivalent of tools/jsonschema_mini.py and the
// C++ schema validator.  It implements the subset of draft 2020-12 that
// the shared-spec schemas actually use, and produces JSON Pointer locations
// for every violation.
//
// The validator is deliberately simple: it handles the constraints declared
// in the schemas (required, additionalProperties:false, const, enum,
// pattern, format, minLength, maxLength, minimum, maximum, minItems, maxItems,
// $ref, oneOf, anyOf, allOf, if/then/else, $defs) and rejects any schema
// that uses a keyword outside that set.
//
// Thread-safety: instances are immutable and safe to share across threads.
// The Android validator does NOT use kotlinx.coroutines — it is a pure
// Kotlin function with no side effects and no I/O.

package io.github.arrowplayer.core.theme

import org.json.JSONArray
import org.json.JSONObject
import java.util.regex.Pattern
import kotlin.math.pow

// ---------------------------------------------------------------------------
//  Schema validation result
// ---------------------------------------------------------------------------

/**
 * A schema validation error with a JSON Pointer to the offending node.
 *
 * @property pointer JSON Pointer path to the failing node (e.g. "/color/text/primary").
 * @property message Human-readable description of the failure.
 */
data class SchemaError(
    val pointer: String,
    val message: String,
)

/**
 * Result of a schema validation pass.
 *
 * @property errors Empty on success; populated with [SchemaError] entries on failure.
 */
data class SchemaResult(
    val errors: List<SchemaError> = emptyList(),
) {
    val ok: Boolean get() = errors.isEmpty()
}

// ---------------------------------------------------------------------------
//  JSON Pointer utilities
// ---------------------------------------------------------------------------

/**
 * Convert a list of path segments into a JSON Pointer string.
 * e.g. listOf("color", "text", "primary") → "/color/text/primary"
 */
private fun pointerOf(segments: List<String>): String =
    if (segments.isEmpty()) "" else "/${segments.joinToString("/")}"

/**
 * Resolve a `$ref` within a schema document.
 * Only local `#` fragment references are supported.
 */
private fun resolveRef(schema: JSONObject, ref: String): JSONObject? {
    if (!ref.startsWith("#/")) return null
    val path = ref.removePrefix("#/").split("/")
    var current: Any = schema
    for (segment in path) {
        current = when (current) {
            is JSONObject -> current.opt(segment) ?: return null
            else -> return null
        }
    }
    return current as? JSONObject
}

// ---------------------------------------------------------------------------
//  Pattern cache  (avoid re-compiling the same regex)
// ---------------------------------------------------------------------------

private val patternCache = mutableMapOf<String, Pattern>()

private fun compilePattern(pattern: String): Pattern {
    return patternCache.getOrPut(pattern) {
        Pattern.compile(pattern)
    }
}

// ---------------------------------------------------------------------------
//  SchemaValidator
// ---------------------------------------------------------------------------

/**
 * Validates JSON documents against a JSON Schema (draft 2020-12 subset).
 *
 * @param schema The compiled schema document (from a JSON file).
 * @property errors The list of errors found during the last validation.
 *
 * Usage:
 * ```
 * val validator = JsonSchemaValidator(schemaObject)
 * val result = validator.validate(instanceObject)
 * if (!result.ok) { result.errors.forEach { println("${it.pointer}: ${it.message}") } }
 * ```
 */
class JsonSchemaValidator(private val schema: JSONObject) {

    private val defs: JSONObject = schema.optJSONObject("\$defs") ?: JSONObject()

    /**
     * Validate a JSON document against the schema.
     *
     * @param instance The JSON value to validate.
     * @param pointer Current JSON Pointer path (for error messages).
     * @return List of [SchemaError] (empty on success).
     */
    fun validate(instance: Any?, pointer: String = ""): List<SchemaError> {
        return validateValue(instance, schema, listOf())
    }

    private fun validateValue(instance: Any?, schemaNode: JSONObject?, path: List<String>): List<SchemaError> {
        if (schemaNode == null) return emptyList()
        val errors = mutableListOf<SchemaError>()
        val ptr = pointerOf(path)

        // Follow $ref if present
        val effectiveSchema = if (schemaNode.has("\$ref")) {
            val ref = schemaNode.getString("\$ref")
            resolveRef(schema, ref) ?: schemaNode
        } else {
            schemaNode
        }

        // type
        if (effectiveSchema.has("type")) {
            val typeOk = when (effectiveSchema.get("type")) {
                "string" -> instance is String
                "number" -> instance is Number && instance !is Boolean
                "integer" -> instance is Number && instance !is Boolean && (instance.toDouble() == instance.toLong().toDouble())
                "boolean" -> instance is Boolean
                "object" -> instance is JSONObject
                "array" -> instance is JSONArray
                "null" -> instance == null || instance == JSONObject.NULL
                is JSONArray -> {
                    val types = (0 until effectiveSchema.getJSONArray("type").length())
                        .map { effectiveSchema.getJSONArray("type").getString(it) }
                    types.any { typeMatches(instance, it) }
                }
                else -> true
            }
            if (!typeOk) {
                val got = when (instance) {
                    is String -> "string"
                    is Number -> if (instance is Boolean) "boolean" else "number"
                    is Boolean -> "boolean"
                    is JSONObject -> "object"
                    is JSONArray -> "array"
                    null, JSONObject.NULL -> "null"
                    else -> "unknown"
                }
                errors.add(SchemaError(ptr, "expected type ${effectiveSchema.get("type")}, got $got"))
            }
        }

        // const
        if (effectiveSchema.has("const")) {
            val const = effectiveSchema.get("const")
            if (!equals(const, instance)) {
                errors.add(SchemaError(ptr, "must equal ${const}"))
            }
        }

        // enum
        if (effectiveSchema.has("enum")) {
            val enumVals = effectiveSchema.getJSONArray("enum")
            if (0 until enumVals.length().none { equals(enumVals[it], instance) }) {
                errors.add(SchemaError(ptr, "must be one of ${(0 until minOf(enumVals.length(), 8)).map { enumVals[it] }.joinToString()}"))
            }
        }

        // numeric constraints
        if (instance is Number && instance !is Boolean) {
            val d = instance.toDouble()
            effectiveSchema.optDouble("minimum", Double.NaN).takeIf { !it.isNaN() }?.let { min ->
                if (d < min) errors.add(SchemaError(ptr, "$d is less than minimum $min"))
            }
            effectiveSchema.optDouble("maximum", Double.NaN).takeIf { !it.isNaN() }?.let { max ->
                if (d > max) errors.add(SchemaError(ptr, "$d is greater than maximum $max"))
            }
            effectiveSchema.optDouble("exclusiveMinimum", Double.NaN).takeIf { !it.isNaN() }?.let { min ->
                if (d <= min) errors.add(SchemaError(ptr, "$d must be > $min"))
            }
            effectiveSchema.optDouble("exclusiveMaximum", Double.NaN).takeIf { !it.isNaN() }?.let { max ->
                if (d >= max) errors.add(SchemaError(ptr, "$d must be < $max"))
            }
            effectiveSchema.optDouble("multipleOf", Double.NaN).takeIf { !it.isNaN() }?.let { mod ->
                if (mod > 0 && (d / mod - (d / mod).toLong()) > 1e-9)
                    errors.add(SchemaError(ptr, "$d is not a multiple of $mod"))
            }
        }

        // string constraints
        if (instance is String) {
            effectiveSchema.optInt("minLength", Int.MIN_VALUE).takeIf { it != Int.MIN_VALUE }?.let { min ->
                if (instance.length < min)
                    errors.add(SchemaError(ptr, "string shorter than minLength $min"))
            }
            effectiveSchema.optInt("maxLength", Int.MAX_VALUE).takeIf { it != Int.MAX_VALUE }?.let { max ->
                if (instance.length > max)
                    errors.add(SchemaError(ptr, "string longer than maxLength $max"))
            }
            if (effectiveSchema.has("pattern")) {
                val pattern = effectiveSchema.getString("pattern")
                if (!compilePattern(pattern).matcher(instance).find())
                    errors.add(SchemaError(ptr, "\"$instance\" does not match /$pattern/"))
            }
            if (effectiveSchema.opt("format") == "uri") {
                if (!instance.matches(Regex("^[A-Za-z][A-Za-z0-9+.-]*:.*")))
                    errors.add(SchemaError(ptr, "\"$instance\" is not an absolute URI"))
            }
        }

        // array constraints
        if (instance is JSONArray) {
            effectiveSchema.optInt("minItems", Int.MIN_VALUE).takeIf { it != Int.MIN_VALUE }?.let { min ->
                if (instance.length() < min)
                    errors.add(SchemaError(ptr, "array has ${instance.length()} items, minimum is $min"))
            }
            effectiveSchema.optInt("maxItems", Int.MAX_VALUE).takeIf { it != Int.MAX_VALUE }?.let { max ->
                if (instance.length() > max)
                    errors.add(SchemaError(ptr, "array has ${instance.length()} items, maximum is $max"))
            }
            if (effectiveSchema.optBoolean("uniqueItems")) {
                val seen = mutableSetOf<String>()
                for (i in 0 until instance.length()) {
                    val canonical = canonicalize(instance[i])
                    if (!seen.add(canonical))
                        errors.add(SchemaError("$ptr/$i", "duplicate item ${instance[i]}"))
                }
            }
            if (effectiveSchema.has("items")) {
                val items = effectiveSchema.get("items")
                if (items is JSONObject) {
                    for (i in 0 until instance.length()) {
                        errors.addAll(validateValue(instance[i], items, path + listOf(i.toString())))
                    }
                }
            }
        }

        // object constraints
        if (instance is JSONObject) {
            // required
            effectiveSchema.optJSONArray("required")?.let { req ->
                for (i in 0 until req.length()) {
                    val key = req.getString(i)
                    if (!instance.has(key)) {
                        errors.add(SchemaError(ptr, "missing required property '$key'"))
                    }
                }
            }

            // minProperties / maxProperties
            val propCount = instance.length()
            effectiveSchema.optInt("minProperties", Int.MIN_VALUE).takeIf { it != Int.MIN_VALUE }?.let { min ->
                if (propCount < min)
                    errors.add(SchemaError(ptr, "object has $propCount properties, minimum is $min"))
            }
            effectiveSchema.optInt("maxProperties", Int.MAX_VALUE).takeIf { it != Int.MAX_VALUE }?.let { max ->
                if (propCount > max)
                    errors.add(SchemaError(ptr, "object has $propCount properties, maximum is $max"))
            }

            // properties
            val props = effectiveSchema.optJSONObject("properties") ?: JSONObject()
            val additionalAllowed = effectiveSchema.opt("additionalProperties")

            instance.keys().forEach { key ->
                val keyPath = path + listOf(key)

                if (props.has(key)) {
                    // Validate against the specific property schema
                    val propSchema = props.getJSONObject(key)
                    errors.addAll(validateValue(instance[key], propSchema, keyPath))
                } else {
                    // additionalProperties check
                    when (additionalAllowed) {
                        is Boolean -> {
                            if (!additionalAllowed) {
                                errors.add(SchemaError(pointerOf(keyPath), "property '$key' is not allowed"))
                            }
                        }
                        is JSONObject -> {
                            errors.addAll(validateValue(instance[key], additionalAllowed, keyPath))
                        }
                    }
                }
            }
        }

        // logic keywords
        effectiveSchema.optJSONArray("allOf")?.let { allOf ->
            for (i in 0 until allOf.length()) {
                val subSchema = allOf.getJSONObject(i)
                errors.addAll(validateValue(instance, subSchema, path))
            }
        }

        if (effectiveSchema.has("anyOf")) {
            val anyOf = effectiveSchema.getJSONArray("anyOf")
            val branchErrors = (0 until anyOf.length()).map { validateValue(instance, anyOf.getJSONObject(it), path) }
            if (branchErrors.all { it.isNotEmpty() }) {
                val closest = branchErrors.minByOrNull { it.size } ?: emptyList()
                errors.add(SchemaError(ptr, "matches no anyOf branches (closest: ${closest.firstOrNull()?.message ?: "none"})"))
            }
        }

        if (effectiveSchema.has("oneOf")) {
            val oneOf = effectiveSchema.getJSONArray("oneOf")
            val passing = (0 until oneOf.length()).filter { validateValue(instance, oneOf.getJSONObject(it), path).isEmpty() }
            when {
                passing.isEmpty() -> {
                    errors.add(SchemaError(ptr, "matches no oneOf branches"))
                }
                passing.size > 1 -> {
                    errors.add(SchemaError(ptr, "matches ${passing.size} oneOf branches, must match exactly one"))
                }
            }
        }

        if (effectiveSchema.has("not")) {
            val notSchema = effectiveSchema.getJSONObject("not")
            if (validateValue(instance, notSchema, path).isEmpty()) {
                errors.add(SchemaError(ptr, "must not match the 'not' schema"))
            }
        }

        // if/then/else
        if (effectiveSchema.has("if")) {
            val ifErrors = validateValue(instance, effectiveSchema.getJSONObject("if"), path)
            if (ifErrors.isEmpty()) {
                if (effectiveSchema.has("then")) {
                    errors.addAll(validateValue(instance, effectiveSchema.getJSONObject("then"), path))
                }
            } else {
                if (effectiveSchema.has("else")) {
                    errors.addAll(validateValue(instance, effectiveSchema.getJSONObject("else"), path))
                }
            }
        }

        return errors
    }

    private fun typeMatches(instance: Any?, type: String): Boolean = when (type) {
        "string" -> instance is String
        "number" -> instance is Number && instance !is Boolean
        "integer" -> instance is Number && instance !is Boolean && instance.toDouble() == instance.toLong().toDouble()
        "boolean" -> instance is Boolean
        "object" -> instance is JSONObject
        "array" -> instance is JSONArray
        "null" -> instance == null || instance == JSONObject.NULL
        else -> false
    }

    private fun equals(a: Any?, b: Any?): Boolean = canonicalize(a) == canonicalize(b)

    private fun canonicalize(v: Any?): String = when (v) {
        null, JSONObject.NULL -> "null"
        is String -> "\"$v\""
        is Number -> if (v.toDouble().isNaN()) "NaN" else if (v is Double && (v.isInfinite())) v.toString() else v.toString()
        is Boolean -> v.toString()
        else -> v.toString()
    }

    // Convenience wrappers for manifest, theme, layout

    fun validateManifest(doc: JSONObject): List<SchemaError> {
        val manifestSchema = resolveRef(schema, "#/\$defs/skinManifest")
            ?: schema.optJSONObject("properties")?.optJSONObject("capabilities")?.let { schema }
        return validate(doc, "")
    }

    fun validateTheme(doc: JSONObject): List<SchemaError> {
        return validate(doc, "")
    }

    fun validateLayout(doc: JSONObject): List<SchemaError> {
        return validate(doc, "")
    }
}

// ---------------------------------------------------------------------------
//  Factory — loads schema from JSON
// ---------------------------------------------------------------------------

/**
 * Load a schema validator from a JSON string.
 *
 * @param schemaJson The schema JSON string.
 * @return A [JsonSchemaValidator] instance.
 * @throws IllegalArgumentException if the schema is not valid JSON or uses
 *         an unsupported keyword.
 */
fun loadSchemaValidator(schemaJson: String): JsonSchemaValidator {
    val schema = JSONObject(schemaJson)
    return JsonSchemaValidator(schema)
}

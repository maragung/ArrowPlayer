#!/usr/bin/env python3
"""A small, dependency-free JSON Schema draft 2020-12 validator.

Why this exists
---------------
`shared-spec/` is load-bearing (REQ-GEN-031): the fixtures there are what prove
the desktop and Android engines agree on a format, rather than merely claiming
it. A fixture nobody validates proves nothing, so validation has to run on a
developer's machine and not only in CI.

The reference validator is the `jsonschema` PyPI package. Installing it is not
available in every environment this repo has to work in, and vendoring a large
third-party library into `tools/` to check our own files would be a poor trade.
So this module implements the subset of draft 2020-12 that the schemas in
`shared-spec/schemas/` actually use — and `tools/validate-shared-spec.py`
asserts that they use nothing outside that subset, so the subset can never
silently drift into a rubber stamp.

What it is NOT
--------------
Not a conformant draft 2020-12 implementation. Unsupported: remote `$ref`,
`$dynamicRef`/`$dynamicAnchor`, `$vocabulary`, `unevaluatedProperties`,
`unevaluatedItems`, `contains`/`minContains`/`maxContains`, `dependentSchemas`,
`dependentRequired`, `contentEncoding`, and full `format` assertion. The full
draft-2020-12 gate runs in `.github/workflows/spec-ci.yml` against the reference
validator; this module is the fast local check, and the two are kept honest by
the keyword allowlist. Regular expressions are evaluated with Python's `re`
rather than ECMA-262 semantics, which is why the schemas here stay inside the
common subset of both.
"""

from __future__ import annotations

import json
import re
from typing import Any, Iterator

# Keywords this module understands. `validate-shared-spec.py` refuses any schema
# that uses a keyword outside this set, which is what stops an unimplemented
# keyword from being silently ignored — the failure mode that makes a homegrown
# validator worse than no validator at all.
SUPPORTED: frozenset[str] = frozenset({
    # annotations, ignored for validation
    "$schema", "$id", "$defs", "$comment", "$anchor",
    "title", "description", "default", "examples", "deprecated",
    "readOnly", "writeOnly",
    # structural
    "$ref",
    "type", "enum", "const",
    "allOf", "anyOf", "oneOf", "not", "if", "then", "else",
    # objects
    "properties", "patternProperties", "additionalProperties", "propertyNames",
    "required", "minProperties", "maxProperties",
    # arrays
    "items", "prefixItems", "minItems", "maxItems", "uniqueItems",
    # numbers
    "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf",
    # strings
    "minLength", "maxLength", "pattern", "format",
})

_TYPES: dict[str, Any] = {
    "object": dict,
    "array": list,
    "string": str,
    "boolean": bool,
    "null": type(None),
}


class SchemaError(Exception):
    """The schema itself is malformed or uses an unsupported keyword."""


def _is_int(value: Any) -> bool:
    # JSON has one number type; 2.0 is an integer. bool is a subclass of int in
    # Python and must not count as one here.
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return True
    return isinstance(value, float) and value.is_integer()


def _is_num(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _type_ok(value: Any, name: str) -> bool:
    if name == "integer":
        return _is_int(value)
    if name == "number":
        return _is_num(value)
    if name == "boolean":
        return isinstance(value, bool)
    expected = _TYPES.get(name)
    if expected is None:
        raise SchemaError(f"unknown type name {name!r}")
    if expected is int:  # unreachable, kept explicit
        return _is_int(value)
    return isinstance(value, expected) and not (expected is not bool and isinstance(value, bool))


def _canonical(value: Any) -> str:
    """A stable key for uniqueItems and enum/const equality.

    JSON equality is structural and order-insensitive for objects, so comparing
    Python objects directly would call [1, 2] and [2, 1] distinct (correct) but
    also {"a": 1, "b": 2} and {"b": 2, "a": 1} distinct (wrong). Sorted keys fix
    that. `1` and `1.0` must also compare equal, hence the int normalisation.
    """
    def norm(node: Any) -> Any:
        if isinstance(node, bool):
            return node
        if isinstance(node, float) and node.is_integer():
            return int(node)
        if isinstance(node, dict):
            return {k: norm(v) for k, v in node.items()}
        if isinstance(node, list):
            return [norm(v) for v in node]
        return node
    return json.dumps(norm(value), sort_keys=True, separators=(",", ":"))


class Validator:
    """Validates instances against one schema document.

    `iter_errors` yields human-readable messages with a JSON-Pointer-ish path so
    a failing fixture says *where* it failed, which is the difference between a
    useful gate and a wall of red.
    """

    def __init__(self, schema: dict[str, Any] | bool, *, check_keywords: bool = True) -> None:
        self.root = schema
        if check_keywords:
            unsupported = sorted(self.unsupported_keywords(schema))
            if unsupported:
                raise SchemaError(
                    "schema uses keywords this validator does not implement: "
                    + ", ".join(unsupported)
                )
        self._patterns: dict[str, re.Pattern[str]] = {}

    # ------------------------------------------------------------------ keywords
    @classmethod
    def unsupported_keywords(cls, schema: Any) -> set[str]:
        """Every keyword in the document that this module would ignore.

        Walks only positions where a schema can appear, so a `properties` entry
        named `contains` (a legitimate property name) is not mistaken for the
        `contains` keyword.
        """
        found: set[str] = set()

        def walk(node: Any) -> None:
            if isinstance(node, bool) or not isinstance(node, dict):
                return
            for key, value in node.items():
                if key not in SUPPORTED:
                    found.add(key)
                    continue
                if key in ("properties", "patternProperties", "$defs"):
                    if isinstance(value, dict):
                        for sub in value.values():
                            walk(sub)
                elif key in ("allOf", "anyOf", "oneOf", "prefixItems"):
                    if isinstance(value, list):
                        for sub in value:
                            walk(sub)
                elif key in ("not", "if", "then", "else", "items",
                             "additionalProperties", "propertyNames"):
                    walk(value)
        walk(schema)
        return found

    # ---------------------------------------------------------------------- refs
    def _resolve(self, ref: str) -> dict[str, Any] | bool:
        if not ref.startswith("#"):
            raise SchemaError(f"only local $ref is supported, got {ref!r}")
        node: Any = self.root
        for raw in ref[1:].split("/"):
            if raw == "":
                continue
            token = raw.replace("~1", "/").replace("~0", "~")
            if isinstance(node, list):
                node = node[int(token)]
            else:
                try:
                    node = node[token]
                except (KeyError, TypeError):
                    raise SchemaError(f"$ref {ref!r} does not resolve") from None
        return node

    def _pattern(self, pattern: str) -> re.Pattern[str]:
        compiled = self._patterns.get(pattern)
        if compiled is None:
            try:
                compiled = re.compile(pattern)
            except re.error as exc:
                raise SchemaError(f"bad pattern {pattern!r}: {exc}") from None
            self._patterns[pattern] = compiled
        return compiled

    # ------------------------------------------------------------------ validate
    def is_valid(self, instance: Any) -> bool:
        for _ in self.iter_errors(instance):
            return False
        return True

    def validate(self, instance: Any) -> None:
        errors = list(self.iter_errors(instance))
        if errors:
            raise ValueError("; ".join(errors))

    def iter_errors(self, instance: Any, schema: Any = None, path: str = "") -> Iterator[str]:
        if schema is None:
            schema = self.root
        if schema is True or schema == {}:
            return
        if schema is False:
            yield f"{path or '<root>'}: schema is `false`, nothing validates"
            return
        if not isinstance(schema, dict):
            raise SchemaError(f"schema at {path!r} is not an object or boolean")

        if "$ref" in schema:
            yield from self.iter_errors(instance, self._resolve(schema["$ref"]), path)
            # draft 2020-12 applies sibling keywords alongside $ref; fall through.

        yield from self._check_core(instance, schema, path)
        yield from self._check_object(instance, schema, path)
        yield from self._check_array(instance, schema, path)
        yield from self._check_number(instance, schema, path)
        yield from self._check_string(instance, schema, path)
        yield from self._check_logic(instance, schema, path)

    # -------------------------------------------------------------------- pieces
    def _check_core(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        where = path or "<root>"
        if "type" in schema:
            names = schema["type"]
            names = [names] if isinstance(names, str) else names
            if not any(_type_ok(instance, n) for n in names):
                yield f"{where}: expected type {'/'.join(names)}, got {type(instance).__name__}"
        if "const" in schema and _canonical(instance) != _canonical(schema["const"]):
            yield f"{where}: must equal {json.dumps(schema['const'])}"
        if "enum" in schema:
            allowed = {_canonical(v) for v in schema["enum"]}
            if _canonical(instance) not in allowed:
                shown = ", ".join(json.dumps(v) for v in schema["enum"][:8])
                more = "" if len(schema["enum"]) <= 8 else f", … ({len(schema['enum'])} total)"
                yield f"{where}: {json.dumps(instance)} is not one of [{shown}{more}]"

    def _check_object(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        if not isinstance(instance, dict):
            return
        where = path or "<root>"
        for name in schema.get("required", []):
            if name not in instance:
                yield f"{where}: missing required property {name!r}"
        if "minProperties" in schema and len(instance) < schema["minProperties"]:
            yield f"{where}: needs at least {schema['minProperties']} properties, has {len(instance)}"
        if "maxProperties" in schema and len(instance) > schema["maxProperties"]:
            yield f"{where}: allows at most {schema['maxProperties']} properties, has {len(instance)}"

        properties = schema.get("properties", {})
        patterns = schema.get("patternProperties", {})
        for key, value in instance.items():
            matched = False
            if key in properties:
                matched = True
                yield from self.iter_errors(value, properties[key], f"{path}/{key}")
            for pattern, sub in patterns.items():
                if self._pattern(pattern).search(key):
                    matched = True
                    yield from self.iter_errors(value, sub, f"{path}/{key}")
            if not matched and "additionalProperties" in schema:
                extra = schema["additionalProperties"]
                if extra is False:
                    yield f"{where}: property {key!r} is not allowed"
                else:
                    yield from self.iter_errors(value, extra, f"{path}/{key}")
            if "propertyNames" in schema:
                for err in self.iter_errors(key, schema["propertyNames"], f"{path}/{key}"):
                    yield f"{err} (property name)"

    def _check_array(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        if not isinstance(instance, list):
            return
        where = path or "<root>"
        prefix = schema.get("prefixItems", [])
        for index, sub in enumerate(prefix):
            if index < len(instance):
                yield from self.iter_errors(instance[index], sub, f"{path}/{index}")
        if "items" in schema:
            for index in range(len(prefix), len(instance)):
                yield from self.iter_errors(instance[index], schema["items"], f"{path}/{index}")
        if "minItems" in schema and len(instance) < schema["minItems"]:
            yield f"{where}: needs at least {schema['minItems']} items, has {len(instance)}"
        if "maxItems" in schema and len(instance) > schema["maxItems"]:
            yield f"{where}: allows at most {schema['maxItems']} items, has {len(instance)}"
        if schema.get("uniqueItems") is True:
            seen: set[str] = set()
            for index, item in enumerate(instance):
                key = _canonical(item)
                if key in seen:
                    yield f"{path}/{index}: duplicate item {json.dumps(item)}"
                seen.add(key)

    def _check_number(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        if not _is_num(instance):
            return
        where = path or "<root>"
        if "minimum" in schema and instance < schema["minimum"]:
            yield f"{where}: {instance} < minimum {schema['minimum']}"
        if "maximum" in schema and instance > schema["maximum"]:
            yield f"{where}: {instance} > maximum {schema['maximum']}"
        if "exclusiveMinimum" in schema and instance <= schema["exclusiveMinimum"]:
            yield f"{where}: {instance} must be > {schema['exclusiveMinimum']}"
        if "exclusiveMaximum" in schema and instance >= schema["exclusiveMaximum"]:
            yield f"{where}: {instance} must be < {schema['exclusiveMaximum']}"
        if "multipleOf" in schema:
            quotient = instance / schema["multipleOf"]
            if abs(quotient - round(quotient)) > 1e-9:
                yield f"{where}: {instance} is not a multiple of {schema['multipleOf']}"

    def _check_string(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        if not isinstance(instance, str):
            return
        where = path or "<root>"
        if "minLength" in schema and len(instance) < schema["minLength"]:
            yield f"{where}: shorter than minLength {schema['minLength']}"
        if "maxLength" in schema and len(instance) > schema["maxLength"]:
            yield f"{where}: longer than maxLength {schema['maxLength']}"
        if "pattern" in schema and not self._pattern(schema["pattern"]).search(instance):
            yield f"{where}: {json.dumps(instance)} does not match /{schema['pattern']}/"
        if schema.get("format") == "uri" and not re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", instance):
            # `format` is an annotation by default in 2020-12; asserted here
            # because every use of it in shared-spec/ means an absolute URI.
            yield f"{where}: {json.dumps(instance)} is not an absolute URI"

    def _check_logic(self, instance: Any, schema: dict, path: str) -> Iterator[str]:
        where = path or "<root>"
        for sub in schema.get("allOf", []):
            yield from self.iter_errors(instance, sub, path)
        if "anyOf" in schema:
            branch_errors = [list(self.iter_errors(instance, sub, path))
                             for sub in schema["anyOf"]]
            if all(branch_errors):
                closest = min(branch_errors, key=len)
                yield f"{where}: matches no anyOf branch (closest: {closest[0]})"
        if "oneOf" in schema:
            passing = [i for i, sub in enumerate(schema["oneOf"])
                       if self.is_valid_against(instance, sub)]
            if len(passing) != 1:
                if not passing:
                    branch_errors = [list(self.iter_errors(instance, sub, path))
                                     for sub in schema["oneOf"]]
                    closest = min(branch_errors, key=len)
                    yield f"{where}: matches no oneOf branch (closest: {closest[0]})"
                else:
                    yield f"{where}: matches {len(passing)} oneOf branches, must match exactly one"
        if "not" in schema and self.is_valid_against(instance, schema["not"]):
            yield f"{where}: must not match the `not` schema"
        if "if" in schema:
            branch = "then" if self.is_valid_against(instance, schema["if"]) else "else"
            if branch in schema:
                yield from self.iter_errors(instance, schema[branch], path)

    def is_valid_against(self, instance: Any, schema: Any) -> bool:
        for _ in self.iter_errors(instance, schema, ""):
            return False
        return True

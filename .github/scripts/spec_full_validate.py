#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Full draft-2020-12 validation of shared-spec/ — spec §25.3 (REQ-BLD-023).

`tools/validate-shared-spec.py` runs anywhere with nothing but the standard
library. That is deliberate and it is what makes it usable in a contributor's
shell, but its validator (`tools/jsonschema_mini.py`) is a documented *subset* of
draft-2020-12. The subset reports what it did not check rather than implying full
coverage, and this script is where the rest gets applied — with the real
`jsonschema` library, pinned by hash in `.github/requirements-spec.txt`.

Two modes, matching the first two assertions §25.3 names:

  --check-schemas   every file in shared-spec/schemas/ is itself a valid
                    draft-2020-12 schema, asserted against the meta-schema
  --check-fixtures  every fixture in the theme-validation corpus produces the
                    verdict index.json claims, and every EFS and smart-playlist
                    case is well-formed against its schema

Running both is the point: a schema that is invalid but never applied looks fine,
and a corpus checked against an invalid schema proves nothing.

Where this and the stdlib validator disagree, that disagreement is the finding —
it means `jsonschema_mini` implements a keyword loosely, and the report says which
keyword so the subset can be fixed rather than quietly trusted.
"""

from __future__ import annotations

import argparse
import json
import sys
from importlib.metadata import version
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SPEC = REPO / "shared-spec"
CORPUS = SPEC / "conformance" / "theme-validation-cases"

try:
    import jsonschema
    from jsonschema import Draft202012Validator
    from jsonschema.validators import validator_for
    from referencing import Registry
    from referencing.jsonschema import DRAFT202012 as DRAFT202012_SPEC
except ImportError as exc:
    print(
        f"FATAL: {exc.name} is not installed. This script is the CI half of the\n"
        "spec gate; install it with:\n"
        "    python -m pip install --require-hashes --no-deps "
        "-r .github/requirements-spec.txt\n"
        "For a validator that needs no install at all, use "
        "tools/validate-shared-spec.py — a documented draft-2020-12 subset that\n"
        "reports what it did not check.",
        file=sys.stderr,
    )
    raise SystemExit(2)

VERSION = version("jsonschema")


def load(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def schema_files() -> list[Path]:
    return sorted((SPEC / "schemas").glob("*.json"))


def registry() -> tuple[dict[str, object], "Registry"]:
    """$id → schema, plus a referencing Registry so $refs resolve offline.

    A validator that reached the network to resolve a $ref would make this gate
    depend on someone else's uptime, and REQ-SEC-013's spirit is that a build
    depends on pinned things only. Registry has no retrieval callable here, so an
    unresolvable $ref raises rather than being fetched — which is the behaviour we
    want: it means a schema referenced something shared-spec/ does not define.

    This uses `referencing`, not the deprecated jsonschema.RefResolver, so the
    script behaves the same on the locally installed 4.19 and the CI-pinned 4.26
    and does not break when RefResolver is finally removed.
    """
    store: dict[str, object] = {}
    for path in schema_files():
        doc = load(path)
        if isinstance(doc, dict) and "$id" in doc:
            store[str(doc["$id"])] = doc
    reg = Registry().with_contents(
        list(store.items()), default_specification=DRAFT202012_SPEC
    )
    return store, reg


def make_validator(schema: object, reg: "Registry"):
    cls = validator_for(schema, default=Draft202012Validator)
    return cls(schema, registry=reg)


def check_schemas() -> list[str]:
    errors: list[str] = []
    store, reg = registry()
    seen_ids: dict[str, Path] = {}

    for path in schema_files():
        rel = path.relative_to(REPO).as_posix()
        try:
            doc = load(path)
        except json.JSONDecodeError as exc:
            errors.append(f"{rel}: does not parse as JSON — {exc}")
            continue

        if not isinstance(doc, dict):
            errors.append(f"{rel}: top level is not an object")
            continue

        declared = doc.get("$schema")
        if declared != "https://json-schema.org/draft/2020-12/schema":
            errors.append(
                f"{rel}: $schema is {declared!r}; REQ-THM-010 specifies "
                "draft-2020-12 and the corpus is validated as such"
            )

        sid = doc.get("$id")
        if not isinstance(sid, str) or not sid:
            errors.append(f"{rel}: no $id — a schema without one cannot be $ref'd")
        elif sid in seen_ids and seen_ids[sid] != path:
            errors.append(
                f"{rel}: $id {sid} is already declared by "
                f"{seen_ids[sid].relative_to(REPO).as_posix()}"
            )
        else:
            seen_ids[str(sid)] = path

        # The assertion that matters: is this a valid schema at all?
        cls = validator_for(doc, default=Draft202012Validator)
        try:
            cls.check_schema(doc)
        except jsonschema.SchemaError as exc:
            location = "/".join(str(p) for p in exc.absolute_path) or "(root)"
            errors.append(f"{rel}: invalid schema at {location} — {exc.message}")
            continue

        # And does it compile with our $refs resolvable, offline?
        try:
            make_validator(doc, reg)
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{rel}: could not build a validator — {type(exc).__name__}: {exc}")

    if not schema_files():
        errors.append("shared-spec/schemas/ contains no schemas")
    return errors


def check_fixtures() -> list[str]:
    errors: list[str] = []
    store, reg = registry()
    validators: dict[str, object] = {}

    index_path = CORPUS / "index.json"
    if not index_path.exists():
        return [f"{index_path.relative_to(REPO).as_posix()}: missing"]

    index = load(index_path)
    if not isinstance(index, dict) or not isinstance(index.get("cases"), list):
        return ["theme-validation-cases/index.json: `cases` must be an array"]

    checked = 0
    for case in index["cases"]:
        rel_file = case.get("file")
        where = f"index.json[{rel_file!r}]"
        sid = case.get("schema")
        if sid is None:
            continue  # judged by code (path safety, SVG, contrast, archive limits)
        if not isinstance(rel_file, str):
            errors.append(f"{where}: `file` is not a string")
            continue

        path = CORPUS / rel_file
        if not path.exists():
            if "generated" not in case:
                errors.append(f"{where}: file absent with no `generated` recipe")
            continue

        if sid not in store:
            errors.append(f"{where}: names schema {sid!r}, which no file declares")
            continue
        if sid not in validators:
            validators[sid] = make_validator(store[sid], reg)

        try:
            document = load(path)
        except json.JSONDecodeError as exc:
            errors.append(f"{where}: fixture does not parse — {exc}")
            continue

        found = list(validators[sid].iter_errors(document))  # type: ignore[attr-defined]
        checked += 1

        # `schemaVerdict` exists because a fixture can be schema-valid and still be
        # rejected by a later REQ-THM-040 pipeline step. The schema is only ever
        # asked about its own step.
        want = case.get("schemaVerdict", case.get("verdict"))
        if want in ("accept", "accept-with-warning", "table"):
            if found:
                detail = "; ".join(
                    f"{'/'.join(str(p) for p in e.absolute_path) or '(root)'}: {e.message}"
                    for e in found[:3]
                )
                errors.append(
                    f"{where}: corpus says accept, full validator rejects — {detail}"
                )
        elif want == "reject":
            if not found:
                errors.append(
                    f"{where}: corpus says reject, but it validates cleanly against {sid}. "
                    "Either the schema is too permissive or the verdict is wrong; both "
                    "are REQ-BLD-023 failures."
                )
        else:
            errors.append(f"{where}: unknown verdict {want!r}")

    # The other two corpora: structural, since their judges are the EFS engine and
    # the smart-playlist compiler rather than a schema.
    for name, req in (
        ("efs-cases.json", "REQ-EFS-012"),
        ("smart-playlist-cases.json", "REQ-PLS-012"),
    ):
        path = SPEC / "conformance" / name
        if not path.exists():
            errors.append(f"shared-spec/conformance/{name}: missing ({req})")
            continue
        try:
            load(path)
        except json.JSONDecodeError as exc:
            errors.append(f"shared-spec/conformance/{name}: does not parse — {exc}")

    print(f"  {checked} fixture(s) validated against their schema with jsonschema")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-schemas", action="store_true")
    parser.add_argument("--check-fixtures", action="store_true")
    args = parser.parse_args()

    if not (args.check_schemas or args.check_fixtures):
        parser.error("pass --check-schemas, --check-fixtures, or both")

    errors: list[str] = []
    if args.check_schemas:
        print(f"draft-2020-12 schema validity (jsonschema {VERSION})")
        errors += check_schemas()
    if args.check_fixtures:
        print(f"fixture verdicts (jsonschema {VERSION})")
        errors += check_fixtures()

    if errors:
        print(f"\n{len(errors)} problem(s):\n", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        print(
            "\nREQ-BLD-023: every schema must be valid and every fixture must produce\n"
            "the verdict the corpus claims. A disagreement with "
            "tools/validate-shared-spec.py\nis itself the finding — it means "
            "jsonschema_mini implements a keyword loosely.",
            file=sys.stderr,
        )
        return 1

    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate shared-spec/ — the cross-platform contract.

`shared-spec/` is load-bearing: REQ-GEN-031 requires the desktop and Android engines to
produce IDENTICAL conformance verdicts, which only means something if the fixtures and the
verdicts they assert are themselves checked. This script is that check, and it runs with
nothing but the Python standard library plus tools/jsonschema_mini.py — no pip, no venv, so
it works in a bare container and in a contributor's shell alike.

What it enforces:

  structure   every .json/.eclayout parses; schemas carry $schema and a unique $id;
              every $ref resolves.
  keywords    every schema is accepted by the jsonschema_mini keyword allowlist, so an
              unimplemented keyword is a hard error here instead of a silent pass at runtime.
  fixtures    every JSON fixture is validated against its paired schema and the result is
              compared with the verdict the corpus claims.
  counts      the per-requirement minimums the spec states as numbers (REQ-EFS-012's 150
              cases, REQ-PLS-012's eight worked examples, REQ-SET-002's documented default
              for every key).
  tokens      design-system/tokens.json agrees with the values themes are allowed to use.

Exit status is 0 only when every check passes. Full JSON Schema draft-2020-12 validation
runs in .github/workflows/spec-ci.yml; this script is deliberately the subset that can run
anywhere, and it reports what it did NOT check rather than implying full coverage.

`--self-test` copies shared-spec/ to a temporary directory, plants one defect in the copy,
runs this same script against it and requires the specific complaint — fourteen times, once
per defect, plus one control run over the unmutated copy. It is end-to-end rather than
unit-level because what OQ-045 doubts is the wiring: the corpus is entirely valid today, so
a clean run is also what an inverted comparison or a never-taken branch would print.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPEC = os.path.join(ROOT, "shared-spec")

sys.path.insert(0, HERE)
try:
    from jsonschema_mini import SUPPORTED, SchemaError, Validator
except ImportError as exc:  # pragma: no cover
    print(f"FATAL: tools/jsonschema_mini.py not importable: {exc}", file=sys.stderr)
    raise SystemExit(2)

errors: list[str] = []
notes: list[str] = []
checked = 0


def fail(where: str, msg: str) -> None:
    errors.append(f"{where}: {msg}")


def load(path: str):
    global checked
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    checked += 1
    return json.loads(text)


def rel(path: str) -> str:
    return os.path.relpath(path, ROOT)


# ----------------------------------------------------------------- self-test
# Each mutation returns True when it managed to plant its defect. A mutation that
# cannot find anything to break is reported as a self-test failure, not skipped:
# it means the corpus no longer contains the shape the check was written for.

def _load(path: str):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _store(path: str, doc) -> bool:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    return True


def _corpus_index(root: str) -> str:
    return os.path.join(root, "conformance", "theme-validation-cases", "index.json")


ACCEPTING = ("accept", "accept-with-warning", "table")


def _flip_theme_verdict(root: str, *, to_reject: bool) -> bool:
    """Claim the schema rejects something it accepts, or the reverse."""
    path = _corpus_index(root)
    index = _load(path)
    for case in index.get("cases", []):
        if not case.get("schema"):
            continue
        fixture = os.path.join(os.path.dirname(path), case.get("file", ""))
        if not os.path.exists(fixture):
            continue
        effective = case.get("schemaVerdict", case.get("verdict"))
        if to_reject and effective in ACCEPTING:
            case["schemaVerdict"] = "reject"
            return _store(path, index)
        if not to_reject and effective == "reject":
            case["schemaVerdict"] = "accept"
            return _store(path, index)
    return False


def _flip_playlist_claim(root: str, *, to_invalid: bool) -> bool:
    path = os.path.join(root, "conformance", "smart-playlist-cases.json")
    doc = _load(path)
    for case in doc.get("cases", []):
        if case.get("schemaValid") is (True if to_invalid else False):
            case["schemaValid"] = not to_invalid
            return _store(path, doc)
    return False


def _drop_schema_id(root: str) -> bool:
    path = os.path.join(root, "schemas", "theme-schema.json")
    doc = _load(path)
    return doc.pop("$id", None) is not None and _store(path, doc)


def _break_a_ref(root: str) -> bool:
    schema_dir = os.path.join(root, "schemas")
    for name in sorted(os.listdir(schema_dir)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(schema_dir, name)
        doc = _load(path)
        broke = False

        def walk(node):
            nonlocal broke
            if broke:
                return
            if isinstance(node, dict):
                if isinstance(node.get("$ref"), str) and node["$ref"].startswith("#"):
                    node["$ref"] = "#/$defs/__planted_missing__"
                    broke = True
                    return
                for value in node.values():
                    walk(value)
            elif isinstance(node, list):
                for value in node:
                    walk(value)

        walk(doc)
        if broke:
            return _store(path, doc)
    return False


def _add_unsupported_keyword(root: str) -> bool:
    path = os.path.join(root, "schemas", "theme-schema.json")
    doc = _load(path)
    # Outside jsonschema_mini's allowlist, so it must be a hard error rather than
    # a keyword quietly treated as satisfied.
    doc["unevaluatedProperties"] = False
    return _store(path, doc)


def _add_orphan_fixture(root: str) -> bool:
    path = os.path.join(root, "conformance", "theme-validation-cases",
                        "_planted-orphan.json")
    with open(path, "w", encoding="utf-8") as f:
        f.write("{}\n")
    return True


def _miscount_readme(root: str) -> bool:
    path = os.path.join(root, "README.md")
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    bumped, count = re.subn(r"(\d+)( EFS cases)",
                            lambda m: f"{int(m.group(1)) + 1}{m.group(2)}",
                            text, count=1)
    if not count:
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(bumped)
    return True


def _shrink_efs_corpus(root: str) -> bool:
    path = os.path.join(root, "conformance", "efs-cases.json")
    doc = _load(path)
    cases = doc.get("cases")
    if not isinstance(cases, list) or len(cases) < 150:
        return False
    doc["cases"] = cases[:149]
    return _store(path, doc)


def _drop_a_settings_default(root: str) -> bool:
    path = os.path.join(root, "schemas", "settings.schema.json")
    schema = _load(path)

    def resolve(node):
        if isinstance(node, dict) and isinstance(node.get("$ref"), str):
            target = schema
            for part in node["$ref"].lstrip("#/").split("/"):
                if isinstance(target, dict) and part in target:
                    target = target[part]
                else:
                    return node
            return target
        return node

    groups = schema.get("$defs", {}).get("settings", {}).get("properties", {})
    for _, group in sorted(groups.items()):
        for _, key in sorted(resolve(group).get("properties", {}).items()):
            node = resolve(key)
            if isinstance(node, dict) and "default" in node and "const" not in node:
                node.pop("default")
                return _store(path, schema)
    return False


def _make_telemetry_mutable(root: str) -> bool:
    path = os.path.join(root, "schemas", "settings.schema.json")
    schema = _load(path)
    groups = schema.get("$defs", {}).get("settings", {}).get("properties", {})
    tel = groups.get("privacy", {}).get("properties", {}).get("telemetryEnabled")
    if not isinstance(tel, dict) or "const" not in tel:
        return False
    # A default of false is not the same promise as a const of false, and
    # REQ-SET-010 asks for the second one.
    tel.pop("const")
    tel["default"] = False
    return _store(path, schema)


def _splice_sql_literal(root: str) -> bool:
    path = os.path.join(root, "conformance", "smart-playlist-cases.json")
    doc = _load(path)
    for case in doc.get("cases", []):
        sql = case.get("expectSql")
        if case.get("schemaValid") and isinstance(sql, dict) and "where" in sql:
            sql["where"] = f"{sql['where']} AND artist = 'Boards of Canada'"
            return _store(path, doc)
    return False


def _remove_companion_doc(root: str) -> bool:
    path = os.path.join(root, "sync-protocol.md")
    if not os.path.exists(path):
        return False
    os.remove(path)
    return True


# (label, mutation, the complaint the run must make)
MUTATIONS = [
    ("a case claims the schema rejects what it accepts",
     lambda r: _flip_theme_verdict(r, to_reject=True), "but it validated cleanly"),
    ("a case claims the schema accepts what it rejects",
     lambda r: _flip_theme_verdict(r, to_reject=False), "but validation reported"),
    ("a rule claims schema-invalid but validates",
     lambda r: _flip_playlist_claim(r, to_invalid=True),
     "claims schema-invalid but it validated cleanly"),
    ("a rule claims schema-valid but does not validate",
     lambda r: _flip_playlist_claim(r, to_invalid=False),
     "claims schemaValid but validation reported"),
    ("a schema loses its $id", _drop_schema_id, "off-domain $id"),
    ("a local $ref points at nothing", _break_a_ref, "$ref does not resolve"),
    ("a schema uses an unimplemented keyword", _add_unsupported_keyword,
     "outside the implemented subset"),
    ("a fixture is on disk but unlisted", _add_orphan_fixture,
     "absent from index.json"),
    ("the README overstates the case count", _miscount_readme, "cases; there are"),
    ("the EFS corpus falls below its stated minimum", _shrink_efs_corpus,
     "at least 150 cases"),
    ("a settings key loses its documented default", _drop_a_settings_default,
     "no documented default"),
    ("telemetry becomes a mutable default", _make_telemetry_mutable,
     "telemetryEnabled must be"),
    ("a literal is spliced into expected SQL", _splice_sql_literal,
     "quoted literal"),
    ("a companion document goes missing", _remove_companion_doc,
     "sync-protocol.md: missing"),
]


def _run_against(spec_root: str) -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, os.path.abspath(__file__), "--spec-root", spec_root],
        capture_output=True, text=True, check=False)
    return proc.returncode, proc.stdout + proc.stderr


def self_test() -> int:
    failures: list[str] = []
    source = os.path.join(ROOT, "shared-spec")
    with tempfile.TemporaryDirectory(prefix="eclipse-spec-") as tmp:
        pristine = os.path.join(tmp, "pristine")
        shutil.copytree(source, pristine)

        # The control run. Without it, fourteen red runs would be equally
        # consistent with a copy step that breaks the tree on its own.
        code, output = _run_against(pristine)
        if code != 0:
            failures.append("the unmutated copy did not pass, so every failure "
                            f"below is suspect:\n{output.strip()[:800]}")

        for number, (label, mutate, expected) in enumerate(MUTATIONS, start=1):
            work = os.path.join(tmp, f"case{number:02d}")
            shutil.copytree(pristine, work)
            if not mutate(work):
                failures.append(f"{label}: nothing in the corpus had the shape this "
                                "mutation needs, so the check went unexercised")
                continue
            code, output = _run_against(work)
            if code == 0:
                failures.append(f"{label}: the mutated tree still passed")
            elif expected not in output:
                failures.append(f"{label}: it failed, but not about that — "
                                f"{expected!r} is absent from the output")

    if failures:
        print(f"shared-spec self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  \u2717 {f}", file=sys.stderr)
        return 1
    print(f"shared-spec self-test: {len(MUTATIONS)} planted defect(s), each caught with "
          f"the right complaint, over an unmutated control that passes")
    return 0


# ------------------------------------------------------------------- arguments
_parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
_parser.add_argument("--spec-root", default=None,
                     help="validate a different tree (used by --self-test)")
_parser.add_argument("--self-test", action="store_true",
                     help="plant defects in a copy of shared-spec/ and require each "
                          "to be caught")
_args = _parser.parse_args()
if _args.self_test:
    raise SystemExit(self_test())
if _args.spec_root:
    SPEC = os.path.abspath(_args.spec_root)


# --------------------------------------------------------------------- 1. parse
json_files: list[str] = []
for dirpath, dirnames, filenames in os.walk(SPEC):
    dirnames.sort()
    for name in sorted(filenames):
        if name.endswith((".json", ".eclayout")):
            json_files.append(os.path.join(dirpath, name))

docs: dict[str, object] = {}
for path in json_files:
    try:
        docs[path] = load(path)
    except (OSError, json.JSONDecodeError) as exc:
        fail(rel(path), f"does not parse: {exc}")

if not json_files:
    fail("shared-spec", "no JSON documents found — the directory is empty")

# ------------------------------------------------------------------- 2. schemas
schema_dir = os.path.join(SPEC, "schemas")
schemas: dict[str, dict] = {}   # $id -> schema
schema_paths: dict[str, str] = {}
for path in sorted(json_files):
    if os.path.dirname(path) != schema_dir:
        continue
    doc = docs.get(path)
    if not isinstance(doc, dict):
        fail(rel(path), "a schema must be a JSON object")
        continue
    if doc.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        fail(rel(path), "missing or wrong $schema (draft 2020-12 is required)")
    sid = doc.get("$id")
    if not isinstance(sid, str) or not sid.startswith("https://eclipse-player.org/schemas/"):
        fail(rel(path), f"missing or off-domain $id: {sid!r}")
        continue
    if sid in schemas:
        fail(rel(path), f"$id collides with {rel(schema_paths[sid])}: {sid}")
        continue
    schemas[sid] = doc
    schema_paths[sid] = path

    # Keyword allowlist. Constructing the validator raises on anything unimplemented,
    # which is the point: an unknown keyword must never be mistaken for a satisfied one.
    try:
        Validator(doc, check_keywords=True)
    except SchemaError as exc:
        fail(rel(path), f"uses a keyword outside the implemented subset: {exc}")

for expected in ("theme", "skin-manifest", "layout", "settings", "smart-playlist"):
    want = f"https://eclipse-player.org/schemas/{expected}/v1"
    if want not in schemas:
        fail("shared-spec/schemas", f"no schema declares $id {want}")

# ---------------------------------------------------------------------- 3. $ref
REF_LOCAL = re.compile(r"^#(/.*)?$")


def walk_refs(node, path: str, sid: str, root: dict) -> None:
    if isinstance(node, dict):
        ref = node.get("$ref")
        if isinstance(ref, str):
            if not REF_LOCAL.match(ref):
                fail(f"{sid}{path}", f"non-local $ref {ref!r}; shared-spec schemas are "
                                     "self-contained so a validator needs no resolver")
            else:
                target = root
                for part in ref[2:].split("/") if len(ref) > 1 else []:
                    part = part.replace("~1", "/").replace("~0", "~")
                    if isinstance(target, dict) and part in target:
                        target = target[part]
                    else:
                        fail(f"{sid}{path}", f"$ref does not resolve: {ref}")
                        break
        for key, value in node.items():
            walk_refs(value, f"{path}/{key}", sid, root)
    elif isinstance(node, list):
        for i, value in enumerate(node):
            walk_refs(value, f"{path}/{i}", sid, root)


broken: set[str] = set()
before = len(errors)
for sid, schema in schemas.items():
    mark = len(errors)
    walk_refs(schema, "", sid, schema)
    if len(errors) > mark:
        broken.add(sid)
if len(errors) > before:
    notes.append(f"{len(broken)} schema(s) failed structural checks; fixtures paired with them "
                 "were not validated, so this run is incomplete rather than clean")

# ------------------------------------------------- 4. theme-validation corpus
corpus_dir = os.path.join(SPEC, "conformance", "theme-validation-cases")
index_path = os.path.join(corpus_dir, "index.json")
if not os.path.exists(index_path):
    fail("shared-spec/conformance/theme-validation-cases", "index.json is missing — the "
         "corpus is unusable without the verdicts (REQ-GEN-031)")
else:
    index = docs.get(index_path)
    cases = index.get("cases") if isinstance(index, dict) else None
    if not isinstance(cases, list) or not cases:
        fail("theme-validation-cases/index.json", "`cases` must be a non-empty array")
        cases = []

    listed: set[str] = set()
    for case in cases:
        where = f"index.json[{case.get('file')!r}]"
        f_rel = case.get("file")
        if not isinstance(f_rel, str):
            fail(where, "`file` must be a string")
            continue
        listed.add(f_rel)
        verdict = case.get("verdict")
        if verdict not in ("accept", "reject", "accept-with-warning", "table"):
            fail(where, f"unknown verdict {verdict!r}")
        for required in ("pipelineStep", "reason", "req"):
            if required not in case:
                fail(where, f"missing `{required}`")

        f_abs = os.path.join(corpus_dir, f_rel)
        generated = case.get("generated")
        if not os.path.exists(f_abs):
            if generated is None:
                fail(where, "file does not exist and no `generated` recipe is given")
            elif not isinstance(generated, dict) or "kind" not in generated:
                fail(where, "`generated` must be an object with a `kind`")
            continue
        if generated is not None:
            fail(where, "both a committed file and a `generated` recipe — pick one")

        sid = case.get("schema")
        if sid is None:
            # Judged by code, not by a schema. Assert only that the artifact is readable
            # and, for SVG, that it is the bytes the sanitiser will see.
            if f_rel.endswith(".json") and f_abs not in docs:
                fail(where, "JSON fixture did not parse")
            continue
        if sid not in schemas:
            fail(where, f"names schema {sid!r}, which no file in schemas/ declares")
            continue
        fixture = docs.get(f_abs)
        if fixture is None:
            fail(where, "fixture did not parse")
            continue

        if sid in broken:
            fail(where, f"skipped: schema {sid} is itself invalid")
            continue
        try:
            validator = Validator(schemas[sid], check_keywords=False)
            found = list(validator.iter_errors(fixture))
        except Exception as exc:            # noqa: BLE001 - any failure is a finding
            fail(where, f"validation raised {type(exc).__name__}: {exc}")
            continue
        # `schemaVerdict` lets a case be schema-valid while a later pipeline step rejects it.
        want = case.get("schemaVerdict", verdict)
        if want in ("accept", "accept-with-warning", "table"):
            if found:
                fail(where, "corpus says the schema accepts this, but validation reported: "
                            + "; ".join(str(e) for e in found[:3]))
        elif want == "reject":
            if not found:
                fail(where, f"corpus says the schema rejects this, but it validated cleanly "
                            f"against {sid}")

    on_disk: set[str] = set()
    for dirpath, dirnames, filenames in os.walk(corpus_dir):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.relpath(os.path.join(dirpath, name), corpus_dir)
            if p != "index.json" and not p.endswith(".md"):
                on_disk.add(p.replace(os.sep, "/"))
    for orphan in sorted(on_disk - listed):
        fail("theme-validation-cases", f"{orphan} is on disk but absent from index.json — an "
             "unlisted fixture is a case neither engine is asked to agree on")

# ------------------------------------------------------ 5. EFS conformance
efs_path = os.path.join(SPEC, "conformance", "efs-cases.json")
if not os.path.exists(efs_path):
    fail("shared-spec/conformance/efs-cases.json", "missing (REQ-EFS-012)")
else:
    efs = docs.get(efs_path, {})
    cases = efs.get("cases", []) if isinstance(efs, dict) else []
    tracks = efs.get("tracks", {}) if isinstance(efs, dict) else {}
    if len(cases) < 150:
        fail("efs-cases.json", f"REQ-EFS-012 requires at least 150 cases; found {len(cases)}")
    seen: set[str] = set()
    for i, case in enumerate(cases):
        where = f"efs-cases.json[{case.get('id', i)}]"
        cid = case.get("id")
        if not isinstance(cid, str) or not cid:
            fail(where, "`id` must be a non-empty string")
        elif cid in seen:
            fail(where, "duplicate id")
        else:
            seen.add(cid)
        if "pattern" not in case:
            fail(where, "missing `pattern`")
        if "expect" not in case:
            fail(where, "missing `expect` — a case with no expectation asserts nothing")
        track = case.get("track")
        if track is not None and track not in tracks:
            fail(where, f"references undefined track {track!r}")
        exp = case.get("expect")
        if isinstance(exp, str) and len(exp) > 4096:
            fail(where, f"expected output is {len(exp)} chars, over the REQ-EFS-009 cap of 4096")

    # Every function in the §10.5 closed library needs at least one case (REQ-EFS-008/012).
    FUNCS = ("if if2 if3 ifequal ifgreater ifless iflonger upper lower title caps trim len "
             "sub left right pad padright cut abbr replace strchr strstr insert repeat "
             "meta_sep add sub2 mul div mod min max num round abs time timems date year age "
             "char crlf tab progress stars fixed").split()
    joined = " ".join(str(c.get("pattern", "")) for c in cases)
    missing = [fn for fn in FUNCS if f"${fn}(" not in joined]
    if missing:
        fail("efs-cases.json", "no case exercises: " + ", ".join("$" + m for m in missing))

# --------------------------------------------- 6. smart-playlist conformance
sp_path = os.path.join(SPEC, "conformance", "smart-playlist-cases.json")
sp_schema = "https://eclipse-player.org/schemas/smart-playlist/v1"
if not os.path.exists(sp_path):
    fail("shared-spec/conformance/smart-playlist-cases.json", "missing (REQ-PLS-012)")
elif sp_schema in schemas:
    sp = docs.get(sp_path, {})
    cases = sp.get("cases", []) if isinstance(sp, dict) else []
    if sp_schema in broken:
        fail("smart-playlist-cases.json", "paired schema is itself invalid; cases unchecked")
        cases = []
    validator = Validator(schemas[sp_schema], check_keywords=False)
    worked = 0
    for i, case in enumerate(cases):
        where = f"smart-playlist-cases.json[{case.get('id', i)}]"
        if case.get("req") == "REQ-PLS-012":
            worked += 1
        rule = case.get("rule")
        claim = case.get("schemaValid")
        if claim is None:
            fail(where, "missing `schemaValid`")
            continue
        try:
            found = list(validator.iter_errors(rule))
        except Exception as exc:            # noqa: BLE001
            fail(where, f"validation raised {type(exc).__name__}: {exc}")
            continue
        if claim and found:
            fail(where, "claims schemaValid but validation reported: "
                        + "; ".join(str(e) for e in found[:3]))
        if not claim and not found:
            fail(where, "claims schema-invalid but it validated cleanly")
        if claim and "expectSql" in case:
            sql = case["expectSql"]
            where_clause = sql.get("where", "")
            params = sql.get("params")
            if not isinstance(params, list):
                fail(where, "`expectSql.params` must be an array")
            elif where_clause.count("?") != len(params):
                fail(where, f"{where_clause.count('?')} placeholders but "
                            f"{len(params)} parameters — REQ-PLS-010 forbids interpolation, so "
                            "the counts must match exactly")
            # REQ-SEC-009: no literal may be spliced into SQL. `ESCAPE '<char>'` is
            # SQL syntax with a fixed operand — it carries no user data — so it is removed
            # before the search rather than being an exception inside it.
            probe = re.sub(r"ESCAPE '(?:\\\\|.)'", "", where_clause)
            if "'" in probe:
                fail(where, "expected SQL contains a quoted literal; every value must be a "
                            "bound parameter (REQ-SEC-009)")
    if worked < 8:
        fail("smart-playlist-cases.json",
             f"REQ-PLS-012 gives eight worked examples; only {worked} are tagged")

# ------------------------------------------------------------- 7. settings
set_schema = "https://eclipse-player.org/schemas/settings/v1"
if set_schema in schemas:
    schema = schemas[set_schema]
    groups = (schema.get("$defs", {}).get("settings", {}).get("properties", {}))
    if not groups:
        fail("settings.schema.json", "$defs.settings.properties is empty")
    def resolve(node):
        """Follow a local $ref one hop, so a key may document its default in $defs."""
        if isinstance(node, dict) and isinstance(node.get("$ref"), str):
            target = schema
            for part in node["$ref"].lstrip("#/").split("/"):
                if isinstance(target, dict) and part in target:
                    target = target[part]
                else:
                    return node
            return target
        return node

    def documents_default(node, depth=0):
        """A key documents its default directly, or is a composite whose every leaf does.

        REQ-SET-002 asks for a documented default per key; a group of related patterns
        such as `appearance.efsPatterns` satisfies it by giving one per pattern rather
        than a default for the whole object, which would have to restate all twelve.
        """
        node = resolve(node)
        if not isinstance(node, dict):
            return False
        if "default" in node or "const" in node:
            return True
        if depth < 3 and node.get("type") == "object":
            props = node.get("properties")
            if isinstance(props, dict) and props:
                return all(documents_default(v, depth + 1) for v in props.values())
        return False

    total = 0
    for gname, group in sorted(groups.items()):
        keys = resolve(group).get("properties", {})
        if not keys:
            fail("settings.schema.json", f"group {gname!r} declares no keys")
        for kname, key in sorted(keys.items()):
            total += 1
            if not documents_default(key):
                fail("settings.schema.json",
                     f"{gname}.{kname} has no documented default (REQ-SET-002)")
    if total < 50:
        fail("settings.schema.json", f"only {total} keys — §19.1 inventories far more")
    privacy = groups.get("privacy", {}).get("properties", {})
    tel = privacy.get("telemetryEnabled", {})
    if tel.get("const") is not False:
        fail("settings.schema.json", "privacy.telemetryEnabled must be `const: false` — "
             "REQ-SET-010 makes telemetry permanently off, which a mutable default with the "
             "value false does not express")

# ------------------------------------------------------------ 8. tokens
tokens_path = os.path.join(SPEC, "design-system", "tokens.json")
if not os.path.exists(tokens_path):
    fail("shared-spec/design-system/tokens.json", "missing (REQ-UIX-001)")
else:
    tokens = docs.get(tokens_path, {})
    for group in ("spacing", "typography", "radius", "elevation", "motion"):
        if group not in tokens:
            fail("tokens.json", f"missing the {group!r} group required by §12.1")
    scale = tokens.get("typography", {}).get("scale", {})
    for style in ("display", "headline", "title", "body", "label", "caption", "mono"):
        if style not in scale:
            fail("tokens.json", f"typography.scale is missing {style!r} (§12.1 has seven)")
    # A theme may only express what the token file describes.
    if "https://eclipse-player.org/schemas/theme/v1" in schemas:
        theme_styles = (schemas["https://eclipse-player.org/schemas/theme/v1"]
                        .get("properties", {}).get("typography", {})
                        .get("properties", {}).get("scale", {}).get("properties", {}))
        extra = set(theme_styles) - set(scale)
        if extra:
            fail("tokens.json", "theme-schema allows type styles the token file does not "
                                f"define: {sorted(extra)}")
    dur = tokens.get("motion", {}).get("duration", {})
    for name in ("instant", "fast", "normal", "slow"):
        if name not in dur:
            fail("tokens.json", f"motion.duration is missing {name!r}")

# -------------------------------------------------------- 9. companion docs
for required in ("README.md", "sync-protocol.md",
                 "grammars/eclipse-format-strings.ebnf", "grammars/smart-playlist.ebnf",
                 "design-system/typography.md", "design-system/motion.md",
                 "design-system/iconography.md"):
    if not os.path.exists(os.path.join(SPEC, required)):
        fail(f"shared-spec/{required}", "missing")

# ------------------------------------------- 10. the README's own arithmetic
# shared-spec/README.md states case counts. A README that overstates coverage is worse
# than one that omits it, so the numbers are asserted rather than trusted.
readme = os.path.join(SPEC, "README.md")
if os.path.exists(readme):
    with open(readme, "r", encoding="utf-8") as f:
        text = f.read()
    actual = {
        "efs": len((docs.get(efs_path) or {}).get("cases", [])),
        "playlist": len((docs.get(sp_path) or {}).get("cases", [])),
        "theme": len((docs.get(index_path) or {}).get("cases", [])),
    }
    actual["total"] = sum(actual.values())
    for label, pattern in (
            ("efs", r"(\d+) EFS cases"),
            ("playlist", r"(\d+) rule.SQL cases"),
            ("theme", r"(\d+) package-validation cases"),
            ("total", r"agreeing on (\d+) cases")):
        m = re.search(pattern, text)
        if m is None:
            fail("shared-spec/README.md", f"no longer states the {label} case count; the "
                 "claim was removed rather than corrected")
        elif int(m.group(1)) != actual[label]:
            fail("shared-spec/README.md",
                 f"claims {m.group(1)} {label} cases; there are {actual[label]}")

# ------------------------------------------------------------------- report
notes.append(f"{len(schemas)} schemas, {checked} JSON documents parsed")
notes.append(f"keyword allowlist: {len(SUPPORTED)} keywords implemented by jsonschema_mini")
notes.append("NOT checked here (runs in .github/workflows/spec-ci.yml): full draft-2020-12 "
             "semantics, `format` assertions, SVG sanitisation, ZIP limits, contrast "
             "computation, and EFS/smart-playlist evaluation — those need the C++ engine")

print("shared-spec validation")
for note in notes:
    print(f"  · {note}")
if errors:
    print(f"\n{len(errors)} problem(s):", file=sys.stderr)
    for e in errors:
        print(f"  ✗ {e}", file=sys.stderr)
    raise SystemExit(1)
print("  ✓ all checks passed")

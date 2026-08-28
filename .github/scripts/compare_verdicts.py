#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Cross-implementation verdict agreement — spec §25.3, REQ-TST-021, REQ-GEN-031.

`REQ-GEN-031` calls `shared-spec/conformance/` *load-bearing*: both the desktop
and the Android implementation MUST run the same fixtures through their own
theme validator and MUST produce identical results. That is the claim "shared
format" rests on, and this script is the only place it is actually tested.
`REQ-TST-021` makes it a failing condition, not a report.

Each implementation's `theme-validate --report <file>` writes one verdict report;
CI uploads them as `verdicts-<implementation>` artifacts and this script compares
whatever was downloaded.

## Report format

This file is the contract. `tools/theme-validate` is written to match it, and any
future Android reporter must emit the same shape:

    {
      "tool": "theme-validate",
      "implementation": "desktop",          // unique per report; names the artifact
      "corpus": "shared-spec/conformance/theme-validation-cases",
      "corpusFingerprint": "<sha256 of index.json, hex>",
      "generatedBy": {"version": "0.1.0", "commit": "873e5be"},
      "cases": [
        {"file": "malicious/zip-slip.arrowskin",
         "verdict": "reject",
         "pipelineStep": 2,                  // optional, diagnostic only
         "reason": "entry escapes the archive root"}   // optional
      ]
    }

`corpusFingerprint` is not ceremony. Two reports that agree while having been
produced against different revisions of the corpus prove nothing at all, and that
failure is invisible without a fingerprint — so a mismatch is an error here rather
than a footnote.

## What counts as disagreement

`verdict` is the result, so verdicts are compared strictly: any difference fails.

`pipelineStep` is compared too, but a difference is a **warning**. A fixture can
violate two steps at once — a skin can be both zip-slip and schema-invalid — and
`REQ-THM-040` stops at the first failure, so two engines may legitimately report
different steps while both correctly rejecting. `REQ-GEN-031` requires identical
*results*, not identical internal ordering. The warning still prints because a
step divergence usually does mean one engine ordered its pipeline wrongly, and
that is worth a human look even when the verdict is right. See OQ-029.

Coverage is checked before agreement: a report that silently omits cases would
"agree" trivially, which is the easiest way for this gate to become decorative.
"""

from __future__ import annotations

import hashlib
import json
import sys
from itertools import combinations
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
INDEX = REPO / "shared-spec" / "conformance" / "theme-validation-cases" / "index.json"

VERDICTS = {"accept", "reject", "accept-with-warning", "table"}


def annotate(level: str, message: str) -> None:
    """Emit a GitHub annotation, and the same text plainly for a local run."""
    one_line = message.replace("\n", " ")
    print(f"::{level} ::{one_line}")


def fingerprint(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fingerprint_of_index() -> str:
    return fingerprint(INDEX)


ALT_KEYS = ("schemaVerdict", "enforcedVerdict", "stepTenVerdict")


def corpus() -> tuple[dict[str, str], dict[str, str], dict[str, dict[str, str]]]:
    """The reference answers, the open conflicts, and the alternate readings.

    `verdict` is what an engine must report in its default configuration — the
    index states that invariant and it holds for all 122 cases. The `*Verdict`
    keys are alternate readings that are deliberately *not* the default, and they
    are loaded here only to make a failure legible: an engine reporting one of
    them has picked a defensible other reading, which is a very different bug
    report from an engine reporting nonsense.
    """
    index = json.loads(INDEX.read_text(encoding="utf-8"))
    expected = {c["file"]: c["verdict"] for c in index["cases"]}
    conflicts = {
        c["file"]: c.get("conflict", "unresolved specification conflict")
        for c in index["cases"]
        if c["file"] in set(index.get("openConflicts", []))
    }
    alternates = {
        c["file"]: {k: c[k] for k in ALT_KEYS if k in c}
        for c in index["cases"]
        if any(k in c for k in ALT_KEYS)
    }
    return expected, conflicts, alternates


def explain(name: str, verdicts: tuple[str, ...], conflicts: dict[str, str],
            alternates: dict[str, dict[str, str]]) -> str:
    """Why this particular mismatch may not be an engine bug at all."""
    notes = []
    for key, value in alternates.get(name, {}).items():
        if value in verdicts:
            verdict = value
            notes.append(
                f"{verdict!r} is this fixture's `{key}` — a reading the corpus "
                "records but does not make the default"
            )
    if name in conflicts:
        notes.append(
            f"this fixture is in `openConflicts` ({conflicts[name]}); its verdict is "
            "still a specification decision, so the fix is to resolve that — see "
            "docs/OPEN-QUESTIONS.md — not to patch an engine"
        )
    return ("\n      ↳ " + "; ".join(notes)) if notes else ""


def load_reports(root: Path) -> tuple[list[dict], list[str]]:
    errors: list[str] = []
    reports: list[dict] = []
    seen: dict[str, Path] = {}

    for path in sorted(root.rglob("*.json")):
        rel = path.relative_to(root).as_posix()
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            errors.append(f"{rel}: does not parse as JSON — {exc}")
            continue
        if not isinstance(doc, dict):
            errors.append(f"{rel}: top level is not an object")
            continue

        impl = doc.get("implementation")
        if not isinstance(impl, str) or not impl:
            errors.append(f"{rel}: no `implementation` — a report must say whose it is")
            continue
        if impl in seen:
            errors.append(
                f"{rel}: `implementation` is {impl!r}, already claimed by "
                f"{seen[impl].relative_to(root).as_posix()}. Two reports from the same "
                "implementation agreeing with each other proves nothing."
            )
            continue
        seen[impl] = path

        cases = doc.get("cases")
        if not isinstance(cases, list):
            errors.append(f"{rel}: `cases` must be an array")
            continue

        by_file: dict[str, dict] = {}
        for i, case in enumerate(cases):
            if not isinstance(case, dict):
                errors.append(f"{rel}: cases[{i}] is not an object")
                continue
            name = case.get("file")
            verdict = case.get("verdict")
            if not isinstance(name, str) or not name:
                errors.append(f"{rel}: cases[{i}] has no `file`")
                continue
            if verdict not in VERDICTS:
                errors.append(
                    f"{rel}: {name}: verdict {verdict!r} is not one of "
                    f"{sorted(VERDICTS)}"
                )
                continue
            if name in by_file:
                errors.append(f"{rel}: {name} appears twice")
                continue
            by_file[name] = case

        doc["_path"] = rel
        doc["_by_file"] = by_file
        reports.append(doc)

    return reports, errors


def check_corpus_revision(reports: list[dict]) -> list[str]:
    errors: list[str] = []
    want = fingerprint(INDEX)
    for report in reports:
        got = report.get("corpusFingerprint")
        if not isinstance(got, str) or not got:
            errors.append(
                f"{report['_path']}: no `corpusFingerprint`. Without it, two reports "
                "produced against different corpus revisions would compare as agreeing."
            )
        elif got != want:
            errors.append(
                f"{report['_path']}: corpusFingerprint {got[:12]}… does not match the "
                f"checked-out index.json ({want[:12]}…). This report was produced "
                "against a different revision of the corpus."
            )
    return errors


def check_coverage(reports: list[dict], expected: dict[str, str]) -> list[str]:
    errors: list[str] = []
    for report in reports:
        got = set(report["_by_file"])
        missing = sorted(set(expected) - got)
        extra = sorted(got - set(expected))
        if missing:
            shown = ", ".join(missing[:5]) + (f", …+{len(missing) - 5}" if len(missing) > 5 else "")
            errors.append(
                f"{report['_path']}: {len(missing)} corpus case(s) absent from the "
                f"report: {shown}. An omitted case agrees with everything."
            )
        if extra:
            shown = ", ".join(extra[:5]) + (f", …+{len(extra) - 5}" if len(extra) > 5 else "")
            errors.append(
                f"{report['_path']}: {len(extra)} case(s) not in index.json: {shown}. "
                "Every fixture an engine judges must be a tracked case."
            )
    return errors


def check_against_index(reports: list[dict], expected: dict[str, str],
                        conflicts: dict[str, str],
                        alternates: dict[str, dict[str, str]]) -> list[str]:
    """Agreement with each other is necessary; agreement with the corpus is the point.

    Two engines can agree and both be wrong. index.json holds the verdict the
    specification requires, so it is the third party in every comparison.
    """
    errors: list[str] = []
    for report in reports:
        for name, case in sorted(report["_by_file"].items()):
            want = expected.get(name)
            if want is not None and case["verdict"] != want:
                errors.append(
                    f"{report['implementation']}: {name}: reported "
                    f"{case['verdict']!r}, index.json requires {want!r}"
                    + explain(name, (case["verdict"],), conflicts, alternates)
                )
    return errors


def compare(reports: list[dict], conflicts: dict[str, str],
            alternates: dict[str, dict[str, str]]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    for left, right in combinations(reports, 2):
        pair = f"{left['implementation']} vs {right['implementation']}"
        shared = sorted(set(left["_by_file"]) & set(right["_by_file"]))
        disagreed = 0
        for name in shared:
            a, b = left["_by_file"][name], right["_by_file"][name]
            if a["verdict"] != b["verdict"]:
                disagreed += 1
                errors.append(
                    f"{pair}: {name}: {left['implementation']} says {a['verdict']!r}, "
                    f"{right['implementation']} says {b['verdict']!r}"
                    + explain(name, (a["verdict"], b["verdict"]), conflicts,
                              alternates)
                )
            elif a.get("pipelineStep") != b.get("pipelineStep") and (
                a.get("pipelineStep") is not None and b.get("pipelineStep") is not None
            ):
                warnings.append(
                    f"{pair}: {name}: same verdict {a['verdict']!r}, but rejected at "
                    f"step {a['pipelineStep']} vs {b['pipelineStep']}. Legitimate when "
                    "the fixture violates both steps; a pipeline-ordering bug otherwise "
                    "(OQ-029)."
                )
        print(f"  {pair}: {len(shared)} shared case(s), {disagreed} disagreement(s)")

    return errors, warnings


def gate(root: Path) -> int:
    if not root.is_dir():
        print(f"FATAL: {root} is not a directory", file=sys.stderr)
        return 2
    if not INDEX.exists():
        print(f"FATAL: {INDEX} is missing; there is nothing to compare against", file=sys.stderr)
        return 2

    print("cross-implementation verdict agreement (REQ-TST-021, REQ-GEN-031)")

    reports, errors = load_reports(root)
    expected, conflicts, alternates = corpus()
    print(f"  corpus: {len(expected)} case(s), index.json {fingerprint(INDEX)[:12]}…")
    if conflicts:
        print(f"  {len(conflicts)} case(s) in openConflicts — a mismatch there still "
              "fails, but is reported as the open conflict it is")
    print(f"  found {len(reports)} usable verdict set(s): "
          f"{', '.join(r['implementation'] for r in reports) or '(none)'}")

    if not reports and not errors:
        print(
            "\nFATAL: no verdict reports at all. The `native` job is supposed to upload\n"
            "verdicts-desktop; if it succeeded and this is empty, the artifact wiring is\n"
            "broken and this gate is silently passing nothing.",
            file=sys.stderr,
        )
        return 1

    errors += check_corpus_revision(reports)
    errors += check_coverage(reports, expected)
    errors += check_against_index(reports, expected, conflicts, alternates)

    warnings: list[str] = []
    if len(reports) >= 2:
        pair_errors, warnings = compare(reports, conflicts, alternates)
        errors += pair_errors

    for warning in warnings:
        annotate("warning", warning)

    if errors:
        print(f"\n{len(errors)} problem(s):\n", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        annotate("error", f"REQ-TST-021: {len(errors)} conformance disagreement(s)")
        return 1

    if len(reports) == 1:
        annotate(
            "warning",
            f"compared 1 verdict set ({reports[0]['implementation']}), need 2. "
            "REQ-GEN-031 is only half-proven until a second implementation runs the "
            "same corpus; tracked as OQ-018. This job is not skipped, because a green "
            "check next to 'both platforms agree' would be a lie while there is one.",
        )
        print(
            "\n  The one report present is internally consistent and matches index.json.\n"
            "  That is everything checkable with a single implementation — it is not\n"
            "  agreement, and it is not reported as agreement."
        )
        return 0

    print(f"\n  {len(reports)} implementations agree on all {len(expected)} cases.")
    return 0


# ---------------------------------------------------------------------------
#  Self-test.
#
#  A gate nobody has watched fail is not a gate. These ten scenarios drive the
#  real code path above against synthetic reports built from the live corpus, and
#  they are committed for the same reason the denylist's corpus is: behaviour
#  tuned once and never re-checked is exactly what rots. Run:
#
#      python3 .github/scripts/compare_verdicts.py --self-test
# ---------------------------------------------------------------------------

def _report(impl: str, mutate=None, fingerprint: str | None = None) -> dict:
    index = json.loads(INDEX.read_text(encoding="utf-8"))
    doc = {
        "tool": "theme-validate",
        "implementation": impl,
        "corpus": "shared-spec/conformance/theme-validation-cases",
        "corpusFingerprint": fingerprint or fingerprint_of_index(),
        "generatedBy": {"version": "0.0.0", "commit": "self-test"},
        "cases": [
            {"file": c["file"], "verdict": c["verdict"],
             "pipelineStep": c.get("pipelineStep")}
            for c in index["cases"]
        ],
    }
    if mutate:
        mutate(doc)
    return doc


def _flip_a_reject(doc: dict) -> None:
    for case in doc["cases"]:
        if case["verdict"] == "reject":
            case["verdict"] = "accept"
            return


def _alternate_reading(doc: dict) -> None:
    """Adopt the step-10 reading the corpus records but does not make default."""
    for case in doc["cases"]:
        if case["file"] == "malicious/layout-efs-output-bomb.eclayout":
            case["verdict"] = "reject"
            return


def _bump_a_step(doc: dict) -> None:
    for case in doc["cases"]:
        if case.get("pipelineStep") is not None:
            case["pipelineStep"] = 99
            return


def _drop_a_case(doc: dict) -> None:
    doc["cases"].pop(0)


SCENARIOS: list[tuple[str, list[tuple[str, dict]], int]] = [
    ("no reports at all — the artifact wiring is broken", [], 1),
    ("one correct set (the state today: a warning, not a pass)",
     [("desktop", _report("desktop"))], 0),
    ("two agreeing sets", [("desktop", _report("desktop")),
                           ("android", _report("android"))], 0),
    ("two sets, one flipped verdict",
     [("desktop", _report("desktop")),
      ("android", _report("android", _flip_a_reject))], 1),
    ("a report that quietly omits a case",
     [("desktop", _report("desktop", _drop_a_case))], 1),
    ("a report from a different corpus revision",
     [("desktop", _report("desktop", fingerprint="0" * 64))], 1),
    ("the same implementation uploaded twice",
     [("desktop", _report("desktop")), ("desktop2", _report("desktop"))], 1),
    ("same verdicts, divergent pipeline step — warns, does not fail",
     [("desktop", _report("desktop")),
      ("android", _report("android", _bump_a_step))], 0),
    ("one engine takes the recorded-but-not-default step-10 reading",
     [("desktop", _report("desktop", _alternate_reading))], 1),
    ("two engines split across the open conflict",
     [("desktop", _report("desktop")),
      ("android", _report("android", _alternate_reading))], 1),
]


def self_test() -> int:
    import io
    import tempfile
    from contextlib import redirect_stdout, redirect_stderr

    failures = 0
    for label, reports, expected in SCENARIOS:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, doc in reports:
                folder = root / f"verdicts-{name}"
                folder.mkdir()
                (folder / f"{name}-verdicts.json").write_text(
                    json.dumps(doc), encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                got = gate(root)
        ok = got == expected
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} exit={got} (want {expected})  {label}")

    if failures:
        print(f"\n{failures} scenario(s) behaved differently from the committed "
              "expectation.", file=sys.stderr)
        return 1
    print(f"\n  {len(SCENARIOS)} scenarios, all as expected.")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--self-test":
        print("compare_verdicts self-test")
        return self_test()
    if len(argv) != 2:
        print(f"usage: {Path(argv[0]).name} <directory-of-verdict-reports>\n"
              f"       {Path(argv[0]).name} --self-test", file=sys.stderr)
        return 2
    return gate(Path(argv[1]))


if __name__ == "__main__":
    sys.exit(main(sys.argv))

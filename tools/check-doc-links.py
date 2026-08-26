#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Documentation gate — spec §27 (REQ-GEN-075).

REQ-GEN-075 states two things a script can check mechanically:

  1. Each document in the §27 table MUST exist.
  2. CI MUST fail on broken internal links.

Everything else in that table — "with at least the stated content" — is a
judgement a reader makes, not something a linter can assert, and this script does
not pretend otherwise. What it can do is make the two mechanical halves true, and
name the requirements that are *deliberately* not yet met rather than letting an
absent file look like an oversight.

Internal links checked:

  file links       [text](../docs/FOO.md), with or without a #fragment
  fragments        the #anchor must correspond to a heading in the target file,
                   using GitHub's slug algorithm
  same-file links  [text](#section) against that file's own headings
  images           ![alt](path)
  reference links  [text][label] plus the [label]: target definition

Deliberately NOT checked: external http(s) URLs (a network fetch does not belong
in a gate that must pass offline — REQ-BLD-023's spirit and OQ-026's lesson), and
`mailto:`.

Also checked, because §0.1 rule 1 makes `docs/OPEN-QUESTIONS.md` load-bearing and
the whole repository cites it by id:

  ids are unique and contiguous  a duplicate or a hole means an entry was lost,
                                 and §6 of that file forbids deleting one
  every entry carries a status   one of Open / Settled / Gap, optionally
                                 qualified — an unlisted status makes the legend
                                 stop meaning anything
  every `OQ-NNN` reference       a citation to an id nobody defined is a dangling
  resolves                       link that happens not to be spelled as a link

That last check exists because the count was hand-maintained and drifted twice —
39, then 48, against an actual 51 — for the same reason six entries live as table
rows rather than headings and were missed. The number is now derived, and the gate
prints it.

Standard library only: no pip, no venv.
"""

from __future__ import annotations

import argparse
import re
import sys
import unicodedata
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
#  The §27 table, transcribed. `required` mirrors the tier marker in the spec:
#  a [v1.x] document is not a 1.0.0 deliverable and its absence is reported as a
#  note, not an error.
# ---------------------------------------------------------------------------
REQUIRED_DOCS: list[tuple[str, bool, str]] = [
    ("README.md", True, "what it is, install, build, licence, contributing"),
    ("docs/ARCHITECTURE.md", True, "five layers, threads, data flows, REQ-GEN-040 rationale"),
    ("docs/AUDIO-ENGINE.md", True, "signal chain, RT rules, gapless formulas, bit-perfect"),
    ("docs/API.md", True, "public module APIs with thread- and RT-safety per function"),
    ("docs/SKIN-AUTHORING.md", True, "two-tier model and its security reasoning"),
    ("docs/PLUGIN-AUTHORING.md", False, "[v1.x] plugin ABI and the seven categories"),
    ("docs/BUILDING.md", True, "clean-machine steps, the Qt-vs-vcpkg split and why"),
    ("docs/TESTING.md", True, "suites, reference hardware, golden corpus, checklists"),
    ("docs/THIRD-PARTY.md", True, "generated: the §4.2 register with SPDX ids and sources"),
    ("docs/LGPL-SOURCE-OFFER.md", True, "per-tag source links for every LGPL component"),
    ("docs/PRIVACY.md", True, "what is stored, what leaves, and how to verify it"),
    ("docs/PARITY.md", True, "generated from §29.2 — the honest parity matrix"),
    ("docs/ROADMAP.md", True, "[v1.x] and [v2] items plus the §2.4 non-goals"),
    ("docs/OPEN-QUESTIONS.md", True, "every assumption the implementation had to make"),
    ("CONTRIBUTING.md", True, "setup, style, commits, review, Definition of Done"),
    ("SECURITY.md", True, "disclosure process, supported versions, response times"),
    ("CODE_OF_CONDUCT.md", True, "Contributor Covenant or equivalent, named contact"),
]

# §27: "One file per decision, minimum: project licence, audio output,
# no-code-in-skins, C ABI for plugins, Qt acquisition, sync wire format."
REQUIRED_ADRS = [
    "0001-project-license.md",
    "0002-audio-output.md",
    "0003-no-code-in-skins.md",
    "0004-plugin-c-abi.md",
    "0005-qt-acquisition.md",
    "0008-sync-wire-format.md",
]

# Documents excluded from link scanning, and why. Both are excluded for the same
# reason the Markdown gate excludes them (.markdownlint-cli2.jsonc): they are not
# authored here.
SCAN_EXCLUDE = {
    "eclipse-player.md",       # upstream specification, consumed not authored
}
SCAN_EXCLUDE_PREFIXES = ("build/", "node_modules/", "desktop/third_party/")

# ---------------------------------------------------------------------------
#  Markdown link extraction
# ---------------------------------------------------------------------------
FENCE = re.compile(r"^\s*(`{3,}|~{3,})")
# Inline links and images: ](target) — the text half may contain nested brackets,
# so anchor on the closing ]( instead of trying to match the label.
INLINE_LINK = re.compile(r"\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_USE = re.compile(r"\]\[([^\]]+)\]")
REF_DEF = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*<?(\S+)>?")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
# An explicit anchor a heading may carry: <a id="..."></a> or {#...}
EXPLICIT_ANCHOR = re.compile(r'<a\s+(?:id|name)="([^"]+)"|\{#([^}]+)\}')

SKIP_SCHEMES = ("http://", "https://", "mailto:", "ftp://", "#!", "tel:")

# ---------------------------------------------------------------------------
#  The OQ register
# ---------------------------------------------------------------------------
OQ_REGISTER = "docs/OPEN-QUESTIONS.md"

# An entry is defined either as its own `### OQ-NNN — …` section or as a row in
# one of the summary tables. Both forms are real: §3's settled EFS ambiguities are
# one-line decisions that would be padding as sections, and treating only headings
# as definitions is exactly the mistake that produced a wrong count.
OQ_HEADING = re.compile(r'^###\s+OQ-(\d{3})\b(.*)$')
OQ_ROW = re.compile(r'^\|\s*\*\*OQ-(\d{3})\*\*\s*\|')

# A status marker closes the heading: `· **Settled**`. The status may be
# qualified — `**Settled, narrowly**` says something a bare marker cannot — so the
# check is on the first word.
OQ_STATUS = re.compile(r'·\s*\*\*([A-Za-z]+)[^*]*\*\*\s*$')
LEGAL_STATUSES = ("Open", "Settled", "Gap")

# Any citation of an id, anywhere. Matched in text files only; a repository-wide
# grep is the point, since the ids are cited from code comments and workflow
# comments as well as prose.
OQ_REFERENCE = re.compile(r'\bOQ-(\d{3})\b')
REFERENCE_SUFFIXES = (".md", ".py", ".yml", ".yaml", ".json", ".jsonc", ".js",
                      ".txt", ".hpp", ".cpp", ".h", ".c", ".cmake", ".ebnf",
                      ".ts", ".qrc")
REFERENCE_EXCLUDE_PREFIXES = ("build/", "node_modules/", "desktop/third_party/",
                              ".git/")
# The specification is consumed, not authored here, and cites no OQ ids anyway.
REFERENCE_EXCLUDE = {"eclipse-player.md"}


def oq_definitions(text: str) -> tuple[dict[int, list[int]], dict[int, str]]:
    """Return {id: [line numbers where defined]} and {id: status or ''}.

    Two return values rather than one because a duplicate is reported by line and
    a bad status is reported by id; collapsing them would lose one or the other.
    """
    defined: dict[int, list[int]] = {}
    status: dict[int, str] = {}
    in_fence = False
    for lineno, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        heading = OQ_HEADING.match(line)
        if heading:
            oq = int(heading.group(1))
            defined.setdefault(oq, []).append(lineno)
            marker = OQ_STATUS.search(heading.group(2))
            status[oq] = marker.group(1) if marker else ""
            continue
        row = OQ_ROW.match(line)
        if row:
            oq = int(row.group(1))
            defined.setdefault(oq, []).append(lineno)
            # A table row has no room for a status marker, and the tables it
            # appears in are titled by status. Not checked, deliberately.
            status.setdefault(oq, LEGAL_STATUSES[1])
    return defined, status


def register_problems(text: str) -> tuple[list[str], int]:
    """Problems internal to the register text, and the number of entries.

    Separate from the filesystem so the self-test can plant defects in a string.
    A check whose logic is only reachable through a real file cannot be shown to
    fail, and a gate nobody has watched fail is OQ-045 again.
    """
    errors: list[str] = []
    defined, status = oq_definitions(text)

    if not defined:
        return [f"{OQ_REGISTER}: no OQ entries found — either the register is "
                f"empty or this parser has stopped matching it"], 0

    for oq, lines in sorted(defined.items()):
        if len(lines) > 1:
            errors.append(
                f"{OQ_REGISTER}: OQ-{oq:03d} defined {len(lines)} times "
                f"(lines {', '.join(str(n) for n in lines)}) — two entries "
                f"sharing an id means one of them cannot be cited")

    holes = sorted(set(range(1, max(defined) + 1)) - set(defined))
    if holes:
        errors.append(
            f"{OQ_REGISTER}: no entry for "
            f"{', '.join(f'OQ-{n:03d}' for n in holes)} — ids run to "
            f"OQ-{max(defined):03d}, and §6 of that file forbids deleting an "
            f"entry: move it to Closed instead")

    for oq, value in sorted(status.items()):
        if not value:
            errors.append(
                f"{OQ_REGISTER}: OQ-{oq:03d} has no status marker — a heading "
                f"must end with `· **Open**`, `· **Settled**` or `· **Gap**`")
        elif value not in LEGAL_STATUSES:
            errors.append(
                f"{OQ_REGISTER}: OQ-{oq:03d} has status **{value}**, which is "
                f"not in the legend ({', '.join(LEGAL_STATUSES)}). A status "
                f"nobody listed makes the other {len(status) - 1} unreadable")

    return errors, len(defined)


def check_oq_register(repo: Path) -> tuple[list[str], int]:
    """Errors, and the number of entries defined."""
    register = repo / OQ_REGISTER
    if not register.exists():
        return [f"{OQ_REGISTER}: MISSING — §0.1 rule 1 requires it"], 0

    errors, count = register_problems(
        register.read_text(encoding="utf-8", errors="replace"))
    if not count:
        return errors, 0
    defined, _ = oq_definitions(
        register.read_text(encoding="utf-8", errors="replace"))

    # Every citation, repository-wide, resolves.
    for path in sorted(repo.rglob("*")):
        if not path.is_file() or path.suffix not in REFERENCE_SUFFIXES:
            continue
        rel = path.relative_to(repo).as_posix()
        if rel in REFERENCE_EXCLUDE or rel.startswith(REFERENCE_EXCLUDE_PREFIXES):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for match in OQ_REFERENCE.finditer(line):
                oq = int(match.group(1))
                if oq not in defined:
                    errors.append(
                        f"{rel}:{lineno}: cites OQ-{oq:03d}, which "
                        f"{OQ_REGISTER} does not define")

    return errors, count


def strip_inline_code(text: str) -> str:
    """Remove `code spans` so a path inside backticks is not read as a link."""
    return re.sub(r"`[^`]*`", "", text)


def slug(heading: str) -> str:
    """GitHub's heading-to-anchor algorithm.

    Strip formatting, lowercase, drop everything that is not a letter, digit,
    space, hyphen or underscore, then replace spaces with hyphens. Notably it
    keeps non-ASCII letters (so `§` is dropped but `é` survives), which is why
    this uses a unicode category test rather than an ASCII allowlist.
    """
    text = re.sub(r"<[^>]+>", "", heading)                      # inline HTML
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)      # links/images
    text = re.sub(r"\[([^\]]*)\]\[[^\]]*\]", r"\1", text)       # reference links
    text = text.replace("`", "")
    text = re.sub(r"[*~]+", "", text)                           # emphasis
    # Underscore emphasis is not intra-word in GFM, so `gapless_info` keeps its
    # underscore while `_stressed_` loses both. Stripping every underscore would
    # invent an anchor GitHub never generates and fail a link that works.
    text = re.sub(r"(?<!\w)_+|_+(?!\w)", "", text)
    out = []
    for ch in text.strip().lower():
        if ch in " \t":
            out.append("-")
        elif ch in "-_":
            out.append(ch)
        elif unicodedata.category(ch)[0] in ("L", "N"):
            out.append(ch)
    return "".join(out)


def headings_of(path: Path) -> set[str]:
    """Every anchor the file offers: heading slugs plus explicit HTML anchors.

    Duplicate headings get GitHub's -1, -2 … suffixes, so those are added too.
    """
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    in_fence: str | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fence = FENCE.match(raw)
        if fence:
            marker = fence.group(1)
            if in_fence is None:
                in_fence = marker[0]
                continue
            if marker[0] == in_fence:
                in_fence = None
            continue
        if in_fence is not None:
            continue
        for m in EXPLICIT_ANCHOR.finditer(raw):
            anchors.add(m.group(1) or m.group(2))
        h = HEADING.match(raw)
        if not h:
            continue
        base = slug(h.group(2))
        if not base:
            continue
        n = counts.get(base, 0)
        counts[base] = n + 1
        anchors.add(base if n == 0 else f"{base}-{n}")
        anchors.add(base)
    return anchors


def links_of(path: Path) -> list[tuple[int, str]]:
    """(line number, target) for every internal link, images included."""
    found: list[tuple[int, str]] = []
    definitions: dict[str, tuple[int, str]] = {}
    used_refs: list[tuple[int, str]] = []
    in_fence: str | None = None

    for lineno, raw in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
    ):
        fence = FENCE.match(raw)
        if fence:
            marker = fence.group(1)
            if in_fence is None:
                in_fence = marker[0]
                continue
            if marker[0] == in_fence:
                in_fence = None
            continue
        if in_fence is not None:
            continue

        line = strip_inline_code(raw)

        d = REF_DEF.match(line)
        if d:
            definitions[d.group(1).lower()] = (lineno, d.group(2))
            continue

        for m in INLINE_LINK.finditer(line):
            found.append((lineno, m.group(1)))
        for m in REF_USE.finditer(line):
            used_refs.append((lineno, m.group(1).lower()))

    for lineno, label in used_refs:
        if label in definitions:
            found.append((lineno, definitions[label][1]))
        else:
            found.append((lineno, f"\0undefined-reference:{label}"))
    found.extend(definitions.values())
    return found


def scannable() -> list[Path]:
    docs = []
    for path in sorted(REPO.rglob("*.md")):
        rel = path.relative_to(REPO).as_posix()
        if rel in SCAN_EXCLUDE or rel.startswith(SCAN_EXCLUDE_PREFIXES):
            continue
        docs.append(path)
    return docs


# Headings whose slug is easy to get wrong, with the anchor GitHub actually
# generates for each. The underscore cases are here because an earlier version
# stripped every underscore as emphasis and so failed a working link into
# `docs/API.md`; a fix without a case that pins it is a fix that comes back.
SLUG_CASES = (
    ("`audio/decode/gapless_info.hpp` — trim metadata",
     "audiodecodegapless_infohpp--trim-metadata"),
    ("OQ-021 — Dependency detection needs `PKG_CONFIG_PATH` for a user-local "
     "prefix · **Settled**",
     "oq-021--dependency-detection-needs-pkg_config_path-for-a-user-local-"
     "prefix--settled"),
    ("snake_case_and_more", "snake_case_and_more"),
    ("_stressed_ heading", "stressed-heading"),
    ("*emphasis* and **strong**", "emphasis-and-strong"),
    ("5 · Verification status — what is proven where",
     "5--verification-status--what-is-proven-where"),
    ("`core/text.hpp` — UTF-8, sort keys, path safety",
     "coretexthpp--utf-8-sort-keys-path-safety"),
    ("Qt 6 · LGPL-3.0-only", "qt-6--lgpl-30-only"),
    ("[A linked heading](x.md)", "a-linked-heading"),
)


# A minimal register that must pass: three heading entries covering all three
# statuses, one qualified status, and two table rows — the shape §3 actually uses.
#
# The ids are deliberately low. This file is a `.py` and is therefore scanned for
# citations like every other file, so any id appearing below must be one the real
# register defines. OQ-001..OQ-007 always will be; a high unused number would make
# the gate fail on its own fixture.
REGISTER_CONTROL = """\
### OQ-001 — Something undecided · **Open**

Body.

### OQ-002 — Something decided · **Settled, narrowly**

Body.

### OQ-003 — Something missing · **Gap**

| Id | Question | Answer |
| --- | --- | --- |
| **OQ-004** | A row entry | Yes |
| **OQ-005** | Another row entry | Yes |

### OQ-006 — Last one · **Settled**
"""

# Each defect is the control with one thing wrong, plus the text that must appear
# in the complaint. Written as edits to the control so a case cannot accidentally
# be testing something else as well.
REGISTER_DEFECTS = (
    ("a duplicated id",
     ("### OQ-006 — Last one · **Settled**",
      "### OQ-006 — Last one · **Settled**\n\n### OQ-006 — And again · **Open**"),
     "defined 2 times"),
    ("a hole in the sequence",
     ("| **OQ-004** | A row entry | Yes |\n", ""),
     "no entry for OQ-004"),
    ("a status that is not in the legend",
     ("· **Settled, narrowly**", "· **Measured**"),
     "not in the legend"),
    ("a heading with no status marker at all",
     ("### OQ-001 — Something undecided · **Open**",
      "### OQ-001 — Something undecided"),
     "has no status marker"),
    ("a status marker that is not at the end of the heading",
     ("### OQ-003 — Something missing · **Gap**",
      "### OQ-003 — · **Gap** something missing"),
     "has no status marker"),
    ("an id inside a fenced block counted as a definition",
     ("### OQ-006 — Last one · **Settled**",
      "```\n### OQ-007 — Not a real entry · **Open**\n```\n\n"
      "### OQ-006 — Last one · **Settled**"),
     None),   # must be accepted: the fence means it defines nothing
)


def register_self_test() -> list[str]:
    """Failures, empty when the register checks behave."""
    failures: list[str] = []

    errors, count = register_problems(REGISTER_CONTROL)
    if errors:
        failures.append(
            "the valid control register was rejected: " + "; ".join(errors))
    if count != 6:
        failures.append(f"the control has 6 entries, the parser found {count}")

    caught = 0
    for name, (find, replace), want in REGISTER_DEFECTS:
        if find not in REGISTER_CONTROL:
            failures.append(f"{name}: the case no longer matches the control")
            continue
        errors, _ = register_problems(REGISTER_CONTROL.replace(find, replace, 1))
        if want is None:
            if errors:
                failures.append(
                    f"{name}: must be accepted, but was rejected — "
                    + "; ".join(errors))
            else:
                caught += 1
            continue
        if not errors:
            failures.append(f"{name}: planted defect was NOT caught")
        elif not any(want in err for err in errors):
            failures.append(
                f"{name}: caught, but for the wrong reason — wanted {want!r}, "
                f"got {errors!r}")
        else:
            caught += 1

    if not failures:
        print(f"OQ register: {caught} of {len(REGISTER_DEFECTS)} planted case(s) "
              f"behaved, {count} valid entries accepted")
        print("  · duplicate id, sequence hole, off-legend status, missing and "
              "misplaced markers all rejected; a fenced example is not a "
              "definition")
    return failures


def self_test() -> int:
    failures = [
        f"{heading!r}\n      slug -> {slug(heading)}\n      want -> {expected}"
        for heading, expected in SLUG_CASES
        if slug(heading) != expected
    ]
    if failures:
        print(f"{len(failures)} slug failure(s):\n", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"heading slugs: {len(SLUG_CASES)} case(s) match GitHub's algorithm")

    failures = register_self_test()
    if failures:
        print(f"\n{len(failures)} register-check failure(s):\n", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the slug algorithm and the register checks against their "
             "committed corpora, then exit",
    )
    if parser.parse_args().self_test:
        return self_test()

    errors: list[str] = []
    notes: list[str] = []

    # ---------------------------------------------------- existence (§27 table)
    for name, required, content in REQUIRED_DOCS:
        if (REPO / name).exists():
            continue
        if required:
            errors.append(f"{name}: MISSING — §27 requires it ({content})")
        else:
            notes.append(f"{name}: absent — {content}")

    adr_dir = REPO / "docs" / "adr"
    for name in REQUIRED_ADRS:
        if not (adr_dir / name).exists():
            errors.append(f"docs/adr/{name}: MISSING — §27 names this decision explicitly")

    # ------------------------------------------------- the register (§0.1 r.1)
    register_errors, oq_count = check_oq_register(REPO)
    errors.extend(register_errors)

    # ------------------------------------------------------------- link checks
    anchor_cache: dict[Path, set[str]] = {}
    checked = 0
    scanned = 0

    for doc in scannable():
        scanned += 1
        rel = doc.relative_to(REPO).as_posix()
        for lineno, target in links_of(doc):
            if target.startswith("\0undefined-reference:"):
                label = target.split(":", 1)[1]
                errors.append(f"{rel}:{lineno}: reference link [{label}] has no definition")
                continue
            if target.startswith(SKIP_SCHEMES) or target.startswith("//"):
                continue
            checked += 1

            path_part, _, fragment = target.partition("#")

            if not path_part:                      # same-file #anchor
                dest = doc
            else:
                candidate = (doc.parent / path_part).resolve()
                try:
                    candidate.relative_to(REPO)
                except ValueError:
                    errors.append(
                        f"{rel}:{lineno}: link escapes the repository: {target}"
                    )
                    continue
                if not candidate.exists():
                    errors.append(f"{rel}:{lineno}: broken link — {target} does not exist")
                    continue
                dest = candidate

            if not fragment or dest.suffix != ".md":
                continue
            if dest not in anchor_cache:
                anchor_cache[dest] = headings_of(dest)
            if fragment.lower() not in {a.lower() for a in anchor_cache[dest]}:
                where = "this file" if dest == doc else dest.relative_to(REPO).as_posix()
                errors.append(
                    f"{rel}:{lineno}: fragment #{fragment} not found in {where}"
                )

    # --------------------------------------------------------------- reporting
    if notes:
        print("notes (not failures):")
        for note in notes:
            print(f"  · {note}")
        print()

    if errors:
        print(f"{len(errors)} documentation problem(s):\n", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        print(
            "\nREQ-GEN-075: every §27 document must exist and internal links must "
            "resolve.\nIf a document is deliberately deferred, it belongs in "
            "docs/OPEN-QUESTIONS.md\nand in the REQUIRED_DOCS table here with "
            "required=False — never silently absent.\n"
            "\n§0.1 rule 1 and §6 of docs/OPEN-QUESTIONS.md: an entry is added, "
            "then marked in place\nwhen it closes. It is never renumbered and "
            "never deleted, so the ids stay contiguous\nand a citation written "
            "today still resolves next year.",
            file=sys.stderr,
        )
        return 1

    print(
        f"doc links: {scanned} document(s), {checked} internal link(s) resolved, "
        f"{len(REQUIRED_DOCS) + len(REQUIRED_ADRS)} §27 deliverable(s) present"
    )
    print(
        f"OQ register: {oq_count} entries, ids contiguous 1..{oq_count}, every "
        f"status in the legend, every OQ-NNN citation in the tree resolves"
    )
    print("  NOT checked here: external http(s) URLs (a gate must pass offline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

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

Standard library only: no pip, no venv.
"""

from __future__ import annotations

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
    text = re.sub(r"[*_~]+", "", text)                          # emphasis
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


def main() -> int:
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
            "required=False — never silently absent.",
            file=sys.stderr,
        )
        return 1

    print(
        f"doc links: {scanned} document(s), {checked} internal link(s) resolved, "
        f"{len(REQUIRED_DOCS) + len(REQUIRED_ADRS)} §27 deliverable(s) present"
    )
    print("  NOT checked here: external http(s) URLs (a gate must pass offline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

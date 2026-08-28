#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Changelog generator — spec §25.5 step 7 (REQ-BLD-025, REQ-BLD-036).

`REQ-BLD-036` splits the changelog in two: the detail sections are generated
from conventional commits, and the **Highlights** section is written by a human
before every release. This script generates the first half and refuses to
fabricate the second. `CHANGELOG.md` states the reason in its own header — "a
changelog that is only a commit dump is not a changelog" — so a tool that
emitted a Highlights section from commit subjects would be defeating the
requirement it appears to implement.

Three decisions, each of which could have been made the dishonest way:

1. **A commit whose header does not parse is a finding, not a silent omission.**
   A generator that drops what it cannot read produces a changelog that looks
   complete and is not, which is the failure OQ-042 records at gate level. This
   repository has such a commit — `Initial commit`, which predates the
   convention — and it is reported every run rather than quietly skipped.

2. **Types that are not user-visible are excluded, and the exclusion is
   counted.** `refactor`, `docs`, `test`, `build`, `ci` and `chore` do not
   belong in a changelog aimed at users, but "36 commits, 6 shown" is a fact the
   reader needs in order to know the other 30 were a decision. The counts are
   printed with the types that produced them.

3. **The section mapping uses the scope enum, not just the type.** Any commit
   whose scope is `sec` lands under **Security** whatever its type, because
   `fix(sec)` is a security fix and Keep a Changelog has a section for exactly
   that. Nothing else in the type list maps there, so without the scope rule the
   Security section could never be populated by this generator at all.

`Deprecated` is the one Keep a Changelog section no commit *type* can reach, and
`REQ-BLD-034` requires deprecations to appear at the time of deprecation naming the
version of removal. A `DEPRECATED:` footer — the same shape as the standard
`BREAKING CHANGE:` footer, and equally invisible to `commitlint`, which permits
arbitrary footers — routes such a commit to that section whatever its type. A
deprecation that is not declared in a footer cannot be generated, and the output
says so rather than leaving the section silently empty.

Breaking changes — a `!` before the colon, or a `BREAKING CHANGE:` footer — are
collected separately and printed first, because `CHANGELOG.md` reserves MAJOR
for four specific surfaces (the plugin ABI, the theme/layout schema, the sync
protocol, the settings-export format) and only a human can say whether a given
breaking change touches one of them. The generator states the rule and hands
over; it does not decide the version.

Output is wrapped at 82 columns to match `CHANGELOG.md`'s own convention, which
`markdownlint` does not enforce there because the file is excluded from it. A
generator that emitted lines the surrounding file would not have is a generator
whose output has to be reflowed by hand every release.

`--self-test` runs the real parser and the real renderer over planted commit
records: an unparseable header, an unknown type, an out-of-enum scope, both
breaking-change spellings, a `feat` with no REQ id, a scope-`sec` commit of a
non-`fix` type, and a control set that renders cleanly. It also checks that the
type and scope tables here still match `commitlint.config.js`, because two lists
of the same thing in two files drift, and the drift would be invisible.

Usage:
    python3 tools/gen-changelog.py                       # HEAD since last tag
    python3 tools/gen-changelog.py --from v0.1.0 --to v0.2.0
    python3 tools/gen-changelog.py --self-test
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

WIDTH = 82

# --------------------------------------------------------------------------
#  The vocabulary. Both tables are asserted against commitlint.config.js by
#  --self-test, so a change there fails here rather than diverging silently.
# --------------------------------------------------------------------------

TYPES = [
    "feat", "fix", "perf", "refactor", "docs",
    "test", "build", "ci", "chore", "revert",
]

AREAS = [
    "gen", "aud", "lib", "pls", "efs", "net", "syn", "set",
    "tst", "thm", "uix", "key", "osi", "aut", "plg", "sec",
    "nfr", "bld", "spec", "deps", "deps-dev",
]

# Keep a Changelog 1.1.0 sections, in the order that file uses them.
SECTIONS = ["Added", "Changed", "Deprecated", "Removed", "Fixed", "Security"]

# Type → section, for the types a user-facing changelog reports.
TYPE_SECTION = {
    "feat": "Added",
    "fix": "Fixed",
    "perf": "Changed",
    "revert": "Removed",
}

# Types deliberately left out, with the reason reported alongside the count.
EXCLUDED = {
    "refactor": "internal structure, no behaviour change",
    "docs": "documentation, not shipped behaviour",
    "test": "test-only",
    "build": "build system",
    "ci": "CI configuration",
    "chore": "housekeeping",
}

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z-]+)\))?(?P<bang>!)?: (?P<subject>.+)$")
REQ_RE = re.compile(r"\bREQ-[A-Z]{3}-\d{3}\b")
BREAKING_RE = re.compile(r"^BREAKING[ -]CHANGE:", re.MULTILINE)
DEPRECATED_RE = re.compile(r"^DEPRECATED:", re.MULTILINE)

US, RS = "\x1f", "\x1e"


def wrap(text: str, indent: str = "") -> list[str]:
    """Wrap to WIDTH without ever breaking inside a token.

    `textwrap` splits on hyphens by default, which folded `REQ-NFR-004` into
    "`REQ-" / "NFR-004`" and broke the code span across two lines. Every
    identifier in this output is hyphenated — REQ ids, scopes, tag names — so
    hyphen splitting is not a corner case here, it is the common case.
    """
    return textwrap.wrap(text, width=WIDTH, subsequent_indent=indent,
                         break_on_hyphens=False, break_long_words=False)


@dataclass
class Commit:
    sha: str
    subject: str
    body: str
    type: str | None = None
    scope: str | None = None
    breaking: bool = False
    deprecates: bool = False
    reqs: list[str] = field(default_factory=list)

    @property
    def short(self) -> str:
        return self.sha[:7]


@dataclass
class Problem:
    sha: str
    what: str


def parse_commit(sha: str, subject: str, body: str) -> tuple[Commit, list[Problem]]:
    """Parse one commit. Problems are returned, never swallowed."""
    c = Commit(sha=sha, subject=subject, body=body)
    problems: list[Problem] = []

    m = HEADER_RE.match(subject)
    if not m:
        problems.append(Problem(sha, f"header is not a conventional commit: {subject!r}"))
        return c, problems

    c.type = m.group("type")
    c.scope = m.group("scope")
    c.subject = m.group("subject")
    c.breaking = bool(m.group("bang")) or bool(BREAKING_RE.search(body))
    c.deprecates = bool(DEPRECATED_RE.search(body))
    c.reqs = sorted(set(REQ_RE.findall(body)))

    if c.type not in TYPES:
        problems.append(Problem(sha, f"type {c.type!r} is not in commitlint's type-enum"))
    if c.scope is not None and c.scope not in AREAS:
        problems.append(Problem(sha, f"scope {c.scope!r} is not in commitlint's scope-enum"))
    if c.type in ("feat", "fix", "perf", "revert") and not c.reqs:
        problems.append(Problem(sha, f"{c.type} with no REQ id in the body (§1.3 rule 10)"))

    return c, problems


def section_for(c: Commit) -> str | None:
    """Which Keep a Changelog section this commit belongs in, or None."""
    if c.type is None or c.type not in TYPES:
        return None
    if c.deprecates:
        return "Deprecated"
    if c.scope == "sec":
        return "Security"
    return TYPE_SECTION.get(c.type)


def read_commits(frm: str | None, to: str) -> list[tuple[str, str, str]]:
    rng = f"{frm}..{to}" if frm else to
    out = subprocess.run(
        ["git", "log", "--reverse", f"--format=%H{US}%s{US}%b{RS}", rng],
        capture_output=True, text=True, check=True,
    ).stdout
    records = []
    for raw in out.split(RS):
        raw = raw.strip("\n")
        if not raw:
            continue
        parts = raw.split(US)
        if len(parts) < 3:
            parts += [""] * (3 - len(parts))
        records.append((parts[0], parts[1], parts[2]))
    return records


def previous_tag(to: str) -> str | None:
    r = subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0", f"{to}^"],
        capture_output=True, text=True,
    )
    return r.stdout.strip() or None


def bullet(c: Commit) -> str:
    text = c.subject
    if c.breaking:
        text = f"**BREAKING** — {text}"
    trail = f" (`{c.short}`"
    if c.reqs:
        trail += ", " + ", ".join(f"`{r}`" for r in c.reqs)
    trail += ")"
    return "\n".join(wrap(f"- {text}{trail}", indent="  "))


def render(commits: list[Commit], problems: list[Problem],
           frm: str | None, to: str) -> str:
    buckets: dict[str, list[Commit]] = {s: [] for s in SECTIONS}
    excluded: dict[str, int] = {}
    unmapped: list[Commit] = []

    for c in commits:
        sec = section_for(c)
        if sec:
            buckets[sec].append(c)
        elif c.type in EXCLUDED:
            excluded[c.type] = excluded.get(c.type, 0) + 1
        else:
            unmapped.append(c)

    lines: list[str] = []
    span = f"{frm}..{to}" if frm else f"the root commit to {to}"
    lines.append(f"<!-- generated by tools/gen-changelog.py over {span} -->")
    lines.append("")

    breaking = [c for c in commits if c.breaking]
    if breaking:
        lines.append("### Breaking changes")
        lines.append("")
        lines += wrap(
            "MAJOR is reserved for breaking changes to the plugin ABI, the "
            "theme/layout schema, the sync protocol, or the settings-export "
            "format. Whether the changes below cross one of those surfaces is a "
            "judgement this generator does not make.")
        lines.append("")
        for c in breaking:
            lines.append(bullet(c))
        lines.append("")

    shown = 0
    for sec in SECTIONS:
        if not buckets[sec]:
            continue
        lines.append(f"### {sec}")
        lines.append("")
        for c in buckets[sec]:
            lines.append(bullet(c))
            shown += 1
        lines.append("")

    if shown == 0:
        lines += wrap(
            "No user-visible change in this range. Every commit was one of the "
            "excluded types listed below; that is a real answer, not an empty "
            "template.")
        lines.append("")

    # The accounting. Without it, "6 shown" and "6 commits" look the same.
    total = len(commits)
    lines.append("### What this range contained")
    lines.append("")
    lines.append(f"- {total} commit(s); **{shown}** reported above.")
    if excluded:
        for t in sorted(excluded):
            lines.append(f"- {excluded[t]} `{t}` — excluded: {EXCLUDED[t]}.")
    if unmapped:
        lines.append(
            f"- {len(unmapped)} commit(s) that map to no section:")
        for c in unmapped:
            what = f"`{c.type}`" if c.type else "no conventional type"
            lines.append(f"  - `{c.short}` ({what}): {c.subject}")
    lines.append("")

    if problems:
        lines.append("### Problems the generator will not paper over")
        lines.append("")
        for p in problems:
            lines.append("\n".join(wrap(f"- `{p.sha[:7]}` — {p.what}", indent="  ")))
        lines.append("")

    lines += wrap(
        "The **Highlights** section is not generated. `REQ-BLD-036` requires a "
        "human to write it before every release, and a tool that produced it "
        "from commit subjects would defeat the requirement it appears to "
        "satisfy.")
    lines.append("")
    if not buckets["Deprecated"]:
        lines += wrap(
            "No **Deprecated** entries were generated. No commit *type* maps to "
            "that section; only a `DEPRECATED:` footer routes to it. If something "
            "was deprecated in this range without such a footer, `REQ-BLD-034` "
            "requires it here — with the version it will be removed in — and this "
            "generator cannot know that.")
        lines.append("")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
#  Self-test
# --------------------------------------------------------------------------

def _check_vocabulary_matches_commitlint() -> list[str]:
    """Two lists of the same thing in two files drift. Catch it here."""
    problems = []
    cfg = Path("commitlint.config.js")
    if not cfg.exists():
        return ["commitlint.config.js not found — cannot check vocabulary drift"]
    text = cfg.read_text(encoding="utf-8")

    m = re.search(r"'type-enum':\s*\[\s*2,\s*'always',\s*\[(.*?)\]", text, re.S)
    if not m:
        problems.append("could not find type-enum in commitlint.config.js")
    else:
        found = sorted(re.findall(r"'([a-z]+)'", m.group(1)))
        if found != sorted(TYPES):
            problems.append(f"type list drifted: here {sorted(TYPES)} vs config {found}")

    m = re.search(r"const AREAS = \[(.*?)\n\];", text, re.S)
    if not m:
        problems.append("could not find AREAS in commitlint.config.js")
    else:
        found = sorted(re.findall(r"'([a-z-]+)'", m.group(1)))
        if found != sorted(AREAS):
            problems.append(f"scope list drifted: here {sorted(AREAS)} vs config {found}")
    return problems


CASES = [
    # (sha, subject, body, expect_section, expect_problem_substring or None)
    ("a" * 40, "feat(aud): add a gapless scheduler", "REQ-AUD-035 done", "Added", None),
    ("b" * 40, "fix(gen): correct a path", "REQ-GEN-051", "Fixed", None),
    ("c" * 40, "perf(aud): halve the copy", "REQ-AUD-011", "Changed", None),
    ("d" * 40, "revert(pls): back out the queue rewrite", "REQ-PLS-001", "Removed", None),
    ("e" * 40, "feat(sec): sandbox the plugin host", "REQ-SEC-020", "Security", None),
    ("f" * 40, "fix(sec): reject a zip-slip path", "REQ-SEC-006", "Security", None),
    ("0" * 40, "docs(gen): explain the layers", "", None, None),
    ("1" * 40, "ci(bld): pin the actions", "", None, None),
    ("2" * 40, "Initial commit", "", None, "not a conventional commit"),
    ("3" * 40, "wibble(gen): do a thing", "REQ-GEN-001", None, "not in commitlint's type-enum"),
    ("4" * 40, "feat(nope): do a thing", "REQ-GEN-001", "Added", "not in commitlint's scope-enum"),
    ("5" * 40, "feat(aud): add a thing", "no requirement here", "Added", "no REQ id in the body"),
    ("6" * 40, "feat(plg)!: change the ABI", "REQ-PLG-001", "Added", None),
    ("7" * 40, "feat(syn): change the protocol",
     "BREAKING CHANGE: v1 payloads are rejected\nREQ-SYN-010", "Added", None),
    ("8" * 40, "refactor(plg): retire the v1 entry point",
     "DEPRECATED: arrow_plugin_init_v1, removed in 2.0.0\nREQ-PLG-004",
     "Deprecated", None),
    # Long on purpose: the wrapping check is vacuous unless one bullet, with its
    # sha and its REQ ids, is longer than the column limit on its own.
    ("9" * 40,
     "feat(lib): scan a twenty-thousand-track library without blocking the "
     "user interface thread at any point",
     "REQ-LIB-001 REQ-LIB-002 REQ-LIB-003 REQ-NFR-004", "Added", None),
]


def self_test() -> int:
    failures: list[str] = []

    for problem in _check_vocabulary_matches_commitlint():
        failures.append(problem)

    commits: list[Commit] = []
    problems: list[Problem] = []
    for sha, subject, body, want_sec, want_problem in CASES:
        c, ps = parse_commit(sha, subject, body)
        commits.append(c)
        problems.extend(ps)

        got_sec = section_for(c)
        if got_sec != want_sec:
            failures.append(f"{sha[:7]} {subject!r}: section {got_sec!r}, expected {want_sec!r}")

        texts = " | ".join(p.what for p in ps)
        if want_problem and want_problem not in texts:
            failures.append(f"{sha[:7]} {subject!r}: expected a problem matching "
                            f"{want_problem!r}, got {texts or '(none)'}")
        if not want_problem and ps:
            failures.append(f"{sha[:7]} {subject!r}: unexpected problem(s): {texts}")

    # Both breaking spellings must be recognised, and only those two commits.
    bang = [c for c in commits if c.breaking]
    if sorted(c.short for c in bang) != sorted(["6" * 7, "7" * 7]):
        failures.append(f"breaking detection: got {[c.short for c in bang]}")

    out = render(commits, problems, "v0.1.0", "v0.2.0")

    # The renderer's own invariants.
    for want in ("### Breaking changes", "### Added", "### Fixed", "### Changed",
                 "### Deprecated", "### Removed", "### Security",
                 "### What this range contained",
                 "### Problems the generator will not paper over"):
        if want not in out:
            failures.append(f"rendered output is missing {want!r}")
    if "Highlights" not in out:
        failures.append("rendered output does not state that Highlights is not generated")
    if re.search(r"^### Highlights", out, re.MULTILINE):
        failures.append("rendered output INVENTED a Highlights section")
    if "**BREAKING** — change the ABI" not in out:
        failures.append("breaking bullet is not marked")
    over = [l for l in out.splitlines() if len(l) > WIDTH]
    if over:
        failures.append(f"{len(over)} rendered line(s) exceed {WIDTH} columns: {over[:2]}")
    # The limit must be doing work: at least one bullet has to have been folded,
    # or the check above would pass over input that never tested it.
    if not any(l.startswith("  ") and "`REQ-NFR-004`" in l for l in out.splitlines()):
        failures.append("no bullet was wrapped with its code spans intact — the "
                        "column check is vacuous, or wrapping split a span")
    split = [l for l in out.splitlines() if l.count("`") % 2]
    if split:
        failures.append(f"{len(split)} line(s) split a code span: {split[:2]}")
    if "6 `docs`" in out or "`docs` — excluded" not in out:
        if "1 `docs` — excluded" not in out:
            failures.append("excluded-type accounting is wrong or missing")
    if "16 commit(s); **12** reported above." not in out:
        failures.append("commit accounting line is wrong: "
                        + next((l for l in out.splitlines() if "reported above" in l), "(absent)"))

    # A range with nothing user-visible must say so rather than render empty.
    quiet, qp = zip(*[(parse_commit("9" * 40, "ci(bld): tweak", "")) for _ in range(1)])
    quiet_out = render(list(quiet), [], None, "HEAD")
    if "No user-visible change in this range." not in quiet_out:
        failures.append("an all-excluded range renders as an empty template")
    if "(no conventional type): Initial commit" not in out:
        failures.append("an unparseable commit renders its absent type as 'None'")
    if "1 `ci` — excluded" not in quiet_out:
        failures.append("an all-excluded range does not account for what it dropped")
    if "No **Deprecated** entries were generated." not in quiet_out:
        failures.append("a range with no deprecation does not say the section is unreachable")
    # And the caveat must NOT appear when a deprecation was in fact generated.
    if "No **Deprecated** entries were generated." in out:
        failures.append("the Deprecated caveat printed even though one was generated")

    if failures:
        print(f"gen-changelog self-test: {len(failures)} failure(s)\n")
        for f in failures:
            print(f"  ✗ {f}")
        return 1

    print(f"gen-changelog self-test: pass — {len(CASES)} planted commit(s) "
          f"({sum(1 for c in CASES if c[4])} of them defective), all six Keep a "
          f"Changelog sections populated including Deprecated via its footer, "
          f"both breaking spellings caught, the "
          f"quiet-range path rendered, wrapping held at {WIDTH} columns with no code span split, and the "
          f"type/scope tables still match commitlint.config.js")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--from", dest="frm", help="start ref (default: previous tag)")
    ap.add_argument("--to", default="HEAD", help="end ref (default: HEAD)")
    ap.add_argument("--output", help="write here instead of stdout")
    ap.add_argument("--self-test", action="store_true",
                    help="run the parser and renderer over planted commits")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    frm = args.frm
    if frm is None:
        frm = previous_tag(args.to)
        if frm is None:
            print("no previous tag; generating from the root commit", file=sys.stderr)

    records = read_commits(frm, args.to)
    if not records:
        print(f"FATAL: no commits in {frm or 'root'}..{args.to}", file=sys.stderr)
        return 1

    commits: list[Commit] = []
    problems: list[Problem] = []
    for sha, subject, body in records:
        c, ps = parse_commit(sha, subject, body)
        commits.append(c)
        problems.extend(ps)

    out = render(commits, problems, frm, args.to)
    if args.output:
        Path(args.output).write_text(out, encoding="utf-8")
        print(f"wrote {args.output} ({len(out.splitlines())} lines, "
              f"{len(commits)} commit(s), {len(problems)} problem(s))",
              file=sys.stderr)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

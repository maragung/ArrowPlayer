#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Action-pin gate — spec §25.2 (REQ-BLD-021, REQ-SEC-013).

Every third-party GitHub Action a workflow runs is code that executes with the
job's token and filesystem. `REQ-SEC-013` asks for a dependency pinning policy;
§25.2 pins the toolchain and the vcpkg baseline. Actions were the hole in that
policy: a `uses: actions/checkout@v4` reference resolves through a tag the
upstream owner can move at any time, so what runs is whatever `v4` points at on
the morning the job starts. That is the one dependency in this repository nobody
could audit, and it was found by a scanner rather than by review (OQ-050).

Two rules, both measured rather than assumed:

1. A remote action must be pinned to a full 40-character lowercase commit SHA.
   Tags — floating (`@v4`) or exact (`@v4.4.0`) — and branches are mutable and
   therefore not pins.

2. The line must carry a trailing comment holding nothing but the version that
   SHA is, e.g. `# v4.4.0`. This is not decoration. Syft reads that comment for
   the component version: with it, the SBOM records
   `pkg:github/actions/checkout@v4.4.0`; without it, the version is the SHA
   itself, which no vulnerability database can order against a fixed-in
   constraint. Dropping the comment restores the original defect in a form that
   looks pinned. Anything beyond the version token goes on its own comment line
   above, because trailing prose would land inside the version syft reads.

Local actions (`./.github/actions/...`) need no pin — they are versioned by the
commit that contains them. Docker references must be digest-pinned
(`docker://image@sha256:...`) for the same reason a tag is not a pin.

This gate reads the workflow files as text on purpose. A YAML parse would be the
obvious approach and would silently defeat rule 2: comments are not part of the
parsed document, so the one thing that makes the pin auditable is invisible to
`yaml.safe_load`. Text scanning is the requirement, not a shortcut. It also keeps
the gate stdlib-only, like every other check in `tools/`.

`--self-test` runs the real line parser and the real rules over planted
references — floating tag, exact tag, branch, short SHA, uppercase SHA, missing
comment, prose-polluted comment, unpinned digest — and over references that must
be accepted. A green run over a tree where every `uses:` already happens to be
pinned would otherwise be equally consistent with a parser that never matches
anything (OQ-045).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORKFLOWS = REPO / ".github" / "workflows"
ACTIONS = REPO / ".github" / "actions"

# `uses:` as a YAML key, quoted or not, with whatever the value is and whatever
# trailing comment follows. Deliberately loose: this gate must see malformed
# references in order to reject them, so parsing cannot be the thing that
# decides a line is uninteresting.
USES = re.compile(
    r'^(?P<indent>\s*)(?:-\s+)?uses:\s*'
    r'(?P<quote>["\']?)(?P<ref>[^"\'#\s]+)(?P=quote)'
    r'(?P<rest>\s*(?:#.*)?)$'
)

SHA40 = re.compile(r'^[0-9a-f]{40}$')
SHA40_ANY_CASE = re.compile(r'^[0-9a-fA-F]{40}$')
DIGEST = re.compile(r'^sha256:[0-9a-f]{64}$')

# The whole trailing comment must be a version and nothing else — see rule 2.
VERSION_COMMENT = re.compile(
    r'^\s*#\s*v?\d+(?:\.\d+){1,2}(?:[-+][0-9A-Za-z.\-]+)?\s*$'
)


class Finding:
    def __init__(self, path: str, line: int, ref: str, problem: str, fix: str):
        self.path, self.line, self.ref = path, line, ref
        self.problem, self.fix = problem, fix

    def __str__(self) -> str:
        return (f"{self.path}:{self.line}: {self.problem}\n"
                f"    uses: {self.ref}\n"
                f"    fix:  {self.fix}")


def check_line(path: str, lineno: int, line: str) -> Finding | None:
    """Apply both rules to one source line. None means the line is fine or is
    not a `uses:` reference at all."""
    m = USES.match(line.rstrip("\n"))
    if not m:
        return None

    ref, rest = m.group("ref"), m.group("rest")

    # Local actions and reusable local workflows: versioned by this commit.
    if ref.startswith("./") or ref.startswith(".\\"):
        return None

    if ref.startswith("docker://"):
        _, _, image = ref.partition("docker://")
        _, sep, digest = image.rpartition("@")
        if not sep or not DIGEST.match(digest):
            return Finding(path, lineno, ref,
                           "docker reference is not digest-pinned",
                           "docker://image@sha256:<64 hex> — a tag is not a pin")
        return None

    name, sep, version = ref.rpartition("@")
    if not sep:
        return Finding(path, lineno, ref, "action reference has no @ref at all",
                       "owner/repo@<40-hex SHA> # vX.Y.Z")

    if not SHA40.match(version):
        if SHA40_ANY_CASE.match(version):
            problem = "SHA pin is not lowercase"
            fix = f"{name}@{version.lower()} # vX.Y.Z"
        elif re.match(r'^[0-9a-fA-F]{7,39}$', version):
            problem = f"abbreviated SHA ({len(version)} chars) is not a pin"
            fix = f"{name}@<full 40-hex SHA> # v{version}"
        else:
            problem = (f"pinned to the mutable ref {version!r} — tags and "
                       f"branches can be moved by the upstream owner")
            fix = f"{name}@<40-hex SHA of {version}> # {version}"
        return Finding(path, lineno, ref, problem, fix)

    if not VERSION_COMMENT.match(rest):
        if rest.strip():
            problem = (f"trailing comment {rest.strip()!r} is not a bare "
                       f"version — syft reads it as the component version")
        else:
            problem = ("SHA pin carries no version comment, so the SBOM records "
                       "the SHA as the version and no CVE database can order it")
        return Finding(path, lineno, ref, problem,
                       f"{name}@{version} # vX.Y.Z  (notes go on their own line)")

    return None


def scan(path: Path) -> list[Finding]:
    rel = path.relative_to(REPO).as_posix()
    findings = []
    for lineno, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        finding = check_line(rel, lineno, line)
        if finding is not None:
            findings.append(finding)
    return findings


def workflow_files() -> list[Path]:
    files: list[Path] = []
    for root in (WORKFLOWS, ACTIONS):
        if not root.exists():
            continue
        for suffix in ("*.yml", "*.yaml"):
            files.extend(root.rglob(suffix))
    return sorted(set(files))


# --- self-test corpus ------------------------------------------------------
# Each entry is one line that MUST be rejected, with the substring the message
# has to contain. A gate that rejects for the wrong reason sends the next
# maintainer to the wrong fix.
MUST_FLAG = [
    ("      - uses: actions/checkout@v4", "mutable ref"),
    ("      - uses: actions/checkout@v4.4.0", "mutable ref"),
    ("      - uses: actions/checkout@main", "mutable ref"),
    ("      - uses: actions/checkout@11d5960", "abbreviated SHA"),
    ("      - uses: actions/checkout@11D5960A326750D5838078E36CF38B85AF677262",
     "not lowercase"),
    ("      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262",
     "no version comment"),
    ("      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 "
     "# v4.4.0 (see OQ-050)", "not a bare version"),
    ("      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 "
     "# pinned", "not a bare version"),
    ("      - uses: actions/checkout", "no @ref at all"),
    ("      - uses: docker://alpine:3.20", "not digest-pinned"),
    ("      - uses: docker://alpine@sha256:deadbeef", "not digest-pinned"),
    ("        uses: some/action@release", "mutable ref"),
]

# Each of these must be accepted. The local-action and docker-digest rows are
# here because a gate that rejects every non-SHA reference would pass the rows
# above while making composite actions unusable.
MUST_NOT_FLAG = [
    "      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0",
    '      - uses: "actions/checkout@11d5960a326750d5838078e36cf38b85af677262" # v4.4.0',
    "      - uses: ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v1.13.0",
    "      - uses: owner/repo/sub/dir@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v1.13",
    "      - uses: owner/repo@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v2.0.0-beta.1",
    "        uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4.6.2",
    "      - uses: ./.github/actions/setup-toolchain",
    "      - uses: docker://alpine@sha256:" + "a" * 64,
    # Not `uses:` lines at all — the parser must not invent findings.
    "      - name: uses: is not a key here",
    "        run: echo uses: actions/checkout@v4",
    "      # - uses: actions/checkout@v4   (commented out)",
    "      with:",
]


def self_test() -> int:
    failures: list[str] = []

    for line, expected in MUST_FLAG:
        finding = check_line("self-test", 1, line)
        if finding is None:
            failures.append(f"accepted a reference it must reject: {line.strip()}")
        elif expected not in finding.problem:
            failures.append(f"rejected {line.strip()!r} for the wrong reason: "
                            f"{finding.problem!r} lacks {expected!r}")

    for line in MUST_NOT_FLAG:
        finding = check_line("self-test", 1, line)
        if finding is not None:
            failures.append(f"rejected a valid line: {line.strip()} "
                            f"({finding.problem})")

    # The parser must actually be looking at the lines it accepts, not skipping
    # them. Six of the MUST_NOT_FLAG rows are real pinned references; if USES
    # stopped matching, every one would be "accepted" for the wrong reason.
    matched = sum(1 for line in MUST_NOT_FLAG if USES.match(line.rstrip()))
    if matched != 8:
        failures.append(f"line parser matched {matched} of the 8 reference rows "
                        f"in the accept corpus, so acceptance proves nothing")

    if failures:
        print(f"action-pin self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  · {f}", file=sys.stderr)
        return 1

    print(f"action-pin self-test: {len(MUST_FLAG)} planted defect(s) caught, "
          f"{len(MUST_NOT_FLAG)} valid line(s) accepted, "
          f"{matched} of them parsed as real references")
    print("  · mutable tags, branches, short and upper-case SHAs, a missing "
          "version comment and a prose-polluted one all rejected separately")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true",
                    help="check the parser and rules against planted defects "
                         "and exit")
    if ap.parse_args().self_test:
        return self_test()

    files = workflow_files()
    if not files:
        print("action pins: no workflow files found under .github/ — nothing to "
              "check, which is not the same as a pass", file=sys.stderr)
        return 1

    findings: list[Finding] = []
    references = 0
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        references += sum(1 for line in text.splitlines()
                          if USES.match(line.rstrip()))
        findings.extend(scan(path))

    if findings:
        print(f"{len(findings)} unpinned or unauditable action reference(s):\n",
              file=sys.stderr)
        for finding in findings:
            print(f"  {finding}\n", file=sys.stderr)
        print("  Resolve a tag to its commit with:", file=sys.stderr)
        print("    git ls-remote https://github.com/<owner>/<repo> "
              "refs/tags/<tag>^{}", file=sys.stderr)
        return 1

    print(f"action pins: {len(files)} workflow file(s), {references} action "
          f"reference(s), all SHA-pinned with an orderable version comment")
    return 0


if __name__ == "__main__":
    sys.exit(main())

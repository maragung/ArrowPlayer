#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""SQL safety gate — spec §21.5 (REQ-SEC-009, REQ-SEC-010).

REQ-SEC-009 forbids string interpolation into SQL anywhere in the codebase,
including the smart-playlist compiler. That matters more than it first appears:
a smart-playlist rule can arrive inside an imported settings bundle (§19.4), so
rule text is untrusted input, and a compiler that pastes literals into SQL would
be a straightforward injection vector.

This script flags SQL statements that are built by concatenation or by a
formatting call. It is intentionally conservative about what counts as SQL, and
it supports an explicit, reviewed escape hatch for the cases where a statement
genuinely must be assembled (for example a parameter list of `?` placeholders,
whose length depends on the query but whose content never does):

    // sql-safety: ok - <reason>

Placing that comment on, or directly above, the offending line suppresses it.
The reason is mandatory so the exemption is reviewable.

`--self-test` plants the injection sites this script exists to catch and requires
each to be caught, plus the safe constructs that must *not* be flagged. There is
no SQL anywhere in the tree yet, so without it a green run would say nothing
about whether the matching works — which is the criticism OQ-045 records.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SEARCH_ROOTS = [REPO / "desktop" / "src", REPO / "desktop" / "tests"]

SQL_KEYWORDS = (
    "SELECT", "INSERT", "UPDATE", "DELETE", "REPLACE", "WHERE", "VALUES",
    "FROM", "JOIN", "ORDER BY", "GROUP BY", "HAVING", "LIMIT", "PRAGMA",
    "CREATE TABLE", "DROP TABLE", "ALTER TABLE", "CREATE INDEX",
)

# A string literal containing an SQL keyword.
#
# Matching is case-SENSITIVE on uppercase keywords. This is a deliberate
# discriminator: SQL in this codebase is written in uppercase (see the schema in
# src/library/), whereas prose and error messages use lowercase. Without it,
# ordinary strings like `" limit=" + std::to_string(n)` match the LIMIT keyword
# and drown the real findings in noise. A lowercase SQL statement would evade
# this check, which is an accepted trade: the convention is enforced by review
# and by the fact that every statement lives in one directory.
SQL_LITERAL = re.compile(
    r'"[^"]*\b(?:' + "|".join(k.replace(" ", r"\s+") for k in SQL_KEYWORDS) + r')\b[^"]*"'
)

# Concatenation or formatting adjacent to that literal.
CONCAT_MARKERS = (
    re.compile(r'"\s*\+'),          # "SELECT ..." + x
    re.compile(r'\+\s*"'),          # x + " WHERE ..."
    re.compile(r'"\s*<<'),          # stream-building a query
    re.compile(r'\.append\s*\('),   # sql.append(untrusted)
    re.compile(r'std::format\s*\('),
    re.compile(r'fmt::format\s*\('),
    re.compile(r'sprintf|snprintf'),
)

EXEMPTION = re.compile(r'//\s*sql-safety:\s*ok\s*-\s*\S+')

SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".cxx"}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


def scan(path: Path) -> list[str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return []
    return scan_lines(rel(path), lines)


def scan_lines(name: str, lines: list[str]) -> list[str]:
    """The whole check, over text rather than a file.

    Split out so --self-test drives the real matcher on synthetic input instead of
    writing planted defects into the tree this gate scans.
    """
    findings: list[str] = []
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if not SQL_LITERAL.search(line):
            continue
        if not any(marker.search(line) for marker in CONCAT_MARKERS):
            continue

        # Exemption on this line or the line directly above.
        if EXEMPTION.search(line):
            continue
        if index > 0 and EXEMPTION.search(lines[index - 1]):
            continue

        findings.append(
            f"{name}:{index + 1}: SQL appears to be built by "
            f"concatenation or formatting\n"
            f"    {stripped[:120]}\n"
            f"    REQ-SEC-009: every SQL literal must be a bound parameter. "
            f"If this is genuinely safe, add:  // sql-safety: ok - <reason>"
        )
    return findings


# --------------------------------------------------------------------------- #
#  Self-test
# --------------------------------------------------------------------------- #

# Each of these is a way SQL has actually been built by hand in real codebases.
MUST_FLAG = [
    ('concatenated literal',
     ['std::string sql = "SELECT * FROM tracks WHERE id = " + id;']),
    ('appended fragment',
     ['sql += " ORDER BY " + column;']),
    ('literal on the right of a +',
     ['auto q = prefix + " WHERE artist = \'" + artist + "\'";']),
    ('stream-built query',
     ['oss << "SELECT path FROM tracks WHERE rating > " << rating;']),
    ('append() call',
     ['sql.append("SELECT * FROM albums WHERE year = ");', 'sql.append(year);']),
    ('std::format',
     ['auto q = std::format("DELETE FROM tracks WHERE id = {}", id);']),
    ('fmt::format',
     ['auto q = fmt::format("UPDATE tracks SET plays = {}", n);']),
    ('snprintf into a query buffer',
     ['snprintf(buf, sizeof buf, "INSERT INTO t VALUES (%s)", v);']),
    ('exemption without a reason is not an exemption',
     ['// sql-safety: ok -',
      'std::string sql = "SELECT * FROM tracks WHERE id = " + id;']),
    ('multi-word keyword with newline-collapsed spacing',
     ['sql = "SELECT a FROM t GROUP BY  b " + tail;']),
]

MUST_NOT_FLAG = [
    ('bound parameter',
     ['auto stmt = db.prepare("SELECT * FROM tracks WHERE id = ?");']),
    ('bound parameter with concatenation elsewhere on the line',
     ['db.prepare("SELECT * FROM tracks WHERE id = ?").bind(1, a + b);']),
    ('lowercase prose that contains an SQL word',
     ['msg = " limit=" + std::to_string(n);']),
    ('exemption on the same line',
     ['sql += placeholders;  // sql-safety: ok - only "?" characters, '
      'count from the caller, no values']),
    ('exemption on the line above',
     ['// sql-safety: ok - fixed table name from an enum, never user input',
      'sql = "SELECT * FROM " + table_for(kind);']),
    ('commented-out code',
     ['// sql = "SELECT * FROM tracks WHERE id = " + id;']),
    ('doc-comment body',
     [' * sql = "SELECT * FROM tracks WHERE id = " + id;']),
    ('a query with no concatenation at all',
     ['static constexpr char kSchema[] = "CREATE TABLE tracks (id INTEGER)";']),
]


def self_test() -> int:
    failures = []
    for label, lines in MUST_FLAG:
        if not scan_lines("synthetic", lines):
            failures.append(f"missed an injection site: {label}")
    for label, lines in MUST_NOT_FLAG:
        found = scan_lines("synthetic", lines)
        if found:
            failures.append(f"false positive on {label}: {found[0].splitlines()[0]}")

    # The known blind spot, asserted rather than left to be rediscovered: matching
    # is case-sensitive on uppercase keywords, so lowercase SQL evades this gate.
    # Pinned as a *documented* behaviour — if someone makes the matcher
    # case-insensitive, this fails and they must update the docstring's rationale
    # instead of finding out from the noise.
    lowercase = ['std::string sql = "select * from tracks where id = " + id;']
    if scan_lines("synthetic", lowercase):
        failures.append("case-insensitive now: the docstring's stated trade-off "
                        "is stale, and prose like \" limit=\" will start matching")

    if failures:
        print(f"sql-safety self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  · {f}", file=sys.stderr)
        return 1
    print(f"sql-safety self-test: {len(MUST_FLAG)} injection site(s) caught, "
          f"{len(MUST_NOT_FLAG)} safe construct(s) left alone, "
          f"1 documented blind spot still blind")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true",
                    help="check the matcher against planted defects and exit")
    if ap.parse_args().self_test:
        return self_test()

    findings: list[str] = []
    scanned = 0
    for root in SEARCH_ROOTS:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in SUFFIXES:
                scanned += 1
                findings.extend(scan(path))

    if findings:
        print(f"{len(findings)} possible SQL injection site(s):\n", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}\n", file=sys.stderr)
        return 1

    print(f"sql safety: {scanned} file(s) scanned, no interpolated SQL found")
    return 0


if __name__ == "__main__":
    sys.exit(main())

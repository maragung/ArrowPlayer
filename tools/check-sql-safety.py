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
"""

from __future__ import annotations

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
    findings: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return findings

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
            f"{rel(path)}:{index + 1}: SQL appears to be built by "
            f"concatenation or formatting\n"
            f"    {stripped[:120]}\n"
            f"    REQ-SEC-009: every SQL literal must be a bound parameter. "
            f"If this is genuinely safe, add:  // sql-safety: ok - <reason>"
        )
    return findings


def main() -> int:
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

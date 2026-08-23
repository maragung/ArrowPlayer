#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real-time safety gate — spec §8.2.3 (REQ-AUD-015, REQ-AUD-017).

The audio callback runs on a thread with a hard deadline. §8.2.3 lists what is
forbidden there — allocation, locks, file I/O, logging, exceptions — and
REQ-AUD-017 requires that every function callable from the callback carries a
`/// RT-SAFE:` doc-comment stating why it is safe.

This script enforces two things:

1. Any function whose doc-comment claims `RT-SAFE` must not contain an obviously
   forbidden construct. A claim that is false is worse than no claim at all,
   because the next maintainer will trust it.

2. Files under the real-time path (audio/graph, audio/dsp, audio/sink) are
   checked for forbidden constructs inside functions marked RT-SAFE.

This is deliberately a lint, not a proof. It catches the mistakes that actually
happen — someone adds a `std::vector::push_back` or a log line inside a
processing loop years later — and it makes the RT-SAFE annotation load-bearing
rather than decorative. TSan and the allocation-hook test in §23.5 cover what
static inspection cannot.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "desktop" / "src"

RT_PATHS = [
    SRC / "audio" / "graph",
    SRC / "audio" / "dsp",
    SRC / "audio" / "sink",
]

RT_MARKER = re.compile(r'///\s*RT-SAFE:')

# Constructs that must never appear in a function claiming RT safety.
FORBIDDEN = [
    (re.compile(r'\bnew\b(?!\s*\()'),                 "heap allocation (new)"),
    (re.compile(r'\bdelete\b'),                        "heap deallocation (delete)"),
    (re.compile(r'\bmalloc\s*\(|\bfree\s*\(|\bcalloc\s*\(|\brealloc\s*\('),
                                                       "C heap allocation"),
    (re.compile(r'\bstd::mutex\b|\bstd::lock_guard\b|\bstd::unique_lock\b|'
                r'\bstd::scoped_lock\b|\bstd::condition_variable\b'),
                                                       "blocking synchronisation"),
    (re.compile(r'\.lock\s*\(\s*\)'),                  "mutex lock"),
    (re.compile(r'\bthrow\b'),                         "exception throw"),
    (re.compile(r'\bstd::cout\b|\bstd::cerr\b|\bprintf\s*\(|\bfprintf\s*\('),
                                                       "console I/O"),
    (re.compile(r'\bstd::ofstream\b|\bstd::ifstream\b|\bfopen\s*\(|\bfwrite\s*\('),
                                                       "file I/O"),
    (re.compile(r'\blog_\w+\s*\(|\bLOG\w*\s*\('),      "logging"),
    (re.compile(r'\.push_back\s*\(|\.emplace_back\s*\(|\.resize\s*\(|'
                r'\.insert\s*\(|\.reserve\s*\('),      "container growth"),
    (re.compile(r'\bstd::string\s+\w+\s*[;=]'),        "std::string construction"),
    (re.compile(r'\bstd::to_string\s*\('),             "std::to_string (allocates)"),
    (re.compile(r'\bstd::regex\b'),                    "std::regex (unbounded time)"),
    (re.compile(r'\bstd::shared_ptr\b'),               "shared_ptr (atomic refcount)"),
    (re.compile(r'\bstd::sort\b'),                     "unbounded sort"),
    (re.compile(r'\bstd::this_thread::sleep'),         "sleep"),
]

SUFFIXES = {".cpp", ".hpp"}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


def function_bodies_marked_rt_safe(lines: list[str]):
    """Yields (start_line, end_line) spans for functions whose preceding
    doc-comment block contains an RT-SAFE marker.

    Brace matching is approximate on purpose: it is a lint, and a mis-detected
    span produces a reviewable false positive rather than a silent miss.
    """
    index = 0
    total = len(lines)
    while index < total:
        if not RT_MARKER.search(lines[index]):
            index += 1
            continue

        # Walk forward past the remaining doc-comment to the declaration.
        cursor = index + 1
        while cursor < total and lines[cursor].lstrip().startswith("///"):
            cursor += 1

        # Find the opening brace of the body, allowing a multi-line signature.
        depth = 0
        body_start = None
        scan = cursor
        while scan < total and scan < cursor + 12:
            if "{" in lines[scan]:
                body_start = scan
                depth = lines[scan].count("{") - lines[scan].count("}")
                break
            if ";" in lines[scan]:
                break  # a declaration only; nothing to check here
            scan += 1

        if body_start is None:
            index = cursor
            continue

        end = body_start
        while end + 1 < total and depth > 0:
            end += 1
            depth += lines[end].count("{") - lines[end].count("}")

        yield body_start, end
        index = end + 1


def scan(path: Path) -> list[str]:
    findings: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return findings

    for start, end in function_bodies_marked_rt_safe(lines):
        for lineno in range(start, min(end + 1, len(lines))):
            line = lines[lineno]
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            for pattern, label in FORBIDDEN:
                if pattern.search(line):
                    findings.append(
                        f"{rel(path)}:{lineno + 1}: {label} inside a function "
                        f"documented RT-SAFE\n"
                        f"    {stripped[:110]}\n"
                        f"    REQ-AUD-015: this is forbidden on the audio "
                        f"callback thread."
                    )
    return findings


def main() -> int:
    findings: list[str] = []
    annotated = 0
    files = 0

    for root in RT_PATHS:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not (path.is_file() and path.suffix in SUFFIXES):
                continue
            files += 1
            text = path.read_text(encoding="utf-8", errors="replace")
            annotated += len(RT_MARKER.findall(text))
            findings.extend(scan(path))

    if findings:
        print(f"{len(findings)} real-time safety violation(s):\n", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}\n", file=sys.stderr)
        return 1

    print(f"rt safety: {files} file(s), {annotated} RT-SAFE annotation(s), no violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())

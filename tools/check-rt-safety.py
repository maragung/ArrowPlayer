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

`--self-test` runs the real span finder and the real pattern list over synthetic
sources: an RT-SAFE function that allocates must be caught, and the same
allocation one line past that function's closing brace must not be. Without it, a
green run over a tree with three annotations would be equally consistent with a
span finder that never matches anything (OQ-045).
"""

from __future__ import annotations

import argparse
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
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return []
    return scan_lines(rel(path), lines)


def scan_lines(name: str, lines: list[str]) -> list[str]:
    """The whole check, over text rather than a file.

    Split out so --self-test exercises the real span finder on synthetic sources
    instead of committing files that violate the rule into the tree this gate scans.
    """
    findings: list[str] = []
    for start, end in function_bodies_marked_rt_safe(lines):
        for lineno in range(start, min(end + 1, len(lines))):
            line = lines[lineno]
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            for pattern, label in FORBIDDEN:
                if pattern.search(line):
                    findings.append(
                        f"{name}:{lineno + 1}: {label} inside a function "
                        f"documented RT-SAFE\n"
                        f"    {stripped[:110]}\n"
                        f"    REQ-AUD-015: this is forbidden on the audio "
                        f"callback thread."
                    )
    return findings


# --------------------------------------------------------------------------- #
#  Self-test
# --------------------------------------------------------------------------- #

def _src(text: str) -> list[str]:
    return text.strip("\n").splitlines()


# Every one of these is a false RT-SAFE claim, which §8.2.3 treats as worse than
# no claim: the next maintainer reads the annotation and trusts it.
MUST_FLAG = [
    ("heap allocation", """
/// RT-SAFE: no allocation, no locks.
void process(float* out, int n) {
    auto* scratch = new float[n];
    (void)scratch;
}
"""),
    ("container growth", """
/// RT-SAFE: fixed-capacity buffer.
void push(float sample) {
    samples_.push_back(sample);
}
"""),
    ("blocking synchronisation", """
/// RT-SAFE: wait-free.
void render() {
    std::lock_guard<std::mutex> guard(mutex_);
}
"""),
    ("exception throw", """
/// RT-SAFE: never throws.
void render(int n) {
    if (n < 0) throw std::invalid_argument("n");
}
"""),
    ("std::to_string", """
/// RT-SAFE: no allocation.
void report(int n) {
    last_ = std::to_string(n);
}
"""),
    ("logging", """
/// RT-SAFE: no I/O.
void render() {
    LOG_WARN("xrun");
}
"""),
    ("C allocation", """
/// RT-SAFE: preallocated.
void resize(int n) {
    buf_ = static_cast<float*>(malloc(sizeof(float) * n));
}
"""),
    ("sleep", """
/// RT-SAFE: no blocking.
void wait_for_slot() {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
}
"""),
    ("violation deep inside a nested body", """
/// RT-SAFE: bounded, branch-free in the common case.
void process(float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        if (out[i] > 1.0F) {
            out[i] = 1.0F;
        } else {
            history_.emplace_back(out[i]);
        }
    }
}
"""),
    ("multi-line signature", """
/// RT-SAFE: reads only atomics.
void process(const float* in,
             float* out,
             int frames)
{
    std::shared_ptr<Filter> f = filter_;
}
"""),
    ("marker with no space after the slashes", """
///RT-SAFE: no allocation.
void render() {
    delete buffer_;
}
"""),
]

MUST_NOT_FLAG = [
    ("the same allocation in a function with no RT-SAFE claim", """
/// Prepares the buffers. Called from the control thread.
void prepare(int n) {
    scratch_ = new float[n];
}
"""),
    ("an allocation one line past the RT-SAFE function's closing brace", """
/// RT-SAFE: operates in place on a fixed span.
void process(float* out, int frames) {
    for (int i = 0; i < frames; ++i) out[i] *= gain_;
}

void prepare(int n) {
    scratch_ = new float[n];
}
"""),
    ("a comment inside an RT-SAFE body naming a forbidden construct", """
/// RT-SAFE: no allocation.
void process(float* out, int frames) {
    // No new/delete here: the scratch space is owned by prepare().
    for (int i = 0; i < frames; ++i) out[i] *= gain_;
}
"""),
    ("an RT-SAFE declaration, with the offender following it", """
/// RT-SAFE: pure arithmetic.
float peak(const float* in, int n) noexcept;

void unrelated(int n) {
    heap_ = new float[n];
}
"""),
    ("a genuinely real-time-safe body", """
/// RT-SAFE: lock-free ring read, no allocation, no syscalls.
int read(float* out, int frames) noexcept {
    const int available = write_.load(std::memory_order_acquire) - read_pos_;
    const int take = available < frames ? available : frames;
    std::memcpy(out, &storage_[read_pos_ & mask_], sizeof(float) * take);
    read_pos_ += take;
    return take;
}
"""),
    ("placement new, which is not an allocation", """
/// RT-SAFE: constructs in preallocated storage.
void emplace(float v) {
    ::new (&storage_[head_]) Sample(v);
}
"""),
    ("a doc-comment continuation line mentioning throw", """
/// RT-SAFE: no allocation, no locks.
/// Callers must not throw across this boundary.
void process(float* out, int frames) {
    for (int i = 0; i < frames; ++i) out[i] = 0.0F;
}
"""),
]


def self_test() -> int:
    failures = []
    for label, text in MUST_FLAG:
        if not scan_lines("synthetic", _src(text)):
            failures.append(f"missed a false RT-SAFE claim: {label}")
    for label, text in MUST_NOT_FLAG:
        found = scan_lines("synthetic", _src(text))
        if found:
            failures.append(f"false positive on {label}: "
                            f"{found[0].splitlines()[0]}")

    # The span finder is the part most likely to silently stop working, and a
    # finder that matches nothing produces exactly the same output as a clean
    # tree. Assert it against a source with two annotated functions.
    two = _src("""
/// RT-SAFE: in place.
void a(float* p, int n) {
    for (int i = 0; i < n; ++i) p[i] = 0.0F;
}

void b() {}

/// RT-SAFE: in place.
void c(float* p, int n) {
    for (int i = 0; i < n; ++i) p[i] = 1.0F;
}
""")
    spans = list(function_bodies_marked_rt_safe(two))
    if len(spans) != 2:
        failures.append(f"span finder found {len(spans)} RT-SAFE bodies, expected 2")
    elif not (two[spans[0][1]].strip() == "}" and two[spans[1][1]].strip() == "}"):
        failures.append("span finder did not end both bodies on a closing brace")

    if failures:
        print(f"rt-safety self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  · {f}", file=sys.stderr)
        return 1
    print(f"rt-safety self-test: {len(MUST_FLAG)} false RT-SAFE claim(s) caught, "
          f"{len(MUST_NOT_FLAG)} legitimate construct(s) left alone, "
          f"span finder bounds both bodies of a two-function source")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true",
                    help="check the span finder and patterns against planted "
                         "defects and exit")
    if ap.parse_args().self_test:
        return self_test()

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

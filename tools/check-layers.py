#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Layer dependency enforcement — spec §7.2 (REQ-GEN-050, REQ-GEN-051).

The architecture in eclipse-player.md §7.1 defines five layers, and §7.2 makes
the dependency direction a hard rule rather than reviewer discipline. This script
is the mechanical enforcement the spec demands, wired into desktop-ci.yml.

Rules checked
-------------
1. The domain layer links against nothing but the C++ standard library.
   No Qt, FFmpeg, SQLite, TagLib, ALSA, WASAPI or platform headers may appear
   anywhere reachable from `eclipse-domain`.

2. Adapter headers are reachable only through their own directory.
   Nothing outside src/audio/sink/ may include a WASAPI or ALSA header;
   nothing outside src/library/ may include a TagLib header; and so on.

3. shared-spec/ contains no compiled code.

Exit status is non-zero on any violation, with the offending file, line and
reason printed so the failure is actionable without opening the script.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DESKTOP = REPO / "desktop"
SRC = DESKTOP / "src"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# --------------------------------------------------------------------------- #
#  Rule 1 — the domain layer is dependency-free
# --------------------------------------------------------------------------- #

# Every source file compiled into `eclipse-domain`. Kept explicit rather than
# globbed so that adding a file to the domain library is a deliberate act that
# shows up in review.
DOMAIN_DIRS = [
    SRC / "core",
    SRC / "audio" / "dsp",
    SRC / "audio" / "analysis",
    SRC / "audio" / "graph",
    SRC / "audio" / "decode",   # ports + pure parsers only; adapters excluded below
    SRC / "theme",
]

# Adapter translation units that live inside an otherwise-domain directory.
# These are compiled into `eclipse-adapters`, not `eclipse-domain`.
DOMAIN_EXCLUSIONS = {
    SRC / "audio" / "decode" / "ffmpeg_decoder.cpp",
}

FORBIDDEN_IN_DOMAIN = {
    # Qt
    "Qt": re.compile(r'^(Q[A-Z]|Qt[A-Z/]|qt/)'),
    # FFmpeg
    "FFmpeg": re.compile(r'^lib(avcodec|avformat|avutil|swresample|swscale)/'),
    # Databases
    "SQLite": re.compile(r'^sqlite3?\.h$'),
    # Tagging
    "TagLib": re.compile(r'^taglib/'),
    # Audio backends
    "ALSA": re.compile(r'^alsa/'),
    "PulseAudio": re.compile(r'^pulse/'),
    "PipeWire": re.compile(r'^pipewire/'),
    "JACK": re.compile(r'^jack/'),
    "WASAPI/COM": re.compile(r'^(audioclient|mmdeviceapi|windows|objbase|combaseapi|avrt)\.h$'),
    # Other third parties
    "libsamplerate": re.compile(r'^samplerate\.h$'),
    "SoundTouch": re.compile(r'^SoundTouch'),
    "Chromaprint": re.compile(r'^chromaprint\.h$'),
    "projectM": re.compile(r'^(libprojectM|projectM)'),
    # Platform
    "POSIX/Linux": re.compile(r'^(sys/|linux/|unistd\.h$|dlfcn\.h$)'),
}

# --------------------------------------------------------------------------- #
#  Rule 2 — adapter headers are confined to their own directory
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class Confinement:
    label: str
    pattern: re.Pattern
    allowed_dir: Path


CONFINEMENTS = [
    Confinement("WASAPI", re.compile(r'^(audioclient|mmdeviceapi|avrt)\.h$'),
                SRC / "audio" / "sink"),
    Confinement("ALSA", re.compile(r'^alsa/'), SRC / "audio" / "sink"),
    Confinement("PulseAudio", re.compile(r'^pulse/'), SRC / "audio" / "sink"),
    Confinement("PipeWire", re.compile(r'^pipewire/'), SRC / "audio" / "sink"),
    Confinement("JACK", re.compile(r'^jack/'), SRC / "audio" / "sink"),
    Confinement("FFmpeg", re.compile(r'^lib(avcodec|avformat|avutil|swresample)/'),
                SRC / "audio" / "decode"),
    Confinement("TagLib", re.compile(r'^taglib/'), SRC / "library"),
    Confinement("SQLite", re.compile(r'^sqlite3?\.h$'), SRC / "library"),
]

SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".hxx", ".ipp"}


def iter_sources(root: Path):
    if not root.exists():
        return
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def includes_of(path: Path):
    """Yields (line_number, included_path) for each #include in `path`."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:  # pragma: no cover - unreadable file is a real failure
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return
    for lineno, line in enumerate(text.splitlines(), start=1):
        match = INCLUDE_RE.match(line)
        if match:
            yield lineno, match.group(1)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


def check_domain_purity() -> list[str]:
    """Rule 1: nothing in the domain layer may reach a third party."""
    violations: list[str] = []
    for directory in DOMAIN_DIRS:
        for path in iter_sources(directory):
            if path in DOMAIN_EXCLUSIONS:
                continue
            for lineno, header in includes_of(path):
                for label, pattern in FORBIDDEN_IN_DOMAIN.items():
                    if pattern.match(header):
                        violations.append(
                            f"{rel(path)}:{lineno}: domain layer includes "
                            f"{label} header <{header}>\n"
                            f"    REQ-GEN-050(2): the domain layer must link "
                            f"against nothing but the standard library."
                        )
    return violations


def check_adapter_confinement() -> list[str]:
    """Rule 2: adapter headers stay inside their own adapter directory."""
    violations: list[str] = []
    for path in iter_sources(SRC):
        for lineno, header in includes_of(path):
            for rule in CONFINEMENTS:
                if not rule.pattern.match(header):
                    continue
                if rule.allowed_dir in path.parents:
                    continue
                violations.append(
                    f"{rel(path)}:{lineno}: <{header}> ({rule.label}) is only "
                    f"permitted under {rel(rule.allowed_dir)}/\n"
                    f"    REQ-GEN-050(3): adapters are reachable only through "
                    f"their layer-2 port."
                )
    return violations


def check_shared_spec_has_no_code() -> list[str]:
    """Rule 3: shared-spec/ is data and specification only."""
    violations: list[str] = []
    spec_dir = REPO / "shared-spec"
    if not spec_dir.exists():
        return violations
    code_suffixes = SOURCE_SUFFIXES | {".c", ".kt", ".java", ".rs", ".go"}
    for path in sorted(spec_dir.rglob("*")):
        if path.is_file() and path.suffix in code_suffixes:
            violations.append(
                f"{rel(path)}: shared-spec/ must contain no compiled code\n"
                f"    REQ-GEN-030 / §5: anything shared lives here as data or "
                f"specification files only."
            )
    return violations


def check_android_isolation() -> list[str]:
    """§5: android/ and desktop/ never import each other."""
    violations: list[str] = []
    for root, other in ((REPO / "desktop", "android"), (REPO / "android", "desktop")):
        if not root.exists():
            continue
        for path in iter_sources(root):
            for lineno, header in includes_of(path):
                if header.startswith(f"{other}/") or f"/{other}/" in header:
                    violations.append(
                        f"{rel(path)}:{lineno}: includes from {other}/\n"
                        f"    §5: the two platforms are fully separated."
                    )
    return violations


CHECKS = (
    ("domain layer purity", check_domain_purity),
    ("adapter confinement", check_adapter_confinement),
    ("shared-spec has no code", check_shared_spec_has_no_code),
    ("platform isolation", check_android_isolation),
)


def main() -> int:
    all_violations: list[str] = []
    width = max(len(name) for name, _ in CHECKS)

    for name, check in CHECKS:
        found = check()
        status = "FAIL" if found else "ok"
        print(f"  {name.ljust(width)}  {status}")
        all_violations.extend(found)

    if all_violations:
        print(f"\n{len(all_violations)} layer violation(s):\n", file=sys.stderr)
        for violation in all_violations:
            print(f"  {violation}\n", file=sys.stderr)
        return 1

    print("\nlayer rules: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

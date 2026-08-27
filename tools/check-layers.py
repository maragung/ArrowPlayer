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

`--self-test` builds throwaway trees in a temporary directory — a domain file
that includes a TagLib header, an ALSA include outside the sink directory, a
Kotlin file under shared-spec/ — and requires each to be caught, plus the
lookalikes that must not be. The four checks take their roots as arguments for
exactly that reason. Nothing is committed: a planted violation inside this
repository would be found by the gate itself, which is why OQ-045 asks for
synthetic inputs rather than fixtures.
"""

from __future__ import annotations

import argparse
import contextlib
import re
import sys
import tempfile
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
# shows up in review. Derived from a root rather than fixed to SRC so the
# self-test can point the same rule at a synthetic tree.
def domain_dirs(src: Path) -> list[Path]:
    return [
        src / "core",
        src / "audio" / "dsp",
        src / "audio" / "analysis",
        src / "audio" / "graph",
        src / "audio" / "decode",  # ports + pure parsers only; adapters excluded
        src / "theme",
    ]


# Adapter translation units that live inside an otherwise-domain directory.
# These are compiled into `eclipse-adapters`, not `eclipse-domain`.
def domain_exclusions(src: Path) -> set[Path]:
    return {src / "audio" / "decode" / "ffmpeg_decoder.cpp"}


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


def confinements(src: Path) -> list[Confinement]:
    sink = src / "audio" / "sink"
    return [
        Confinement("WASAPI", re.compile(r'^(audioclient|mmdeviceapi|avrt)\.h$'), sink),
        Confinement("ALSA", re.compile(r'^alsa/'), sink),
        Confinement("PulseAudio", re.compile(r'^pulse/'), sink),
        Confinement("PipeWire", re.compile(r'^pipewire/'), sink),
        Confinement("JACK", re.compile(r'^jack/'), sink),
        Confinement("FFmpeg", re.compile(r'^lib(avcodec|avformat|avutil|swresample)/'),
                    src / "audio" / "decode"),
        Confinement("TagLib", re.compile(r'^taglib/'), src / "library"),
        Confinement("SQLite", re.compile(r'^sqlite3?\.h$'), src / "library"),
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


def check_domain_purity(src: Path) -> list[str]:
    """Rule 1: nothing in the domain layer may reach a third party."""
    violations: list[str] = []
    excluded = domain_exclusions(src)
    for directory in domain_dirs(src):
        for path in iter_sources(directory):
            if path in excluded:
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


def check_adapter_confinement(src: Path) -> list[str]:
    """Rule 2: adapter headers stay inside their own adapter directory."""
    violations: list[str] = []
    rules = confinements(src)
    for path in iter_sources(src):
        for lineno, header in includes_of(path):
            for rule in rules:
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


# --------------------------------------------------------------------------- #
#  Rule 1 (ordering) — a layer may look down, never up
# --------------------------------------------------------------------------- #

LAYER_NAMES = {5: "PRESENTATION", 4: "APPLICATION", 3: "DOMAIN",
               2: "PORTS", 1: "ADAPTERS"}

# Directory -> layer, as paths relative to `desktop/`. Longest prefix wins, and
# `src` is last so that anything under it not claimed by a more specific entry
# is domain. That default is deliberate: a new directory of pure code needs no
# entry here, while a new *adapter* directory does — and forgetting to add one
# makes the adapter fail the check the moment it includes its port, which is a
# loud failure rather than a silent exemption.
LAYER_PREFIXES = (
    ("ui", 5),                  # desktop/ui — the Qt shell (§7.1 layer 5)
    ("src/app", 4),
    ("src/ports", 2),
    ("src/audio/sink", 1),      # WASAPI / ALSA / PulseAudio
    ("src/library", 1),         # TagLib / SQLite
    ("src/platform", 1),
    ("src", 3),
)

# The one translation unit whose job is to join the layers. §7.2 rule 1 read
# literally forbids it, so the exemption is named here rather than implied by a
# check that quietly skips it: main.cpp constructs layer 4 and hands control to
# layer 5, and something has to.
COMPOSITION_ROOT = "src/main.cpp"


def _internal_roots(src: Path) -> set[str]:
    """First path segments that mean "this include is ours". Derived from the
    layer table and from `domain_dirs` rather than restated, so a directory
    added to either is classified by both."""
    roots = {prefix.split("/")[1] for prefix, _ in LAYER_PREFIXES
             if prefix.startswith("src/")}
    roots.update(d.relative_to(src).parts[0] for d in domain_dirs(src))
    return roots


def _layer_of_relpath(relpath: str) -> int | None:
    """`relpath` is POSIX-relative to desktop/. Longest prefix wins."""
    parts = relpath.split("/")
    best: tuple[int, int] | None = None
    for prefix, layer in LAYER_PREFIXES:
        seg = prefix.split("/")
        if parts[:len(seg)] == seg and (best is None or len(seg) > best[0]):
            best = (len(seg), layer)
    return None if best is None else best[1]


def layer_of_file(path: Path, desktop: Path) -> int | None:
    """The layer a source file belongs to, or None if it is exempt."""
    try:
        relpath = path.relative_to(desktop).as_posix()
    except ValueError:
        return None
    if relpath == COMPOSITION_ROOT:
        return None
    # An adapter translation unit sitting in an otherwise-domain directory is an
    # adapter. Asked of `domain_exclusions` rather than listed again here.
    if path in domain_exclusions(desktop / "src"):
        return 1
    return _layer_of_relpath(relpath)


def layer_of_include(header: str, roots: set[str]) -> int | None:
    """The layer an #include names, or None if it is not ours to classify.
    Third-party and standard headers are None: rules 2 and 3 own those."""
    if header.startswith("eclipse/ui/"):
        return 5                      # desktop/ui's public include directory
    first = header.split("/")[0]
    if first not in roots or ".." in header:
        return None
    return _layer_of_relpath("src/" + header)


def check_layer_order(repo: Path) -> list[str]:
    """Rule 1: layer N may depend on layers < N, never on layers > N."""
    violations: list[str] = []
    desktop = repo / "desktop"
    roots = _internal_roots(desktop / "src")
    for root in (desktop / "src", desktop / "ui"):
        for path in iter_sources(root):
            here = layer_of_file(path, desktop)
            if here is None:
                continue
            for lineno, header in includes_of(path):
                there = layer_of_include(header, roots)
                if there is None:
                    continue
                # §7.1 numbers ADAPTERS 1 and PORTS 2, so rule 1's arithmetic
                # permits the one direction port/adapter design forbids and
                # forbids the one it requires. Both directions are therefore
                # stated outright, and the tension is recorded as OQ-055.
                if here == 1 and there == 2:
                    continue                        # implements its own port
                if there == 1 and here != 1:
                    violations.append(
                        f"{rel(path)}:{lineno}: layer {here} "
                        f"({LAYER_NAMES[here]}) includes <{header}>, which is "
                        f"an adapter (layer 1)\n"
                        f"    REQ-GEN-050(3): an adapter is reachable only "
                        f"through its layer-2 port. Only the composition root "
                        f"names one."
                    )
                elif there > here:
                    violations.append(
                        f"{rel(path)}:{lineno}: layer {here} "
                        f"({LAYER_NAMES[here]}) includes <{header}>, which is "
                        f"layer {there} ({LAYER_NAMES[there]})\n"
                        f"    REQ-GEN-050(1): a layer may depend on lower "
                        f"layers only. Invert the dependency through a "
                        f"layer-2 port."
                    )
    return violations


def check_shared_spec_has_no_code(spec_dir: Path) -> list[str]:
    """Rule 3: shared-spec/ is data and specification only."""
    violations: list[str] = []
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


def check_android_isolation(repo: Path) -> list[str]:
    """§5: android/ and desktop/ never import each other."""
    violations: list[str] = []
    for root, other in ((repo / "desktop", "android"), (repo / "android", "desktop")):
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


def checks_for(repo: Path):
    """The five checks bound to a tree. `main` binds the repository; the
    self-test binds a temporary directory."""
    src = repo / "desktop" / "src"
    return (
        ("domain layer purity", lambda: check_domain_purity(src)),
        ("layer ordering", lambda: check_layer_order(repo)),
        ("adapter confinement", lambda: check_adapter_confinement(src)),
        ("shared-spec has no code",
         lambda: check_shared_spec_has_no_code(repo / "shared-spec")),
        ("platform isolation", lambda: check_android_isolation(repo)),
    )


# --------------------------------------------------------------------------- #
#  Self-test
# --------------------------------------------------------------------------- #

@contextlib.contextmanager
def _tree(files: dict[str, str]):
    """Materialises {relative path: contents} in a temporary directory."""
    with tempfile.TemporaryDirectory(prefix="eclipse-layers-") as tmp:
        root = Path(tmp)
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text.strip("\n") + "\n", encoding="utf-8")
        yield root


# (label, files, expected violation count). The counts are exact rather than
# "> 0": a check that reports six violations for one bad include is also broken,
# just less visibly.
DOMAIN_CASES = [
    ("TagLib in the domain layer",
     {"core/library/index.hpp": '#include <taglib/tag.h>'}, 1),
    ("Qt in the domain layer",
     {"audio/dsp/meter.hpp": '#include <QString>'}, 1),
    ("a POSIX header in the real-time path",
     {"audio/graph/rt_thread.cpp": '#include <sys/mman.h>'}, 1),
    ("FFmpeg in a decode file that is not the adapter",
     {"audio/decode/probe.cpp": '#include <libavcodec/avcodec.h>'}, 1),
    ("SQLite reached from the theme layer",
     {"theme/store.cpp": '#include <sqlite3.h>'}, 1),
    ("windows.h in the domain layer",
     {"core/time/clock.hpp": '#include <windows.h>'}, 1),
    ("two offenders in one file are two violations",
     {"core/x.cpp": '#include <QWidget>\n#include <alsa/asoundlib.h>'}, 2),

    ("the standard library and internal headers",
     {"core/json/json.hpp": '#include <vector>\n#include <cstdint>\n'
                            '#include "core/error/result.hpp"'}, 0),
    ("the FFmpeg adapter, which is excluded by name",
     {"audio/decode/ffmpeg_decoder.cpp": '#include <libavcodec/avcodec.h>'}, 0),
    ("a header whose name merely starts with Q",
     {"core/collections/Queue.hpp": '#include "Queue.h"\n#include <queue>'}, 0),
    ("an adapter directory, which is not part of the domain layer",
     {"library/tag_reader.cpp": '#include <taglib/tag.h>'}, 0),
    ("a commented-out include",
     {"core/x.cpp": '// #include <QString>'}, 0),
]

ORDER_CASES = [
    ("the domain layer reaching into the application layer",
     {"desktop/src/core/session_cache.cpp": '#include "app/session.hpp"'}, 1),
    ("the domain layer reaching the UI",
     {"desktop/src/audio/dsp/meter.cpp": '#include <eclipse/ui/shell.hpp>'}, 1),
    ("the application layer reaching the UI",
     {"desktop/src/app/application.hpp": '#include <eclipse/ui/shell.hpp>'}, 1),
    ("a port naming an adapter, which rule 1's arithmetic would permit",
     {"desktop/src/ports/decoder.hpp": '#include "audio/sink/alsa_sink.hpp"'}, 1),
    ("the application layer naming an adapter directly",
     {"desktop/src/app/session.cpp": '#include "library/tag_reader.hpp"'}, 1),
    ("the domain layer naming an adapter directly",
     {"desktop/src/core/scan.cpp": '#include "platform/win32_paths.hpp"'}, 1),
    ("two upward includes in one file are two violations",
     {"desktop/src/core/x.cpp": '#include "app/a.hpp"\n'
                                '#include <eclipse/ui/shell.hpp>'}, 2),

    ("the application layer reaching the domain layer, which is downward",
     {"desktop/src/app/session.cpp": '#include "core/error.hpp"'}, 0),
    ("the UI reaching the application layer, which is downward",
     {"desktop/ui/src/shell.cpp": '#include "app/app_info.hpp"'}, 0),
    ("an adapter including the port it implements (§7.2 rule 3, OQ-055)",
     {"desktop/src/audio/sink/alsa_sink.hpp": '#include "ports/audio_sink.hpp"'}, 0),
    ("two files in the same layer",
     {"desktop/src/core/a.cpp": '#include "core/b.hpp"'}, 0),
    ("one adapter file including another in the same adapter directory",
     {"desktop/src/audio/sink/alsa_sink.cpp": '#include "audio/sink/alsa_pcm.hpp"'},
     0),
    ("the composition root, exempt by name",
     {"desktop/src/main.cpp": '#include "app/application.hpp"\n'
                              '#include <eclipse/ui/shell.hpp>'}, 0),
    ("third-party and standard headers, which other checks own",
     {"desktop/src/core/x.cpp": '#include <vector>\n#include <taglib/tag.h>'}, 0),
    ("a commented-out upward include",
     {"desktop/src/core/x.cpp": '// #include "app/session.hpp"'}, 0),
    ("a relative escape, which the isolation check owns",
     {"desktop/src/core/x.cpp": '#include "../app/session.hpp"'}, 0),
]

CONFINEMENT_CASES = [
    ("ALSA outside the sink directory",
     {"theme/renderer.cpp": '#include <alsa/asoundlib.h>'}, 1),
    ("SQLite outside src/library",
     {"core/db/cache.cpp": '#include <sqlite3.h>'}, 1),
    ("TagLib in the decode directory",
     {"audio/decode/probe.cpp": '#include <taglib/tag.h>'}, 1),
    ("WASAPI in the app layer",
     {"app/bootstrap.cpp": '#include <mmdeviceapi.h>'}, 1),

    ("ALSA in the sink directory, where it belongs",
     {"audio/sink/alsa_sink.cpp": '#include <alsa/asoundlib.h>'}, 0),
    ("ALSA in a subdirectory of the sink directory",
     {"audio/sink/linux/alsa_pcm.cpp": '#include <alsa/asoundlib.h>'}, 0),
    ("TagLib and SQLite in src/library, where they belong",
     {"library/tag_reader.cpp": '#include <taglib/tag.h>\n#include <sqlite3.h>'}, 0),
    ("FFmpeg in the decode directory, where it belongs",
     {"audio/decode/ffmpeg_decoder.cpp": '#include <libavformat/avformat.h>'}, 0),
]

SPEC_CASES = [
    ("Kotlin under shared-spec/",
     {"shared-spec/conformance/Cases.kt": 'object Cases'}, 1),
    ("C++ under shared-spec/",
     {"shared-spec/tools/gen.cpp": 'int main() { return 0; }'}, 1),
    ("data and prose under shared-spec/",
     {"shared-spec/schemas/theme.schema.json": '{}',
      "shared-spec/README.md": '# shared-spec',
      "shared-spec/grammars/efs.ebnf": 'expr = "x" ;'}, 0),
]

ISOLATION_CASES = [
    ("a desktop file including from android/",
     {"desktop/src/app/bridge.cpp": '#include "android/jni_bridge.h"'}, 1),
    ("an android file including from desktop/",
     {"android/app/src/main/cpp/nat.cpp":
      '#include "desktop/src/core/error/result.hpp"'}, 1),
    ("a nested reference to android/",
     {"desktop/src/x.cpp": '#include "../../android/shim.h"'}, 1),

    ("ordinary desktop includes",
     {"desktop/src/core/x.cpp": '#include "core/error/result.hpp"'}, 0),
    ("androidx, which is a different word",
     {"desktop/src/x.cpp": '#include <androidx/annotation.h>'}, 0),
    ("the word android in prose rather than an include",
     {"desktop/src/x.cpp": '// The android/ tree has its own copy of this table.'}, 0),
]


def self_test() -> int:
    failures = []

    def run(label, cases, invoke, prefix=""):
        for name, files, expected in cases:
            with _tree({prefix + k if prefix else k: v
                        for k, v in files.items()}) as root:
                found = invoke(root)
            if len(found) != expected:
                failures.append(
                    f"{label}: {name} — expected {expected} violation(s), "
                    f"got {len(found)}")

    run("domain purity", DOMAIN_CASES, check_domain_purity)
    run("layer ordering", ORDER_CASES, check_layer_order)
    run("adapter confinement", CONFINEMENT_CASES, check_adapter_confinement)
    run("shared-spec", SPEC_CASES,
        lambda root: check_shared_spec_has_no_code(root / "shared-spec"))
    run("platform isolation", ISOLATION_CASES, check_android_isolation)

    # A check that silently walks nothing passes every negative case above, so
    # assert that the positive cases fail *because of* the include and not
    # because the tree happened to be readable: the same file, one include
    # changed, must flip the verdict.
    with _tree({"core/x.hpp": '#include <taglib/tag.h>'}) as root:
        bad = check_domain_purity(root)
    with _tree({"core/x.hpp": '#include <string>'}) as root:
        good = check_domain_purity(root)
    if not (len(bad) == 1 and not good):
        failures.append("the same file with one include changed did not flip "
                        "the domain-purity verdict")

    # The same guard for the ordering check, which is the one most easily
    # written so that it never matches: an include table that classifies
    # nothing passes all seven negative cases above without walking a file.
    with _tree({"desktop/src/core/x.cpp": '#include "app/y.hpp"'}) as root:
        up = check_layer_order(root)
    with _tree({"desktop/src/app/x.cpp": '#include "core/y.hpp"'}) as root:
        down = check_layer_order(root)
    if not (len(up) == 1 and not down):
        failures.append("reversing the direction of one include did not flip "
                        "the layer-ordering verdict")

    ALL_CASES = (DOMAIN_CASES, ORDER_CASES, CONFINEMENT_CASES, SPEC_CASES,
                 ISOLATION_CASES)
    total = sum(len(c) for c in ALL_CASES)
    planted = sum(1 for c in ALL_CASES for case in c if case[2])

    if failures:
        print(f"layers self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  · {f}", file=sys.stderr)
        return 1
    print(f"layers self-test: {total} synthetic tree(s) over all five checks, "
          f"{planted} of them planted violations that must be caught")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true",
                    help="run the five checks over synthetic trees and exit")
    if ap.parse_args().self_test:
        return self_test()

    CHECKS = checks_for(REPO)
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

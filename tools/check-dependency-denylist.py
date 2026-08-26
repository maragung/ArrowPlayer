#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Dependency denylist gate — spec §19.5, §25.6 (REQ-SET-010, REQ-TST-024).

REQ-SET-010 does not say telemetry is disabled by default. It says the SDKs are
**absent from the dependency tree**, and that CI must assert it by scanning the
resolved graph against a denylist: "A policy that lives only in a README is not a
guarantee; a build that fails is." This script is that build failure.

The graph is assembled from every place a dependency can enter, so that adding one
somewhere unscanned is itself a failure rather than a way around the gate:

  desktop/vcpkg.json            direct ports and per-feature ports
  vcpkg dry-run output          the *resolved* graph including transitives, when
                                a run is piped in via --resolved-graph
  desktop/cmake/**              find_package / pkg_check_modules / FetchContent
  package.json                  Node dev tooling (commitlint, markdownlint)
  android/**                    Gradle version catalogs, when android/ exists

A name matches the denylist if the denylisted token appears as a whole component
of it — `com.google.firebase:firebase-analytics` matches `firebase`. Matching is on
word boundaries, never substrings, because substring matching produces false
positives that get the gate switched off, and a gate that is switched off protects
nothing.

Some vendor names are also ordinary words: `segment`, `heap`, `adjust`, `tune`,
`amplitude` — the last of which is a signal-processing term in an audio project.
Those are listed in GENERIC and need corroboration: either the whole dependency
name is exactly the token, or the name also carries a word like `analytics`,
`sdk` or `tracking`. So `com.segment.analytics:analytics-android` is denied while
`segment-tree` is not, and `amplitude` is denied while `amplitude-envelope` is
not. The alternative — dropping the generic vendors from the list — would leave
real telemetry SDKs unmatched, which is the worse failure.

Standard library only: no pip, no venv.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
#  The denylist. Grouped by the four categories REQ-TST-024 names, because a
#  reviewer adding an entry should have to say which category it falls in.
# ---------------------------------------------------------------------------
DENYLIST: dict[str, tuple[str, ...]] = {
    "analytics / product telemetry": (
        "google-analytics", "googleanalytics", "ga4", "gtag", "firebase",
        "firebase-analytics", "mixpanel", "amplitude", "segment", "posthog",
        "heap", "countly", "matomo", "piwik", "plausible", "umami", "flurry",
        "umeng", "appmetrica", "yandexmetrica", "clevertap", "moengage",
        "leanplum", "swrve", "localytics", "apptentive", "kochava", "singular",
        "fullstory", "smartlook", "hotjar", "logrocket", "mouseflow",
        "clarity-js", "opentelemetry", "statsd", "datadog", "dd-trace",
        "newrelic", "new-relic", "elastic-apm", "dynatrace", "appdynamics",
    ),
    "crash / error reporting": (
        # REQ-SET-012 requires crash reporting to be local-first: a crash writes a
        # report to the local logs directory and the user may choose to open it,
        # pre-filled, in their browser. That is us writing a file — it needs no
        # library, so no library here is a loss. A linked reporter is denied even
        # when configured offline, because REQ-SET-010 is about absence.
        "sentry", "sentry-native", "raven", "bugsnag", "rollbar", "raygun",
        "airbrake", "appcenter", "app-center", "crashlytics",
        "firebase-crashlytics", "acra", "instabug", "embrace", "bugly",
        "breakpad", "google-breakpad", "crashpad", "backtrace-io",
    ),
    "attribution / install tracking": (
        "adjust", "appsflyer", "appsflyer-sdk", "branch-sdk", "branchio",
        "tenjin", "tune", "mobileapptracking", "kissmetrics", "attributionkit",
        "facebook-sdk", "facebook-android-sdk", "fbsdk", "tiktok-sdk",
        "play-install-referrer", "installreferrer",
    ),
    "advertising": (
        "admob", "play-services-ads", "play-services-measurement", "doubleclick",
        "applovin", "ironsource", "unityads", "unity-ads", "vungle",
        "chartboost", "tapjoy", "adcolony", "inmobi", "mopub", "pangle",
        "mintegral", "smaato", "fyber", "criteo", "taboola", "outbrain",
        "onesignal", "urbanairship", "airship-sdk", "braze", "appboy",
    ),
}

# Advertising-id and fingerprinting surfaces. REQ-SET-010 names "no advertising
# id access, no fingerprinting" separately from the SDKs, and these are the
# library-level ways they arrive.
DENYLIST["advertising id / fingerprinting"] = (
    "play-services-ads-identifier", "advertising-identifier", "fingerprintjs",
    "fingerprint2", "clientjs", "deviceid", "openudid", "ssaid",
)

# ---------------------------------------------------------------------------
#  Splitting a package name into comparable components.
#
#  Ports, Gradle coordinates and pkg-config module names all use different
#  separators, and one of them (`.`) also separates the parts of a coordinate we
#  do want to compare. So: lowercase, split on non-alphanumerics, and compare
#  both the individual components and every adjacent pair, which is what lets
#  `play-services-ads` match `com.google.android.gms:play-services-ads`.
# ---------------------------------------------------------------------------
SPLIT = re.compile(r"[^a-z0-9]+")

# Denylist tokens that are also ordinary words. These need corroboration — see the
# module docstring. Every one of them is a real vendor; none is dropped.
GENERIC = frozenset({
    "segment", "heap", "adjust", "tune", "amplitude", "branch-sdk", "singular",
    "embrace", "airship-sdk", "raven", "breakpad", "deviceid", "ssaid",
    "ga4", "gtag",
})

# Words that corroborate a generic vendor name: a dependency that pairs one of
# these with a generic token is a telemetry SDK, not a coincidence.
CORROBORATING = frozenset({
    "analytics", "analytic", "telemetry", "tracking", "tracker", "track",
    "attribution", "attribute", "ads", "ad", "advert", "advertising", "adserver",
    "crash", "crashes", "error", "errors", "reporting", "reporter", "metrics",
    "monitoring", "insights", "sdk", "measurement", "session", "replay",
    "heatmap", "apm", "rum", "beacon", "pixel", "install", "referrer",
})


def variants(name: str) -> set[str]:
    parts = [p for p in SPLIT.split(name.lower()) if p]
    out = {name.lower(), "".join(parts), "-".join(parts)}
    out.update(parts)
    for i in range(len(parts) - 1):
        out.add(f"{parts[i]}-{parts[i + 1]}")
        out.add(f"{parts[i]}{parts[i + 1]}")
    for i in range(len(parts) - 2):
        out.add("-".join(parts[i:i + 3]))
    return out


def denied(name: str) -> list[tuple[str, str]]:
    """(category, matched token) for every denylist hit against this name."""
    forms = variants(name)
    parts = {p for p in SPLIT.split(name.lower()) if p}
    whole = "-".join(p for p in SPLIT.split(name.lower()) if p)
    corroborated = bool(parts & CORROBORATING)

    hits = []
    for category, tokens in DENYLIST.items():
        for token in tokens:
            normalised = "-".join(t for t in SPLIT.split(token) if t)
            if token not in forms and normalised not in forms:
                continue
            if token in GENERIC and not (corroborated or whole == normalised):
                continue
            hits.append((category, token))
    return hits


# ---------------------------------------------------------------------------
#  Graph collection. Each collector returns (source description, [names]).
# ---------------------------------------------------------------------------
def from_vcpkg_manifest() -> tuple[str, list[str]]:
    path = REPO / "desktop" / "vcpkg.json"
    if not path.exists():
        return ("desktop/vcpkg.json", [])
    manifest = json.loads(path.read_text(encoding="utf-8"))
    names: list[str] = []

    def add_dep(dep: object) -> None:
        if isinstance(dep, str):
            names.append(dep)
        elif isinstance(dep, dict) and "name" in dep:
            names.append(str(dep["name"]))

    for dep in manifest.get("dependencies", []):
        add_dep(dep)
    for feature in manifest.get("features", {}).values():
        if isinstance(feature, dict):
            for dep in feature.get("dependencies", []):
                add_dep(dep)
    return ("desktop/vcpkg.json", names)


def from_resolved_graph(path: Path) -> tuple[str, list[str]]:
    """Parse `vcpkg install --dry-run` output.

    Its package lines look like:  `  * name[feat,feat]:triplet@version#port`
    """
    names: list[str] = []
    line_re = re.compile(r"^\s*\*?\s*([A-Za-z0-9][A-Za-z0-9._+-]*)(?:\[[^\]]*\])?:")
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = line_re.match(raw)
        if m:
            names.append(m.group(1))
    return (f"resolved graph ({path.name})", names)


def from_cmake() -> tuple[str, list[str]]:
    names: list[str] = []
    patterns = (
        re.compile(r"find_package\s*\(\s*([A-Za-z0-9_.+-]+)", re.I),
        re.compile(r"pkg_check_modules\s*\([^)]*?IMPORTED_TARGET\s+([^)]*)\)", re.I | re.S),
        re.compile(r"FetchContent_Declare\s*\(\s*([A-Za-z0-9_.+-]+)", re.I),
        re.compile(r"CPMAddPackage\s*\(\s*\"?([A-Za-z0-9_.+/-]+)", re.I),
    )
    roots = [REPO / "desktop"]
    for root in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.name != "CMakeLists.txt" and path.suffix != ".cmake":
                continue
            if "third_party" in path.parts or "build" in path.parts:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for pattern in patterns:
                for m in pattern.finditer(text):
                    for token in re.split(r"[\s;]+", m.group(1)):
                        token = token.split(">=")[0].split("=")[0].strip()
                        if token and not token.startswith("$"):
                            names.append(token)
    return ("desktop/**/CMakeLists.txt and *.cmake", names)


def from_package_json() -> tuple[str, list[str]]:
    path = REPO / "package.json"
    if not path.exists():
        return ("package.json", [])
    data = json.loads(path.read_text(encoding="utf-8"))
    names: list[str] = []
    for key in ("dependencies", "devDependencies", "optionalDependencies"):
        names.extend(data.get(key, {}).keys())
    return ("package.json", names)


def from_package_lock() -> tuple[str, list[str]]:
    """Every installed npm package, not just the ones we asked for.

    Three direct devDependencies pull in well over a hundred transitive packages,
    and a telemetry SDK arriving as somebody else's dependency is denied for the
    same reason it would be as ours — REQ-SET-010 is about what ships and what
    phones home, not about who wrote the manifest entry. The lockfile is the
    resolved graph and it is committed, so unlike the vcpkg side (OQ-025) this
    needs no separate resolution step.
    """
    path = REPO / "package-lock.json"
    if not path.exists():
        return ("package-lock.json (absent)", [])
    data = json.loads(path.read_text(encoding="utf-8"))
    names: list[str] = []
    for key in data.get("packages", {}):
        if not key:
            continue  # the root project itself
        # "node_modules/a/node_modules/@scope/b" → "@scope/b"
        names.append(key.rsplit("node_modules/", 1)[-1])
    return ("package-lock.json (resolved graph)", sorted(set(names)))


def from_gradle() -> tuple[str, list[str]]:
    android = REPO / "android"
    if not android.exists():
        return ("android/** (absent)", [])
    names: list[str] = []
    for path in sorted(android.rglob("*.toml")) + sorted(android.rglob("*.gradle.kts")):
        text = path.read_text(encoding="utf-8", errors="replace")
        names.extend(re.findall(r'"([a-z0-9.-]+:[a-z0-9.-]+)(?::[^"]*)?"', text))
        names.extend(re.findall(r'module\s*=\s*"([^"]+)"', text))
    return ("android/** version catalogs", names)


# ---------------------------------------------------------------------------
#  Self-test corpus.
#
#  A denylist is only as good as its matcher, and a matcher tuned by hand is
#  exactly the kind of thing that silently rots. These two lists are the
#  behaviour, committed: MUST_DENY names that have to match, MUST_ALLOW names
#  that must not. Half of MUST_ALLOW is there because it *looks* like telemetry
#  to a substring matcher — `segment-tree`, `amplitude-envelope`, `adjust-gain`,
#  `fine-tune` — and in an audio project those are ordinary words.
# ---------------------------------------------------------------------------
MUST_DENY = (
    "com.google.firebase:firebase-analytics",
    "firebase-crashlytics",
    "io.sentry:sentry-android",
    "sentry-native",
    "@sentry/node",
    "com.bugsnag:bugsnag-android",
    "google-breakpad",
    "com.google.android.gms:play-services-ads",
    "com.google.android.gms:play-services-ads-identifier",
    "com.google.android.gms:play-services-measurement",
    "com.appsflyer:af-android-sdk",
    "io.branch.sdk.android:library",
    "com.adjust.sdk:adjust-android",
    "com.segment.analytics.android:analytics",
    "@heap/analytics",
    "amplitude",
    "segment",
    "mixpanel-android",
    "opentelemetry-cpp",
    "datadog-agent",
    "statsd-client",
    "io.embrace:embrace-android-sdk",
    "com.urbanairship.android:airship-sdk",
    "com.microsoft.clarity:clarity-js",
)

MUST_ALLOW = (
    # The real dependency graph, direct and transitive.
    "sqlite3", "zlib", "libzip", "taglib", "libsamplerate", "ffmpeg",
    "soundtouch", "chromaprint", "projectm", "rtaudio", "gtest", "utfcpp",
    "glm", "opengl", "opengl-registry", "egl-registry", "projectm-eval",
    "pkgconf", "vcpkg-cmake", "vcpkg-cmake-config", "Qt6", "PkgConfig",
    "SQLite3", "Threads", "Git", "nlohmann-json", "alsa", "pulse",
    "@commitlint/cli", "markdownlint-cli2",
    # Android, for when android/ exists.
    "androidx.media3:media3-exoplayer", "org.jetbrains.kotlin:kotlin-stdlib",
    "com.google.dagger:hilt-android", "androidx.room:room-runtime",
    # Words a substring matcher would get wrong. All plausible in audio code.
    "segment-tree", "heap-profiler-none", "amplitude-envelope", "adjust-gain",
    "tune-pitch", "fine-tune", "branch-predictor", "raven-parser",
    "error-code", "session-manager", "audio-metrics", "embrace-warmth",
    "clarity", "stack-protector",
)


def self_test() -> int:
    failures = []
    for name in MUST_DENY:
        if not denied(name):
            failures.append(f"MISSED  {name}: should be denied, is not")
    for name in MUST_ALLOW:
        hits = denied(name)
        if hits:
            tokens = ", ".join(sorted({t for _, t in hits}))
            failures.append(f"FALSE POSITIVE  {name}: matched {tokens}")
    if failures:
        print(f"{len(failures)} matcher failure(s):\n", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        f"denylist matcher: {len(MUST_DENY)} name(s) correctly denied, "
        f"{len(MUST_ALLOW)} correctly allowed"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--resolved-graph",
        type=Path,
        help="output of `vcpkg install --dry-run`, so transitive ports are scanned too",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the matcher against its committed corpus and exit",
    )
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    sources = [
        from_vcpkg_manifest(),
        from_cmake(),
        from_package_json(),
        from_package_lock(),
        from_gradle(),
    ]
    if args.resolved_graph:
        if not args.resolved_graph.exists():
            print(f"FATAL: {args.resolved_graph} does not exist", file=sys.stderr)
            return 2
        sources.append(from_resolved_graph(args.resolved_graph))

    findings: list[str] = []
    total = 0
    for where, names in sources:
        unique = sorted(set(names))
        total += len(unique)
        for name in unique:
            hits = denied(name)
            if not hits:
                continue
            # One finding per dependency, not per matching token: several tokens
            # matching the same name is one problem reported once.
            category = hits[0][0]
            tokens = ", ".join(sorted({t for _, t in hits}))
            findings.append(f"{where}: {name}  → {category} (matched: {tokens})")

    tokens = sum(len(v) for v in DENYLIST.values())

    if findings:
        print(f"{len(findings)} denylisted dependenc(ies) in the graph:\n", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        print(
            "\nREQ-SET-010: analytics, crash-reporting, attribution and advertising\n"
            "SDKs must be ABSENT from the dependency tree — not disabled by a flag.\n"
            "REQ-SET-011 makes introducing any of them a major-version change that\n"
            "requires an opt-in prompt defaulting to no, field-by-field documentation\n"
            "in docs/PRIVACY.md, and a changelog entry at the top of the release notes.",
            file=sys.stderr,
        )
        return 1

    print(f"dependency denylist: {total} dependenc(ies) scanned against {tokens} tokens, none denied")
    for where, names in sources:
        count = len(set(names))
        print(f"  · {where}: {count}" + ("" if count else "  (nothing to scan)"))
    if not args.resolved_graph:
        print(
            "  NOT checked here: transitive ports. Pass --resolved-graph with the\n"
            "  output of `vcpkg install --dry-run` to include them. §25.4 assigns\n"
            "  that to security.yml, which is not written yet, so nothing runs it\n"
            "  for you (OQ-015)."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())

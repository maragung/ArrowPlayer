# ADR 0005 — Qt via aqtinstall, everything else via vcpkg

- **Status:** Accepted
- **Date:** 2026-08-23
- **Requirements:** REQ-BLD-001, REQ-BLD-022, REQ-GEN-013

## Context

Specification v1.x said dependencies were "pinned via **vcpkg** for reproducible,
cache-friendly CI builds on both platforms", and listed Qt among them.

Two problems surfaced when planning the build:

1. **Building Qt through vcpkg is a CI trap.** The `qtbase` port compiles Qt from
   source. On a cache miss that is hours per platform, and the Qt ports are a
   well-known source of build fragility. A pipeline that occasionally takes three
   hours is a pipeline people learn to ignore.

2. **System Qt is too old on a Tier-1 target.** Ubuntu 22.04 LTS ships Qt 6.2.4.
   The skin engine's QML surfaces target 6.8 LTS. Using the system package would
   either drop 22.04 from Tier 1 or force the UI down to 6.2.

## Decision

- **Qt** comes from official prebuilt binaries via **`aqtinstall`**, at the exact
  version pinned in `desktop/qt-version.txt`. CI caches the install by version key.
- **Everything else** (SQLite, FFmpeg, TagLib, libsamplerate, SoundTouch,
  Chromaprint, libzip, GoogleTest) comes from **vcpkg** in manifest mode with a
  pinned `builtin-baseline`.
- `desktop/vcpkg.json` **must not list Qt.**

## Consequences

**Positive.** Qt install drops from hours to minutes and is deterministic. The
pinned version is a single-line change, visible in review. Qt arrives as shared
libraries, which is what REQ-GEN-013 requires anyway for LGPL-3.0 compliance —
the vcpkg route would have made it easy to accidentally produce a static Qt and
violate the licence.

**Negative.** Two dependency mechanisms instead of one, so `docs/BUILDING.md` has
to document both, and a contributor must run one extra command. Accepted: the
alternative costs every contributor and every CI run.

**Negative.** `aqtinstall` is a third-party tool wrapping Qt's download servers.
If it breaks, Qt acquisition breaks. Mitigated by pinning `aqtinstall` itself and
by the fact that a Qt tarball can be fetched manually as a fallback.

## Alternatives considered

**vcpkg for Qt as well** — rejected for the build-time and fragility reasons above.

**System Qt via `apt`** — rejected: 6.2.4 on Ubuntu 22.04, and unpinnable across
distributions, which defeats reproducibility.

**Conan** — rejected: a second general-purpose package manager with no advantage
over vcpkg for our non-Qt dependencies, and the same from-source Qt problem.

# Third-Party Licences — Arrow Player

`arrow-player.md` §27 requires this document: *Generated; the §4.2 register with SPDX ids, versions, licence texts, and source URLs*. This is that document. It is **generated** by `tools/gen-third-party/gen-third-party.py` from `tools/gen-third-party/register.json` and MUST NOT be edited by hand — fix the generator or the register, never the output (`REQ-GEN-012`).

Scope: the **desktop** build. The Android half of the §4.2 register is listed at the end for completeness and is **not in the current Android build** — the app exists as a Phase 0 scaffold, but the NDK-level components below are not in it yet, and the Gradle version catalog is reconciled with this half of the register when they arrive (ADR 0012, OQ-018). §0.1 rule 2 forbids silently downgrading a requirement, so those entries are kept and marked, not dropped.

> **Generation mode: direct-only.** This document was generated **without** a resolved dependency graph, so it covers the **direct** dependencies of §4.2 and does **not** enumerate the transitive set. This is the honest degraded mode described in the open-questions log under OQ-025 — see the *Transitive dependencies* section for how to regenerate with the full graph.

- [How this document is generated](#how-this-document-is-generated)
- [Desktop dependencies — the §4.2 register](#desktop-dependencies--the-42-register)
- [Qt — exact version, configuration, and source](#qt--exact-version-configuration-and-source)
- [FFmpeg — the LGPL configuration](#ffmpeg--the-lgpl-configuration)
- [Transitive dependencies](#transitive-dependencies)
- [Codec patent notes](#codec-patent-notes)
- [Licence texts and the source offer](#licence-texts-and-the-source-offer)
- [Android dependencies — listed for completeness, not in this build](#android-dependencies--listed-for-completeness-not-in-this-build)
- [Trademark and asset hygiene](#trademark-and-asset-hygiene)

## How this document is generated

The §4.2 register is held as data in `tools/gen-third-party/register.json`, transcribed from the specification. This document is emitted from it. The reasoning is `REQ-GEN-012`'s own: a register *“kept accurate”* is only meaningful if a machine can reproduce it and a gate fails when the two diverge.

Three checks run in the generator, each naming the requirement it enforces:

1. **Cross-check against `desktop/vcpkg.json` (`REQ-GEN-012`).** Every port the manifest asks for — direct and per-feature — must map to a register entry. If one does not, the generator refuses to write and fails, rather than emit a document that misrepresents the build.
2. **Freshness (`--check`).** CI regenerates the document in memory and compares it to the committed file; a stale document fails the build. §25.5 step 8 and §25.6 both require the release to regenerate this document and fail if it is stale.
3. **Transitive coverage (`--resolved-graph`, OQ-025).** Given the output of `vcpkg install --dry-run`, every resolved port must be described by the register, the transitive reference, or the build-only list; a port described nowhere is a component nobody has looked at, and fails the gate.

This is a different question from the one `tools/check-dependency-denylist.py` answers. That gate looks for components that must be **absent** (telemetry, crash-reporting, attribution, advertising; `REQ-SET-010`); this one checks that every component that is **present** is accounted for. The two are complementary and are not duplicated.

**Licence texts are referenced, not embedded.** Each entry carries its SPDX identifier and a link to the canonical SPDX text, plus the corresponding source URL. The verbatim texts are materialised at package time — vcpkg writes each port's exact text to `vcpkg_installed/<triplet>/share/<port>/copyright`, and Qt's full LGPL-3.0 text ships to `licenses/LGPL-3.0.txt` (`REQ-GEN-013`(3)). The Help → Third-Party Licences screen is generated from this register at build time and shows the full texts in the application (`REQ-GEN-019`); it is never hand-maintained in a second place.

## Desktop dependencies — the §4.2 register

The register of **direct** dependencies, transcribed from §4.2 and kept in `register.json`. “Linkage” and “Obligation” are the specification's own columns; “SPDX id” and the exact “Version” are what §27 additionally requires here.

| Component | SPDX id | Version | Linkage | Obligation | Source |
|---|---|---|---|---|---|
| Qt 6 | `LGPL-3.0-only` | 6.8.2 | Dynamic, always | Provide relinking ability + licence text + source offer. Static linking is FORBIDDEN (§4.3). | [https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz](https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz) |
| FFmpeg (libavformat, libavcodec, libavutil, libswresample) | `LGPL-2.1-or-later` | 7.1.2 (vcpkg port-version 5) | Dynamic | Built without `--enable-gpl` and without `--enable-nonfree` (§4.4). Publish source offer. | [https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz](https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz) |
| TagLib | `LGPL-2.1-or-later OR MPL-1.1` | 2.x | Dynamic | Source offer if the LGPL arm is chosen. | [https://github.com/taglib/taglib](https://github.com/taglib/taglib) |
| SoundTouch | `LGPL-2.1-or-later` | 2.3.x | Dynamic | Source offer. | [https://www.surina.net/soundtouch/](https://www.surina.net/soundtouch/) |
| libsamplerate | `BSD-2-Clause` | ≥ 0.2.2 | Static OK | Attribution. | [https://github.com/libsndfile/libsamplerate](https://github.com/libsndfile/libsamplerate) |
| Chromaprint | `LGPL-2.1-or-later` | 1.5.x | Dynamic | Source offer. | [https://github.com/acoustid/chromaprint](https://github.com/acoustid/chromaprint) |
| projectM | `LGPL-2.1-or-later` | 4.x | Dynamic | Source offer. | [https://github.com/projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm) |
| SQLite | `blessing` | 3.4x | Static OK | None. | [https://www.sqlite.org/](https://www.sqlite.org/) |
| nlohmann/json | `MIT` | 3.11.x | Static OK | Attribution. | [https://github.com/nlohmann/json](https://github.com/nlohmann/json) |
| libzip | `BSD-3-Clause` | current | Static OK | Attribution. | [https://libzip.org/](https://libzip.org/) |
| zlib | `Zlib` | 1.3.x | Static OK | Attribution. | [https://zlib.net/](https://zlib.net/) |
| GoogleTest | `BSD-3-Clause` | 1.15.2 | Test-only | Not shipped. | [https://github.com/google/googletest](https://github.com/google/googletest) |
| RtAudio | `MIT` | 6.x | Dynamic | Attribution. | [https://github.com/thestk/rtaudio](https://github.com/thestk/rtaudio) |

SPDX texts: each identifier above resolves at `https://spdx.org/licenses/<id>.html`. Where the displayed licence differs from a bare SPDX id, the reason is in the notes below.

### Per-dependency notes

- **Qt 6** — LGPL-3.0-only. Official prebuilt shared libraries via aqtinstall at the version pinned in desktop/qt-version.txt. NOT a vcpkg dependency (ADR 0005 / REQ-BLD-001); desktop/vcpkg.json MUST NOT list Qt.
  - Exact version 6.8.2 (desktop/qt-version.txt); the register names the 6.8 LTS series.
  - REQ-GEN-013(4) requires this document to state the exact Qt version and configure flags and link to the corresponding source archive.
  - REQ-GEN-013(3): the full LGPL-3.0 text ships in the installed tree at licenses/LGPL-3.0.txt and is reachable from Help -> Licences.
  - REQ-GEN-013(2)/(5): the user MUST be able to replace the Qt shared libraries with a compatible build; no checksum or signature gate over Qt binaries, no anti-tivoization.
- **FFmpeg (libavformat, libavcodec, libavutil, libswresample)** — LGPL-2.1-or-later. vcpkg, pinned by override to 7.1.2#5 in desktop/vcpkg.json (features avcodec, avformat, swresample, zlib; default features off).
  - REQ-GEN-014 fixes the configure flags above; REQ-GEN-015 asserts at build time that the linked FFmpeg reports LGPL and not `GPL version`/`nonfree` via `avutil_license()` — a mechanical guard, not a review step (ADR 0006).
  - v1.0 decodes only. Encoders are enabled selectively and only for LGPL-clean codecs when the converter lands (REQ-GEN-016); `libfdk_aac` is permanently excluded as non-free.
- **TagLib** — LGPL-2.1-or-later OR MPL-1.1. vcpkg (pinned baseline).
  - Dual-licensed. Arrow takes the LGPL-2.1-or-later arm, so the source-offer obligation applies (recorded in the companion source-offer document).
- **SoundTouch** — LGPL-2.1-or-later. vcpkg, behind the `tempo` feature (on by default) mirroring the ARROW tempo/BPM options (REQ-AUD-090..093).
- **libsamplerate** — BSD-2-Clause. vcpkg (pinned baseline).
  - The ≥ 0.2.2 floor folds in two separate limits: releases before 0.1.9 were GPL and MUST NOT be used (they are incompatible with the MPL-2.0 core, §4.1), and anything below the registered version is a dependency the register does not describe, which REQ-GEN-012 makes a build failure. ArrowDependencies.cmake enforces `samplerate>=0.2.2`.
- **Chromaprint** — LGPL-2.1-or-later. vcpkg, behind the `fingerprint` feature (on by default). Local and offline; the AcoustID network lookup is a separate opt-in (§9.9).
- **projectM** — LGPL-2.1-or-later. vcpkg, behind the `visualizer` feature (on by default). Pulls transitive ports (see the transitive section) — glm, projectm-eval, and the OpenGL/EGL registries.
  - ADR 0009 records the choice of projectM for MilkDrop-compatible visualisation.
- **SQLite** — Public domain (SQLite Blessing; SPDX `blessing`). vcpkg, with the `fts5` feature for full-text search.
  - SQLite is dedicated to the public domain; SPDX records this as the `blessing` identifier (the 'SQLite Blessing'). No obligation, no attribution required.
- **nlohmann/json** — MIT. Listed in the §4.2 register. NOT currently in the build: the desktop tree uses a bespoke, hardened in-house parser (`arrow::json`, MPL-2.0, desktop/src/core/json/) rather than nlohmann/json, so it is registered but not linked today.
  - Kept in the register because §4.2 lists it and REQ-GEN-012 forbids silently dropping a registered dependency; the honest status is 'registered, not linked in the current build'.
- **libzip** — BSD-3-Clause. vcpkg with `default-features: false`. The default features pull in bzip2 and OpenSSL; both are refused — REQ-THM-015 allows a skin archive only 'deflate or stored, no encryption, no other compression methods', and OpenSSL has no row in §4.2 (see OQ-025).
  - The register offers `libzip` or `minizip-ng`; Arrow uses libzip (BSD-3-Clause). minizip-ng would be the zlib-licensed alternative.
- **zlib** — Zlib. vcpkg (pinned baseline).
- **GoogleTest** — BSD-3-Clause. vcpkg behind the `tests` feature, and pinned as a vendored tarball desktop/third_party/googletest-1.15.2.tar.gz with its SHA-256 asserted at build time. Test-only; never present in a shipped artifact.
  - Because it is test-only and not shipped, it carries no runtime attribution or source-offer obligation.
- **RtAudio** — MIT-like (SPDX `MIT`; RtAudio adds a request to notify upstream of modifications). vcpkg behind the `rtaudio` feature, which is OFF by default (matches the ARROW_ENABLE_RTAUDIO CMake option). Optional last-resort Linux fallback sink only (REQ-AUD-073, [v1.x], §8.7.7); never bit-perfect, never a platform default.
  - The RtAudio licence is MIT with an added request that modifications be sent upstream; there is no distinct SPDX identifier, so it is recorded as `MIT` with this note.

## Qt — exact version, configuration, and source

`REQ-GEN-013`(4) requires this document to state the exact Qt version and the configure flags used, and to link the corresponding source archive.

- **Exact version:** 6.8.2 (pinned in `desktop/qt-version.txt`; the register names the 6.8 LTS series).
- **Configuration:** official prebuilt **shared** libraries obtained via `aqtinstall`, not built from source and not from vcpkg (ADR 0005 / `REQ-BLD-001`). `desktop/vcpkg.json` MUST NOT list Qt. Qt is therefore **dynamically linked, always** — static Qt is forbidden in every shipped artifact (`REQ-GEN-013`(1)).
- **Relinking duty (`REQ-GEN-013`(2),(5)):** the user MUST be able to replace a Qt shared library with a compatible build and still run Arrow Player. No checksums or signature checks are applied over Qt binaries, and the application is not shipped in a form that prevents installing a modified Qt.
- **Licence text (`REQ-GEN-013`(3)):** the full LGPL-3.0 text ships in the installed tree at `licenses/LGPL-3.0.txt` and is reachable from Help → Licences.
- **Corresponding source:** [https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz](https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz)

The precise per-release source archive, keyed by release tag, is recorded in the LGPL source-offer document `docs/LGPL-SOURCE-OFFER.md` (`REQ-GEN-020`).

## FFmpeg — the LGPL configuration

`REQ-GEN-014` fixes the FFmpeg configuration for shipped artifacts, and `REQ-GEN-015` asserts it mechanically at build time. FFmpeg can be built LGPL or GPL depending on flags; a single missing flag would be a licence violation invisible in the source tree (ADR 0006). The shipped configuration is:

```text
--disable-gpl
--disable-nonfree
--disable-programs
--disable-doc
--disable-encoders
--disable-muxers
--disable-filters
--disable-devices
--disable-network
--enable-shared
--disable-static
```

`REQ-GEN-015`: CI verifies at build time that the linked FFmpeg reports `LGPL` and neither `GPL version` nor `nonfree` via `avutil_license()`, and fails the build otherwise. This is a mechanical guard, not a review step — a configure flag recorded in a document is a flag that eventually goes wrong; a test that fails the build is a flag that stays right.

- **Pinned version:** 7.1.2 (vcpkg port-version 5) (override in `desktop/vcpkg.json`).
- **Corresponding source:** [https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz](https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz)
- **v1.0 decodes only.** Encoders are enabled selectively and only for LGPL-clean codecs when the converter lands (`REQ-GEN-016`); `libfdk_aac` is permanently excluded as non-free.

## Transitive dependencies

This document was generated **without** a resolved graph, so the transitive set is **not enumerated here**. This is deliberate and honest: `vcpkg` is not installed on the machine that produced this document, so no real `vcpkg install --dry-run` output was available, and inventing one would be worse than omitting it.

To regenerate with the transitive set covered, once vcpkg is available:

```sh
vcpkg install --dry-run --triplet x64-linux-arrow > /tmp/graph.txt
python3 tools/gen-third-party/gen-third-party.py --resolved-graph /tmp/graph.txt
```

OQ-025 in the open-questions log ([OPEN-QUESTIONS.md](OPEN-QUESTIONS.md)) records the design: §4.2 is the register of **direct** dependencies, and this document additionally carries the transitive set when a graph is supplied, so the CI gate compares the resolved graph against this document rather than against §4.2 alone. docs/OPEN-QUESTIONS.md OQ-025 also records that one resolved graph for `x64-linux-arrow` pulled in six transitive components (`utfcpp`, `glm`, `projectm-eval`, and the OpenGL/EGL registries), all licence-compatible, and that `libzip`'s bzip2/OpenSSL default features were removed rather than registered.

## Codec patent notes

`REQ-GEN-017` requires this document to record the following.

- MP3: all known essential patents expired by April 2017; decoding and encoding are unencumbered.
- AAC: decoded via FFmpeg's native LGPL decoder. Patent pools exist for AAC; because we distribute source and unmodified LGPL binaries and charge nothing, we take the same position as every Linux distribution. Encoding to AAC is not offered (REQ-GEN-016).
- Monkey's Audio (APE): decoded via FFmpeg's native LGPL `ape` decoder. The upstream Monkey's Audio SDK's bespoke licence MUST NOT be vendored.
- DSD: decoded to PCM by FFmpeg's LGPL `dsd_*` decoders. No proprietary component.

## Licence texts and the source offer

As stated above, licence texts are **referenced, not embedded** in this document. Each SPDX identifier resolves to its canonical text at `https://spdx.org/licenses/<id>.html`, and the exact per-port text is written by vcpkg to `vcpkg_installed/<triplet>/share/<port>/copyright` at install time. The in-application Help → Third-Party Licences screen is generated from this register and carries the full texts (`REQ-GEN-019`).

**Source offer.** The components under an LGPL arm carry a source-offer obligation: `Chromaprint`, `FFmpeg`, `Qt 6`, `SoundTouch`, `TagLib`, `projectM`. The written source offer — the precise source archive for every LGPL component in every release, keyed by release tag — is published in the companion document `docs/LGPL-SOURCE-OFFER.md` (`REQ-GEN-020`), and each release regenerates it (§25.5 step 8).

## Android dependencies — listed for completeness, not in this build

> Listed for completeness. The Android app exists as a Phase 0 scaffold (ADR 0012 restored the target ADR 0011 had deferred) but none of these NDK-level components is in its build yet; the Gradle version catalog is reconciled with this half of the register when the components arrive (OQ-018). §0.1 rule 2 forbids silently downgrading a requirement, so the Android half is kept here and rendered, marked not-yet-shipped, rather than dropped.

| Dependency | SPDX id | Note | Source |
|---|---|---|---|
| AndroidX Media3 (ExoPlayer) | `Apache-2.0` | Clean. | [https://github.com/androidx/media](https://github.com/androidx/media) |
| AndroidX (Compose, Room, WorkManager, DataStore, Lifecycle) | `Apache-2.0` | Clean. | [https://developer.android.com/jetpack/androidx](https://developer.android.com/jetpack/androidx) |
| Hilt / Dagger | `Apache-2.0` | Clean. | [https://github.com/google/dagger](https://github.com/google/dagger) |
| Kotlin stdlib / coroutines | `Apache-2.0` | Clean. | [https://github.com/JetBrains/kotlin](https://github.com/JetBrains/kotlin) |
| projectM (Android) | `LGPL-2.1-or-later` | Dynamic `.so`, source offer. | [https://github.com/projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm) |
| Chromaprint (NDK) | `LGPL-2.1-or-later` | Dynamic `.so`, source offer. | [https://github.com/acoustid/chromaprint](https://github.com/acoustid/chromaprint) |

## Trademark and asset hygiene

- No Winamp, AIMP, foobar2000, or other third-party trademark, logo, skin, icon, or sound asset is copied into this repository (REQ-GEN-022). Reference players are studied for design and behaviour, never for assets; built-in skins are original work.
- Bundled fonts MUST be OFL-1.1, Apache-2.0, or public domain, and their licence files MUST ship alongside them (REQ-GEN-023).

---

Generated by `tools/gen-third-party/gen-third-party.py` from `tools/gen-third-party/register.json`. To change the register, edit that file and regenerate; to verify freshness in CI, run the generator with `--check`. No timestamp is written, so the output is deterministic and `--check` is stable.

# Eclipse Player — Master Build Specification & Prompt

| | |
|---|---|
| **Document version** | 2.0.0 |
| **Status** | Approved for implementation |
| **Last revised** | 2026-08-23 |
| **Supersedes** | 1.x (`eclipse-player.md`, 208 lines) |
| **Audience** | An autonomous AI coding agent, plus human reviewers of its output |
| **Design references** | Winamp 2/5 (plugin SDK, two-tier skins, title formatting), AIMP 4/5/6 (DSP depth, tag tooling, LGPL dependency compliance, cross-platform parity docs), foobar2000 (title formatting, smart playlists), MilkDrop/projectM (visualizer preset ecosystem) |

---

## Table of Contents

| § | Section |
|---|---|
| 0 | [Document Control & How To Use This Prompt](#0--document-control--how-to-use-this-prompt) |
| 1 | [Role, Engineering Standard & Definition of Done](#1--role-engineering-standard--definition-of-done) |
| 2 | [Product Vision, Reference Analysis & Non-Goals](#2--product-vision-reference-analysis--non-goals) |
| 3 | [Support Matrix](#3--support-matrix) |
| 4 | [Licensing & Legal Compliance](#4--licensing--legal-compliance) |
| 5 | [Repository Layout](#5--repository-layout) |
| 6 | [Technology Stack & Architecture Decision Records](#6--technology-stack--architecture-decision-records) |
| 7 | [Architecture](#7--architecture) |
| 8 | [Audio Engine Specification](#8--audio-engine-specification) |
| 9 | [Library, Metadata & Playlist Specification](#9--library-metadata--playlist-specification) |
| 10 | [Eclipse Format Strings (Display-String Language)](#10--eclipse-format-strings-display-string-language) |
| 11 | [Theme & Skin Engine Specification](#11--theme--skin-engine-specification) |
| 12 | [UI/UX Specification](#12--uiux-specification) |
| 13 | [Keyboard Shortcuts & Global Hotkeys](#13--keyboard-shortcuts--global-hotkeys) |
| 14 | [Operating-System Integration](#14--operating-system-integration) |
| 15 | [Android Auto Specification](#15--android-auto-specification) |
| 16 | [Plugin & Extension SDK](#16--plugin--extension-sdk) |
| 17 | [Network Features](#17--network-features) |
| 18 | [Sync Module](#18--sync-module) |
| 19 | [Settings, Data & Privacy](#19--settings-data--privacy) |
| 20 | [Non-Functional Requirements & Performance Budgets](#20--non-functional-requirements--performance-budgets) |
| 21 | [Security & Threat Model](#21--security--threat-model) |
| 22 | [Error Handling, Logging & Observability](#22--error-handling-logging--observability) |
| 23 | [Testing Strategy](#23--testing-strategy) |
| 24 | [Build & Toolchain](#24--build--toolchain) |
| 25 | [CI/CD](#25--cicd) |
| 26 | [Versioning & Release Policy](#26--versioning--release-policy) |
| 27 | [Documentation Deliverables](#27--documentation-deliverables) |
| 28 | [Build Phases & Exit Gates](#28--build-phases--exit-gates) |
| 29 | [Appendices](#29--appendices) |

---

## 0 · Document Control & How To Use This Prompt

### 0.1 How to consume this document

This is a **specification**, not a suggestion list. When implementing:

1. **Never invent a requirement.** If something is not specified here and you need it, add it to `docs/OPEN-QUESTIONS.md` with a proposed answer and proceed with the proposal, clearly marked `ASSUMPTION:` in a code comment.
2. **Never silently downgrade a requirement.** If a requirement proves technically impossible, stop, document why in `docs/adr/`, and propose an alternative. Do not ship a weaker behaviour behind the same name.
3. **Work phase by phase** (§28). Do not start phase *N+1* until every exit gate of phase *N* is green in CI.
4. **Every requirement has an ID** (e.g. `REQ-AUD-014`). Reference the ID in commit messages, test names, and code comments so traceability is mechanical.
5. **Respect the MoSCoW tier.** Implement all `[v1.0]` items before any `[v1.x]` item. `[v2]` items must not influence v1.0 architecture beyond leaving a clean extension point.

### 0.2 Requirement ID scheme

`REQ-<AREA>-<NNN>` where `<AREA>` is one of:

| Code | Area | Code | Area |
|---|---|---|---|
| `GEN` | General / cross-cutting | `THM` | Theme & skin engine |
| `AUD` | Audio engine | `UIX` | UI/UX |
| `LIB` | Library & metadata | `KEY` | Shortcuts & hotkeys |
| `PLS` | Playlists & queue | `OSI` | OS integration |
| `EFS` | Format strings | `AUT` | Android Auto |
| `NET` | Network features | `PLG` | Plugin SDK |
| `SYN` | Sync | `SEC` | Security |
| `SET` | Settings & privacy | `NFR` | Non-functional |
| `TST` | Testing | `BLD` | Build & release |

### 0.3 MoSCoW tiers

| Tag | Meaning | Rule |
|---|---|---|
| `[v1.0]` | **MUST.** Ships in 1.0.0. | 1.0.0 cannot be tagged while any `[v1.0]` requirement is unmet. |
| `[v1.x]` | **SHOULD.** Ships in a 1.x minor. | Architecture must leave a seam for it; no implementation before 1.0.0 ships. |
| `[v2]` | **COULD.** Post-1.x. | Recorded only so v1 design does not preclude it. |
| `[NON-GOAL]` | **WON'T.** Explicitly out of scope. | Must be refused if requested; see §2.4. |

### 0.4 Glossary

| Term | Definition |
|---|---|
| **Sink** | A concrete audio-output backend implementing `IAudioSink` (WASAPI, ALSA, AAudio, …). |
| **Bit-perfect** | The exact integer sample values from the decoder reach the DAC: no resampling, no mixing, no volume scaling, no DSP, no dither. See §8.8 for the binding contract. |
| **Gapless** | Consecutive tracks join with zero inserted or dropped samples at the boundary, verified sample-exactly. See §8.4. |
| **Theme** | A safe, data-only token bundle (colours, type, spacing, motion). No layout, no code. §11.2. |
| **Skin** | A Theme *plus* an optional declarative layout document. Still no code. §11.4. |
| **EFS** | Eclipse Format Strings — the user-facing display-string language. §10. |
| **RT thread** | The real-time audio callback thread. Subject to the hard constraints in §8.2.3. |
| **DHU** | Desktop Head Unit, Google's Android Auto emulator. |
| **LUFS** | Loudness Units relative to Full Scale, per ITU-R BS.1770. |
| **Parity** | A feature behaving identically across platforms. Deviations must appear in §29.2. |

### 0.5 Normative language

**MUST** / **MUST NOT** / **REQUIRED** are absolute. **SHOULD** / **SHOULD NOT** are strong recommendations that require a written ADR to violate. **MAY** is optional.

---

## 1 · Role, Engineering Standard & Definition of Done

### 1.1 Role

You are a professional software engineer with 30+ years of shipped production experience spanning native mobile development, desktop systems programming, and audio engineering. You have written audio callbacks that must never miss a deadline, and you have maintained codebases for a decade after writing them. Apply that standard throughout.

### 1.2 Engineering standard

- **Layered architecture** with enforced dependency direction (§7.2). No shortcuts that create tech debt.
- **Defensive error handling** at every boundary where audio hardware, the filesystem, the network, or the OS is touched. No `catch(...)` {} swallowing. No ignored return values.
- **Exhaustive doc-comments** on every public API: purpose, parameters, ownership, thread-safety, failure modes, and real-time safety.
- **Semantic versioning from commit #1**; conventional commits from commit #1.
- **Maintainability and correctness over cleverness.** A junior maintainer must be able to follow the audio path.
- **Tests ship with the module, not after.** A module without tests is an incomplete module.
- **C++ side:** C++20, RAII everywhere, no raw owning pointers, no manual memory management — except inside pre-allocated audio buffers, where every such site MUST carry a comment explaining why and how it is bounded.
- **Kotlin side:** explicit API mode, no `!!`, no `GlobalScope`, structured concurrency only, `Result`/sealed-class error modelling over exceptions across module boundaries.
- **No dead code, no commented-out code, no `TODO` without a linked issue ID.**

### 1.3 Definition of Done (applies to every unit of work)

A change is done when **all** of the following hold:

1. It implements a stated requirement ID, or is refactoring with no behaviour change.
2. Unit tests cover the happy path, every documented failure mode, and every boundary condition.
3. `clang-format` / `ktlint` clean; `clang-tidy` / `detekt` produce no new findings.
4. Public APIs are documented; the doc-comment states thread-safety and RT-safety.
5. CI is green on **every** platform in the matrix, not just the author's.
6. Any user-visible string is externalised for translation (§12.7).
7. Any new dependency is recorded in §4.2 with its licence, and in the SBOM.
8. Any deviation from this document is recorded in `docs/adr/`.
9. Performance-sensitive paths (§20) have a benchmark, and it did not regress.
10. The commit message references the requirement ID.

---

## 2 · Product Vision, Reference Analysis & Non-Goals

### 2.1 Vision

**Eclipse Player** is a free, open-source, privacy-first music player family with two siblings:

- **Eclipse Player for Android** — native app with full Android Auto integration.
- **Eclipse Player for Desktop** — a single C++ codebase producing native, first-class builds for **Windows 10/11** and **Ubuntu 22.04 LTS / 24.04 LTS** (Debian-based distributions generally).

Both share one visual design language, one skin/theme package format, and one library data model, and can optionally sync over the local network. Each is a fully standalone, **offline-first** application. **No forced accounts. No ads. No telemetry. Every network feature is off until the user turns it on.**

### 2.2 Positioning

Eclipse Player targets the audience that Winamp and AIMP built and that modern streaming clients abandoned: people who **own** their music files, care about **audio fidelity**, and want to **make the player theirs**.

The three differentiators, in priority order:

1. **A real skin engine** — per-skin *layout* control, not palette swapping, and safe enough to install from the internet (§11).
2. **Audio-engineering credibility** — sample-exact gapless, genuine bit-perfect output, a specified DSP chain with published coefficients (§8).
3. **Privacy as an engineering property, not a marketing claim** — verifiable by reading the code and by running the app behind a firewall (§19.5).

### 2.3 Reference analysis — what we take, and what we reject

#### From Winamp

| Take | Why | Where |
|---|---|---|
| Plugin SDK with a stable ABI and clear plugin categories | 66 plugins appeared within 9 months of the 1998 SDK; it is the single reason Winamp stayed relevant for 25 years. A player without an extension story ages out. | §16 `[v1.x]` |
| Two-tier skin model (simple tokens vs. full layout) | Lets casual users recolour and lets authors rebuild the UI, without forcing every author into the hard tier. | §11.1 `[v1.0]` |
| Configurable title/display formatting | Power users demand control over what the list shows. | §10 `[v1.0]` |
| Separate, dockable windows + compact "windowshade" mode | Distinct, beloved interaction model no modern player offers. | §12.2 `[v1.x]` |
| Visualizer *preset ecosystem* (MilkDrop `.milk`) | Thousands of existing presets. Inheriting an ecosystem beats authoring four visualizers. | §12.4 `[v1.0]` via projectM |
| Single-instance + enqueue from command line / file-manager drop | Present in Winamp 0.20a in 1997; still the fastest way to play a file. | §14.1 `[v1.0]` |
| Strict Unicode correctness in filenames *and* tags | Winamp needed until 5.33 to get this right. We get it right on day one. | §9.2.4 `[v1.0]` |

| Reject | Why |
|---|---|
| Bitmap sprite-sheet skins | Fails on HiDPI and on arbitrary window sizes. We use vector + tokens. |
| Scripting language inside skins (Maki) | Arbitrary code execution from downloaded packages. See §11.1 and §21.3. |
| Rewriting the app on a new framework mid-life (Winamp3) | Winamp3 lost users to its own predecessor. We evolve one codebase. |
| Bundled adware / installer offers | Non-negotiable. §19.5. |
| Video playback, CD burning, portable-device sync | Scope sprawl that hollowed out Winamp's core. §2.4. |

#### From AIMP

| Take | Why | Where |
|---|---|---|
| **LGPL-only dependency discipline with a published source offer** | AIMP ships FFmpeg 7.1.1 and SoundTouch under LGPLv2.1 with public source links. That is the exact compliance pattern we adopt. | §4 `[v1.0]` |
| Documented **per-platform parity matrix** | AIMP 6 publishes a "differences between Windows and Linux" page. Cross-platform honesty prevents bug reports and builds trust. | §29.2 `[v1.0]` |
| Deep DSP: 18-band EQ, tempo/pitch/speed as *separate* controls, BPM detection | This is the audiophile credibility layer. | §8.9 |
| Extended fade rules (on play, pause, stop, seek, **and** track change) | Click-free transport is felt immediately, and almost nobody implements it fully. | §8.5 `[v1.0]` |
| Per-output-device EQ presets | Headphones and speakers need different curves. Obvious once seen. | §8.9.6 `[v1.x]` |
| Advanced/mass tag editor, custom tags, tag-driven file renaming | The library-curator workflow. | §9.2 |
| Bookmarks, A-B repeat, auto-resume, shutdown-on-complete | Small features with disproportionate loyalty effects. | §9.7, §12.2 |
| First-party **Skin Editor** and **Localization Editor** | An ecosystem needs tooling, not just a file format. | §11.7 `[v1.x]` |
| Addon taxonomy: skins, icon packs, plugins, **encoders**, wallpapers | Clear extension categories users already understand. | §16.2 |
| Cue-sheet support (external *and* embedded) | Required for the single-file-album collections that serious collectors keep. | §9.3 `[v1.0]` |
| Internet-radio recording, ICY metadata, cancellable connect | Radio treated as a real feature, not a URL box. | §17.1 `[v1.x]` |

| Reject | Why |
|---|---|
| Proprietary licence / closed source | Directly contrary to our vision. |
| BASS audio library | Not OSS-compatible. AIMP's original choice; we use FFmpeg + native sinks. |
| Windows-first with Linux arriving 18 years later | We are cross-platform from commit #1, with CI proving it. |

#### From foobar2000

| Take | Why | Where |
|---|---|---|
| Title-formatting language design (`[]` optional blocks, `$func()`) | The best-designed of the three; we adopt its semantics. | §10 `[v1.0]` |
| Rule-based smart playlists as a first-class query language | Winamp and foobar2000 have it; AIMP desktop does not. Clear win. | §9.6 `[v1.0]` |

### 2.4 Non-Goals

These are **refused**, not deferred. If asked to add one, cite this section.

| `[NON-GOAL]` | Rationale |
|---|---|
| Video playback | Doubles the decode/render surface for an audience that has a video player. Diluted Winamp. |
| CD ripping and CD burning | Optical drives are near-extinct; the code is high-maintenance and platform-specific. Audio-CD *playback* is `[v2]`. |
| Portable-media-device sync (iPod, MTP, PlaysForSure) | Enormous device-quirk matrix, vanishing user base. |
| Cloud music streaming / subscription services | Contradicts offline-first, requires accounts and DRM. |
| DRM of any kind | Would require a proprietary component and closed-source blobs. |
| NFT, crypto-wallet, or blockchain features | Actively user-hostile. Named explicitly because a reference player shipped this. |
| Bundled third-party offers, adware, or an "optimizer" upsell | Non-negotiable. |
| Mandatory account creation for any feature whatsoever | Sync uses device pairing, not accounts (§18). |
| Analytics or telemetry enabled by default | §19.5 forbids it structurally, not just by policy. |
| macOS and iOS builds | No CI hardware, no maintainer. Architecture MUST NOT preclude them; we simply do not claim them. |
| A web/Electron/React-Native shell | Fails the native-integration and memory requirements in §20. |

---

## 3 · Support Matrix

### 3.1 Desktop — operating systems and architectures

| OS | Versions | Architectures | Tier |
|---|---|---|---|
| Windows | 10 (1809+ / build 17763+), 11 | `x64`, `arm64` | **Tier 1** — CI-built, CI-tested, release artifacts |
| Ubuntu | 22.04 LTS, 24.04 LTS | `x86_64`, `arm64` | **Tier 1** — CI-built, CI-tested, release artifacts |
| Debian | 12+ | `x86_64`, `arm64` | **Tier 2** — `.deb` expected to work, not CI-tested |
| Other glibc Linux | glibc ≥ 2.35 | `x86_64` | **Tier 3** — AppImage only, best effort, no support promise |

`REQ-GEN-001` `[v1.0]` Windows `arm64` and Linux `arm64` MUST be produced by CI as release artifacts. Cross-compilation is acceptable; `arm64` need not be CI-*tested* in 1.0.0 (record this in §29.2).

`REQ-GEN-002` `[v1.0]` The Windows build MUST NOT require a Visual C++ Redistributable install: statically link the CRT (`/MT`) or ship the runtime DLLs in the install directory.

`REQ-GEN-003` `[v1.0]` The Linux build MUST NOT assume a desktop environment. It MUST run on GNOME, KDE Plasma, XFCE, and a bare window manager. Tray-icon absence MUST degrade gracefully (§14.2.4).

### 3.2 Desktop — toolchains

| Component | Minimum | Pinned in CI | Note |
|---|---|---|---|
| C++ standard | C++20 | C++20 | No C++23 features; GCC 12 support is incomplete. |
| MSVC | VS 2022 17.10 (`19.40`) | latest 17.x | |
| GCC | 12 | 12 on 22.04, 13 on 24.04 | Ubuntu 22.04 defaults to GCC 11, which has incomplete C++20. CI MUST install and select `g++-12`. |
| Clang | 16 | 18 | Used for `clang-tidy`, `clang-format`, and sanitizer builds. |
| CMake | 3.28 | 3.28+ | Needed for `CMakePresets` v6 and `FILE_SET HEADERS`. |
| Ninja | 1.11 | latest | Default generator on all platforms. |

### 3.3 Android

| Property | Value |
|---|---|
| `minSdk` | **26** (Android 8.0) |
| `targetSdk` / `compileSdk` | Latest stable at release time |
| ABIs | `arm64-v8a`, `armeabi-v7a`, `x86_64` |
| Language | Kotlin, explicit API mode |
| JDK | 21 |
| Form factors | Phone, tablet, foldable, **Android Auto** |

`REQ-GEN-004` `[v1.0]` **`minSdk 26` rationale** (this MUST appear in `docs/ARCHITECTURE.md`): API 26 is the floor at which notification channels (`NotificationChannel`) and the modern audio-focus API (`AudioFocusRequest`) exist, and at which foreground-service behaviour is consistent. Media3 itself supports API 21; we choose 26 to avoid two divergent notification and audio-focus code paths. *(The v1.x claim that API 26 was "required for reliable MediaSession/Auto behaviour" was inaccurate and MUST NOT be repeated.)*

`REQ-GEN-005` `[v1.0]` The following permission behaviour is REQUIRED and MUST be implemented, not discovered late:

| API level | Permission / requirement | Handling |
|---|---|---|
| ≤ 32 | `READ_EXTERNAL_STORAGE` | Runtime request with rationale UI. |
| ≥ 33 | `READ_MEDIA_AUDIO` | Replaces the above. Both paths MUST exist. |
| ≥ 33 | `POST_NOTIFICATIONS` | Requested at first playback, never at cold start. Denial MUST NOT break playback. |
| ≥ 34 | `FOREGROUND_SERVICE_MEDIA_PLAYBACK` | Declared in the manifest; service type `mediaPlayback`. |
| ≥ 29 | Scoped storage | All file access via `MediaStore` or SAF (`ACTION_OPEN_DOCUMENT_TREE`) with persisted URI permissions. |
| any | `MANAGE_EXTERNAL_STORAGE` | **MUST NOT be requested.** It is a Play Store policy risk and unnecessary given SAF. |

`REQ-GEN-006` `[v1.0]` User-selected folders MUST be persisted as SAF tree URIs with `takePersistableUriPermission`, and the app MUST recover gracefully when a persisted URI becomes invalid (SD card removed, folder deleted) by marking the source offline rather than deleting library rows.

---

## 4 · Licensing & Legal Compliance

> This section is normative and blocking. A release that violates it cannot be published. Every claim here MUST be re-verified by CI (§25.6), not trusted to memory.

### 4.1 Project licence decision

`REQ-GEN-010` `[v1.0]` **Eclipse Player's own source code is licensed `MPL-2.0`** (Mozilla Public License 2.0).

Rationale, to be recorded in `docs/adr/0001-project-license.md`:

- **File-level copyleft** keeps improvements to Eclipse's own files open, which protects the project, without infecting the plugin ABI.
- **Compatible with LGPL dependencies** without requiring the whole work to be GPL.
- **Permits closed-source plugins** to link against the plugin SDK (§16), which is the ecosystem outcome Winamp proved matters.
- **Does not force GPL on downstream distributions**, so distro packagers and Qt commercial-licence holders are both unblocked.
- Consequence accepted: we **cannot** use GPL-only components. This is a deliberate constraint that shapes §4.2.

`REQ-GEN-011` `[v1.0]` The plugin SDK headers (`desktop/include/eclipse/plugin/**`) MUST additionally be dual-licensed under `Apache-2.0 OR MPL-2.0`, so plugin authors of any licence can include them without ambiguity.

### 4.2 Dependency licence register

`REQ-GEN-012` `[v1.0]` `docs/THIRD-PARTY.md` MUST contain this table, kept accurate, and CI MUST fail if a dependency appears in the build that is absent from it.

#### Desktop

| Dependency | Version | Licence | Linkage | Obligation |
|---|---|---|---|---|
| Qt 6 | 6.8 LTS | LGPL-3.0-only | **Dynamic, always** | Provide relinking ability + licence text + source offer. Static linking is FORBIDDEN (§4.3). |
| FFmpeg (`libavformat`, `libavcodec`, `libavutil`, `libswresample`) | 7.1.x | **LGPL-2.1-or-later** | Dynamic | Built **without** `--enable-gpl` and **without** `--enable-nonfree` (§4.4). Publish source offer. |
| TagLib | 2.x | LGPL-2.1-or-later **OR** MPL-1.1 | Dynamic | Source offer if the LGPL arm is chosen. |
| SoundTouch | 2.3.x | LGPL-2.1-or-later | Dynamic | Source offer. |
| libsamplerate | **≥ 0.2.2** | BSD-2-Clause | Static OK | Attribution. **Versions < 0.1.9 were GPL — MUST NOT be used.** |
| Chromaprint | 1.5.x | LGPL-2.1-or-later | Dynamic | Source offer. |
| projectM | 4.x | LGPL-2.1-or-later | Dynamic | Source offer. |
| SQLite | 3.4x | Public domain | Static OK | None. |
| `nlohmann/json` | 3.11.x | MIT | Static OK | Attribution. |
| `libzip` or `minizip-ng` | current | BSD-3-Clause / zlib | Static OK | Attribution. |
| zlib | 1.3.x | zlib | Static OK | Attribution. |
| GoogleTest | 1.15+ | BSD-3-Clause | Test-only | Not shipped. |
| RtAudio *(optional fallback sink only)* | 6.x | MIT-like | Dynamic | Attribution. See §8.7.7. |

#### Android

| Dependency | Licence | Note |
|---|---|---|
| AndroidX Media3 (ExoPlayer) | Apache-2.0 | Clean. |
| AndroidX (Compose, Room, WorkManager, DataStore, Lifecycle) | Apache-2.0 | Clean. |
| Hilt / Dagger | Apache-2.0 | Clean. |
| Kotlin stdlib / coroutines | Apache-2.0 | Clean. |
| projectM (Android) | LGPL-2.1-or-later | Dynamic `.so`, source offer. |
| Chromaprint (NDK) | LGPL-2.1-or-later | Dynamic `.so`, source offer. |

### 4.3 Qt LGPL-3.0 compliance rules

`REQ-GEN-013` `[v1.0]` All of the following are REQUIRED:

1. Qt libraries MUST be **dynamically linked**. Static Qt builds are forbidden in all shipped artifacts.
2. The user MUST be able to replace a Qt shared library with a compatible build and have Eclipse Player still run. No hard-coded checksums or signature checks over Qt binaries.
3. The full LGPL-3.0 text MUST ship in the installed tree at `licenses/LGPL-3.0.txt` and be reachable from **Help → Licences** in the UI.
4. `docs/THIRD-PARTY.md` MUST state the exact Qt version and configure flags used, and link to the corresponding source archive.
5. **No anti-tivoization conflict:** Eclipse Player MUST NOT be shipped in a form that prevents the user from installing a modified Qt.

### 4.4 FFmpeg build constraints

`REQ-GEN-014` `[v1.0]` The FFmpeg build used for shipped artifacts MUST be configured with **all** of:

```
--disable-gpl            # implied by omitting --enable-gpl; assert explicitly
--disable-nonfree
--disable-programs       # no ffmpeg/ffplay/ffprobe binaries shipped
--disable-doc
--disable-encoders       # v1.0 decodes only; see REQ-GEN-016
--disable-muxers
--disable-filters
--disable-devices
--disable-network        # Eclipse does its own HTTP; see REQ-NET-002
--enable-shared
--disable-static
```

`REQ-GEN-015` `[v1.0]` CI MUST assert at build time that the linked FFmpeg reports neither GPL nor non-free: verify `avutil_license()` returns a string containing `LGPL` and **not** `GPL version` / `nonfree`. Fail the build otherwise. This is a mechanical guard, not a review step.

`REQ-GEN-016` `[v1.x]` When the audio converter (§9.10) lands, encoders are enabled **selectively** and only LGPL-clean ones: FFmpeg's native `flac`, `alac`, `opus` (via libopus, BSD-3), `vorbis` (via libvorbis, BSD-3), `wavpack`, `pcm_*`. **`libfdk_aac` MUST NOT be used** (non-free). **`libmp3lame` is LGPL-2.1 and is permitted**, but MUST remain an optional, separately-enabled component so the default build stays minimal.

### 4.5 Codec patent notes

`REQ-GEN-017` `[v1.0]` `docs/THIRD-PARTY.md` MUST record:

- **MP3**: all known essential patents expired by April 2017; decoding and encoding are unencumbered.
- **AAC**: decode via FFmpeg's native LGPL decoder. Patent pools exist for AAC; because we distribute source and unmodified LGPL binaries and charge nothing, we take the same position as every Linux distribution. Encoding to AAC is **not** offered (see `REQ-GEN-016`).
- **Monkey's Audio (APE)**: use FFmpeg's native LGPL `ape` decoder. The upstream Monkey's Audio SDK's bespoke licence MUST NOT be vendored.
- **DSD**: decoded to PCM by FFmpeg's LGPL `dsd_*` decoders. No proprietary component.

### 4.6 Steinberg ASIO

`REQ-GEN-018` `[v1.x]` ASIO support (§8.7.5) MUST be:

- Behind the CMake option `ECLIPSE_ENABLE_ASIO`, **default `OFF`**.
- Built only when the user supplies the Steinberg ASIO SDK themselves via `-DASIO_SDK_DIR=...`.
- **Never vendored**, never redistributed, never present in an official release artifact.
- Documented in `docs/BUILDING.md` as a self-service, licence-accepting step.

### 4.7 Attribution & source-offer obligations

`REQ-GEN-019` `[v1.0]` The shipped application MUST include a **Help → Third-Party Licences** screen that lists, for every bundled component: name, version, licence identifier (SPDX), full licence text, and a URL to the exact corresponding source. This screen MUST be generated from `docs/THIRD-PARTY.md` at build time — never hand-maintained in two places.

`REQ-GEN-020` `[v1.0]` The project MUST publish a **written source offer** page (`docs/LGPL-SOURCE-OFFER.md`, mirrored on the website) linking the precise source archive for every LGPL component in every release, keyed by release tag. This mirrors AIMP's published practice and is the cheapest way to be unambiguously compliant.

`REQ-GEN-021` `[v1.0]` Every release MUST include a **CycloneDX SBOM** (§25.6) enumerating all components with SPDX licence identifiers.

### 4.8 Trademark and asset hygiene

`REQ-GEN-022` `[v1.0]` No Winamp, AIMP, foobar2000, or other third-party trademark, logo, skin, icon, or sound asset may be copied into this repository. Reference players are studied for **design and behaviour**, never for assets. Built-in skins MUST be original work. Any skin that evokes a physical product MUST not use that product's trademarks.

`REQ-GEN-023` `[v1.0]` Bundled fonts MUST be OFL-1.1, Apache-2.0, or public domain, and their licence files MUST ship alongside them.

---

## 5 · Repository Layout

Platforms are fully separated. `android/` and `desktop/` MUST NOT import each other's code. Anything genuinely shared lives in `shared-spec/` as **data and specification files only** — never as compiled code.

`REQ-GEN-030` `[v1.0]` The repository MUST match this layout. Every file listed without a `[v1.x]`/`[v2]` marker is required by 1.0.0.

```
eclipse-player/
├── README.md                       # what it is, screenshots, install, build, licence
├── LICENSE                         # MPL-2.0
├── CHANGELOG.md                    # generated from conventional commits
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
├── SECURITY.md                     # disclosure policy, supported versions
├── .editorconfig
├── .gitignore
├── .gitattributes                  # enforce LF, mark binary assets
├── .clang-format                   # desktop C++ style, checked in CI
├── .clang-tidy                     # desktop C++ lint rules
├── .markdownlint.json
├── commitlint.config.js            # conventional-commit enforcement
│
├── android/                        # Native Android app — self-contained Gradle build
│   ├── .editorconfig               # ktlint rules — named `.editorconfig` so ktlint discovers it
│   ├── settings.gradle.kts
│   ├── build.gradle.kts
│   ├── gradle.properties
│   ├── gradle/libs.versions.toml   # single version catalog — no inline versions anywhere
│   ├── config/
│   │   └── detekt.yml
│   ├── app/                        # thin shell: DI wiring, navigation, manifest
│   ├── core-common/                # Result types, dispatchers, logging facade, test utils
│   ├── core-model/                 # pure domain entities — no Android imports
│   ├── core-player/                # Media3 wrapper, DSP AudioProcessors, queue engine
│   ├── core-library/               # scanner, TagLib/Media3 metadata, Room DB, search
│   ├── core-theme/                 # theme + skin engine (consumes shared-spec schema)
│   ├── core-datastore/             # settings via DataStore, export/import
│   ├── feature-library/            # browse UI
│   ├── feature-player/             # Now Playing UI, visualizer, lyrics
│   ├── feature-settings/
│   ├── feature-skins/              # skin browser/installer
│   ├── auto/                       # MediaLibraryService + Android Auto browse tree
│   └── benchmark/                  # macrobenchmark: startup, scroll jank, scan
│
├── desktop/                        # Cross-platform C++ app — Windows + Ubuntu, ONE codebase
│   ├── CMakeLists.txt
│   ├── CMakePresets.json           # configure/build/test presets per platform
│   ├── vcpkg.json                  # manifest, with builtin-baseline pinned
│   ├── vcpkg-configuration.json
│   ├── qt-version.txt              # exact Qt version consumed by aqtinstall in CI
│   ├── include/eclipse/
│   │   ├── plugin/                 # PUBLIC, ABI-STABLE plugin SDK headers (§16)
│   │   └── version.hpp.in          # generated version header
│   ├── src/
│   │   ├── main.cpp                # entry: single-instance check, CLI parse, bootstrap
│   │   ├── app/                    # application object, lifecycle, DI container, CLI
│   │   ├── audio/
│   │   │   ├── decode/             # IDecoder port + FFmpeg implementation
│   │   │   ├── sink/               # IAudioSink port + WASAPI/ALSA/PipeWire/Pulse/JACK/ASIO
│   │   │   ├── dsp/                # EQ, ReplayGain, limiter, tempo/pitch, biquad kernels
│   │   │   ├── graph/              # engine, ring buffers, RT thread, gapless scheduler
│   │   │   └── analysis/           # ReplayGain scanner, BPM, fingerprint, spectrum taps
│   │   ├── library/                # scanner, watcher, SQLite index, TagLib layer, cue
│   │   ├── core/                   # playlists, queue, EFS engine, settings, smart-playlist
│   │   ├── theme/                  # theme/skin loader, validator, layout DSL interpreter
│   │   ├── net/                    # HTTP client, radio, podcast, MusicBrainz, scrobble
│   │   ├── sync/                   # LAN discovery, pairing, sync engine
│   │   ├── plugin/                 # host side of the plugin SDK: loader, registry, sandbox
│   │   └── native/                 # tray, hotkeys, SMTC, MPRIS2, file assoc, IPC
│   ├── qml/                        # Qt Quick UI — skin-driven surfaces
│   ├── ui/                         # Qt Widgets shell: window chrome, dialogs, prefs
│   ├── resources/
│   │   ├── skins/                  # bundled skin packages
│   │   ├── icons/                  # bundled icon sets (SVG)
│   │   ├── i18n/                   # .ts sources and compiled .qm
│   │   └── eclipse.qrc
│   ├── tests/
│   │   ├── unit/                   # GoogleTest — pure logic
│   │   ├── integration/            # decode correctness, gapless, DB migrations
│   │   ├── ui/                     # QTest — widgets + QML
│   │   ├── fuzz/                   # libFuzzer targets (§21.6)
│   │   ├── bench/                  # performance benchmarks (§20)
│   │   └── data/                   # golden-file corpus manifest (§29.4)
│   ├── cmake/                      # find-modules, toolchains, warning flags, sanitizers
│   └── packaging/
│       ├── windows/                # WiX .msi, NSIS .exe, windeployqt, file associations
│       └── linux/                  # CPack .deb control, AppImage recipe, .desktop, MIME XML
│
├── shared-spec/                    # Platform-agnostic specs consumed by both codebases
│   ├── README.md                   # versioning policy for everything in here
│   ├── schemas/
│   │   ├── theme-schema.json       # v1 — single source of truth for themes (§11.2)
│   │   ├── skin-manifest.schema.json
│   │   ├── layout.schema.json      # declarative layout DSL (§11.4)
│   │   ├── settings.schema.json    # settings export/import (§19.4)
│   │   └── smart-playlist.schema.json
│   ├── design-system/
│   │   ├── tokens.json             # canonical token values (§12.1)
│   │   ├── typography.md
│   │   ├── motion.md
│   │   └── iconography.md
│   ├── grammars/
│   │   ├── eclipse-format-strings.ebnf   # §10
│   │   └── smart-playlist.ebnf           # §9.6
│   ├── sync-protocol.md            # §18
│   └── conformance/                # cross-platform golden fixtures both apps test against
│       ├── efs-cases.json          # EFS input → expected output
│       ├── smart-playlist-cases.json
│       └── theme-validation-cases/
│
├── tools/
│   ├── skin-editor/                # [v1.x] first-party skin authoring tool
│   ├── theme-validate/             # CLI: validate a theme/skin against the schema
│   ├── gen-third-party/            # generates THIRD-PARTY.md + in-app licence screen
│   └── corpus-fetch/               # fetches/generates the audio test corpus (§29.4)
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── API.md
│   ├── AUDIO-ENGINE.md
│   ├── SKIN-AUTHORING.md
│   ├── PLUGIN-AUTHORING.md         # [v1.x]
│   ├── BUILDING.md
│   ├── TESTING.md
│   ├── THIRD-PARTY.md              # generated
│   ├── LGPL-SOURCE-OFFER.md
│   ├── PRIVACY.md
│   ├── PARITY.md                   # generated from §29.2
│   ├── ROADMAP.md
│   ├── OPEN-QUESTIONS.md
│   └── adr/                        # 0001-project-license.md, 0002-audio-output.md, ...
│
└── .github/
    ├── ISSUE_TEMPLATE/
    ├── PULL_REQUEST_TEMPLATE.md
    ├── dependabot.yml
    └── workflows/
        ├── android-ci.yml
        ├── desktop-ci.yml
        ├── spec-ci.yml             # validates shared-spec schemas + conformance fixtures
        ├── security.yml            # CodeQL, fuzz smoke, SBOM diff, licence audit
        └── release.yml
```

`REQ-GEN-031` `[v1.0]` `shared-spec/conformance/` is load-bearing: **both** the desktop and Android implementations MUST run the same fixture files through their own EFS engine, smart-playlist evaluator, and theme validator, and MUST produce identical results. This is how "shared format" is proven rather than asserted.

---

## 6 · Technology Stack & Architecture Decision Records

Each subsection is a condensed ADR. The full versions live in `docs/adr/`.

### 6.1 Desktop UI — Qt 6.8 LTS (Widgets + Qt Quick)

**Decision.** Qt Widgets for the main window chrome, menus, preferences, and dialogs; **Qt Quick/QML for all skin-driven surfaces** (Now Playing, library grid, mini-player, visualizer host).

**Why.** Qt is the only mature, natively-compiled, richly skinnable cross-platform C++ UI framework with first-class Windows *and* Linux packaging, an accessibility bridge to both UIA and AT-SPI2, a translation pipeline, and a GPU-accelerated declarative scene graph. The Widgets/Quick split is deliberate: chrome and dialogs benefit from native-feeling widgets and are not skinned; the skinned surfaces need the scene graph and property bindings.

**Rejected.** GTK4 (weak Windows story); wxWidgets (no declarative layer, dated); Dear ImGui (not accessible, not native-feeling); Electron/Tauri/React-Native (fails §20 memory and startup budgets, and the native-integration requirements in §14); Avalonia/.NET (extra runtime, weaker low-latency audio interop); Slint (immature ecosystem, licence friction).

**Pinned.** Qt **6.8 LTS**, installed via `aqtinstall`, modules: `qtbase qtdeclarative qtsvg qttools qtshadertools`, plus `qtwayland` on Linux. `qtmultimedia` is **NOT** used — we own the audio path.

### 6.2 Qt acquisition — `aqtinstall`, not vcpkg

**Decision.** Qt comes from official prebuilt binaries via `aqtinstall`, pinned in `desktop/qt-version.txt`. **All other** C/C++ dependencies come from vcpkg in manifest mode with a pinned `builtin-baseline`.

**Why.** Building Qt from source through vcpkg costs hours per platform per cache miss and is a well-known CI fragility. Ubuntu 22.04's system Qt is 6.2.4, below what our QML surfaces need. `aqtinstall` gives a reproducible, pinned, minutes-long install of the exact LTS we target, and keeps us dynamically linked as §4.3 requires.

**Consequence.** `desktop/vcpkg.json` MUST NOT list Qt. `docs/BUILDING.md` MUST document both steps.

**Rejected.** vcpkg Qt port (build time); system Qt (version too old on 22.04, unpinnable); Conan (second package manager for no additional benefit).

### 6.3 Audio decode — FFmpeg 7.1 (LGPL configuration)

**Decision.** FFmpeg `libavformat` + `libavcodec` + `libavutil` + `libswresample`, LGPL-only build per §4.4, behind our own `IDecoder` port (§8.3).

**Why.** Broadest practical format coverage from one dependency, actively maintained, and — per §4 — legally clean when built correctly. AIMP ships FFmpeg 7.1.1 under LGPLv2.1 in a comparable freeware player, which is direct precedent that the configuration is workable.

**Rejected.** GStreamer (its `gst-plugins-ugly` set is GPL, so a permissive core would need constant vigilance; plus heavy runtime and plugin-registry complexity); libVLC (large, oriented to video, LGPL-2.1+ but far more surface than needed); BASS (proprietary, incompatible with §4.1 — this is precisely where we diverge from AIMP's original choice); per-format libraries (`libFLAC` + `libmpg123` + `libopus` + …) — considered, and kept as the **fallback plan** if FFmpeg's LGPL discipline ever becomes untenable, since the `IDecoder` port makes the swap local.

**Note.** FFmpeg is used for **decode only** in v1.0. Its network, filter, muxer, and encoder subsystems are disabled (§4.4).

### 6.4 Audio output — `IAudioSink` port with native backends

**Decision.** Define our own `IAudioSink` abstraction (§8.7) and implement it with **native, per-platform backends**:

| Platform | Backend | Modes | Tier |
|---|---|---|---|
| Windows | **WASAPI** via `IAudioClient`/`IAudioClient3` | shared **and** `AUDCLNT_SHAREMODE_EXCLUSIVE` | `[v1.0]` |
| Linux | **ALSA** (`libasound`) | `plughw:` (shared) and `hw:` (direct/exclusive) | `[v1.0]` |
| Linux | **PulseAudio** (`libpulse` simple + async) | shared | `[v1.0]` |
| Linux | **PipeWire** (native `libpipewire`) | shared, low-latency | `[v1.x]` |
| Linux | **JACK** | shared, pro-audio routing | `[v1.x]` |
| Windows | **ASIO** | exclusive | `[v1.x]`, opt-in build (§4.6) |
| Android | **AAudio** (API 26+) via Media3's `DefaultAudioSink`, `AudioTrack` fallback | shared, offload | `[v1.0]` |

**Why this replaces the v1.x plan.** The previous revision specified RtAudio and claimed *"bit-perfect / exclusive output … WASAPI exclusive on Windows, ALSA/PipeWire direct on Ubuntu, both via RtAudio's low-level device access."* **That is not achievable.** Verified against RtAudio's official README and API notes:

1. RtAudio's Linux backends are **OSS, ALSA, JACK, and PulseAudio**. **There is no PipeWire backend.**
2. RtAudio's public API exposes **no share-mode selection**, so `AUDCLNT_SHAREMODE_EXCLUSIVE` is unreachable.
3. RtAudio explicitly states that *"all necessary data format conversions, channel compensation, de-interleaving, and byte-swapping is handled by internal RtAudio routines"* — internal conversion is fundamentally incompatible with a bit-perfect guarantee.
4. RtAudio's ASIO support requires Steinberg SDK sources that we cannot redistribute (§4.6).

Owning the sink layer also gives us the device-period, clock, and buffer control that sample-exact gapless (§8.4) and the DSP chain (§8.9) depend on.

**Rejected.** RtAudio as the primary sink (the four reasons above); `miniaudio` and PortAudio (both *do* expose WASAPI exclusive mode and are strong options — rejected as primaries because we still need per-backend control for bit-perfect verification, but either is an acceptable substitute for the Tier-2 backends if maintenance cost bites; record such a change as an ADR); Qt Multimedia (no exclusive mode, no low-latency guarantees).

**Retained.** RtAudio MAY be compiled in as an **optional last-resort fallback sink** for exotic Linux setups, clearly labelled as non-bit-perfect in the UI (§8.7.7).

### 6.5 Resampling, time-stretch, analysis

| Concern | Library | Licence | Note |
|---|---|---|---|
| Sample-rate conversion | **libsamplerate ≥ 0.2.2** | BSD-2 | Quality tiers mapped in §8.6. Must be ≥ 0.1.9; earlier releases were GPL. |
| Pitch-preserving tempo/speed | **SoundTouch 2.3.x** | LGPL-2.1 | Dynamic link. Also supplies BPM detection, matching AIMP's approach. |
| Audio fingerprint | **Chromaprint 1.5.x** | LGPL-2.1 | Local compare offline; AcoustID network lookup is opt-in (§9.9). |
| Loudness / ReplayGain 2.0 | Own implementation of ITU-R BS.1770-4 | MPL-2.0 | ~400 lines, fully testable, avoids a dependency. §8.9.4. |

### 6.6 Metadata — TagLib 2.x

**Decision.** TagLib 2.x for reading and writing tags across all supported containers.

**Why.** The de-facto standard C++ tag library; correct handling of ID3v2.3/2.4 text encodings, Vorbis comments, APEv2, MP4 atoms, and WMA/ASF; battle-tested by Amarok, JuK, and many others. Writing tags with a bespoke parser is a data-loss risk we refuse to take.

**Constraint.** TagLib is the **only** writer of tags. FFmpeg MAY expose metadata for read-only display of formats TagLib does not cover, but MUST NOT be used to write.

### 6.7 Visualization — projectM 4.x

**Decision.** Embed **projectM** (LGPL-2.1, OpenGL/C++) as the advanced visualizer engine, giving MilkDrop `.milk` preset compatibility, alongside three lightweight native visualizers.

**Why.** MilkDrop and its predecessor Geiss were the two most-downloaded Winamp plugins ever (≈4.7M and ≈2.7M downloads). projectM is its maintained, cross-platform, LGPL reimplementation and is already shipped inside Qmmp, Clementine, and Poweramp on Android — so it is proven on both of our target platforms. Inheriting a two-decade preset ecosystem is strictly better than hand-writing four visualizers.

**Rejected.** Writing a bespoke visualizer framework only (no ecosystem); Butterchurn (MIT, but WebGL/JS — wrong runtime for us).

### 6.8 Local index — SQLite, mirrored by Room

**Decision.** SQLite on desktop, Room on Android, over **one schema defined in §9.4**. Same table names, same column names, same types, same semantics, same migration numbers.

**Why.** Makes the sync module (§18) a diff over a shared model rather than a translation layer, and lets `shared-spec/conformance/` fixtures validate both.

**Constraint.** `REQ-LIB-001` `[v1.0]` Any schema change MUST be applied to **both** platforms in the same commit, with matching migration numbers, or CI fails.

### 6.9 Android stack

| Concern | Choice | Note |
|---|---|---|
| UI | Jetpack Compose, Material 3 as a baseline that the theme engine fully overrides | No XML layouts except the widget/notification surfaces that require them. |
| Playback | **Media3** (`ExoPlayer` + `MediaLibraryService`) | §8.10 covers where Media3's defaults are insufficient. |
| DSP | Custom `AudioProcessor` chain sharing the *same* biquad coefficients as desktop | **Not** `android.media.audiofx.Equalizer` — see §8.9.7. |
| DB | Room + FTS4/FTS5 | Schema per §9.4. |
| Background scan | WorkManager, expedited where appropriate | §9.1. |
| Settings | DataStore (Proto) | Export/import per §19.4. |
| DI | Hilt | |
| Async | Coroutines + Flow, structured concurrency only | |

### 6.10 Rationale summary for `docs/ARCHITECTURE.md`

`REQ-GEN-040` `[v1.0]` `docs/ARCHITECTURE.md` MUST state, in prose a new contributor can follow: *Qt + FFmpeg + our own native sink layer is the combination that gives full control of the audio buffer for gapless and DSP work, first-class native OS integration (SMTC, MPRIS2, tray, hotkeys), and a QML-based skin engine with genuine per-skin layout control rather than palette swapping. The sink layer is ours rather than a third party's specifically because bit-perfect output and sample-exact gapless require share-mode, device-period, and clock control that general-purpose wrappers do not expose.*

---

## 7 · Architecture

### 7.1 Layer model

Both platforms implement the same five layers. Names differ per language; responsibilities do not.

```
┌──────────────────────────────────────────────────────────────────────┐
│ 5  PRESENTATION    QML surfaces + Widgets shell  |  Compose UI       │
│                    Skin-driven. Zero business logic. Zero file I/O.  │
├──────────────────────────────────────────────────────────────────────┤
│ 4  APPLICATION     Use cases, view-models, command dispatch,          │
│                    playback session, queue orchestration              │
├──────────────────────────────────────────────────────────────────────┤
│ 3  DOMAIN          Pure entities + rules: Track, Album, Playlist,     │
│                    SmartRule, EFS AST, ThemeTokens, DspGraphSpec.     │
│                    No framework imports. Fully unit-testable.        │
├──────────────────────────────────────────────────────────────────────┤
│ 2  PORTS           Interfaces only: IDecoder, IAudioSink, ITagReader, │
│                    ITagWriter, ILibraryIndex, IHttpClient, IClock,    │
│                    IFileSystem, IMediaSession, IPluginHost            │
├──────────────────────────────────────────────────────────────────────┤
│ 1  ADAPTERS        FFmpeg, WASAPI/ALSA/AAudio, TagLib, SQLite/Room,   │
│                    Qt, Media3, SMTC/MPRIS, projectM, Chromaprint      │
└──────────────────────────────────────────────────────────────────────┘
```

### 7.2 Dependency rules (mechanically enforced)

`REQ-GEN-050` `[v1.0]` The following MUST hold, and MUST be enforced by a CI check that inspects includes/imports — not by reviewer discipline:

1. Layer *N* MAY depend on layers *< N*. It MUST NOT depend on layers *> N*.
2. **Layer 3 (Domain) MUST NOT import** Qt, FFmpeg, SQLite, Android, Compose, or any adapter. Domain code compiles into a library that links against nothing but the C++ standard library (desktop) or Kotlin stdlib (Android).
3. Layer 1 adapters MUST be reachable only through layer 2 ports. No call site outside `src/audio/sink/` may include a WASAPI or ALSA header; no call site outside `src/library/` may include a TagLib header.
4. `android/**` MUST NOT reference `desktop/**` and vice versa. `shared-spec/**` MUST contain no compiled code.
5. Feature modules on Android MUST NOT depend on each other, only on `core-*`.

`REQ-GEN-051` `[v1.0]` Implement the enforcement as: a CMake-time include-path partition plus a `tools/` script that greps for forbidden includes per directory, wired into `desktop-ci.yml`; and a Gradle module-dependency assertion test on Android.

### 7.3 Threading model overview

Detailed in §8.2. Summary of the process-wide thread inventory:

| Thread | Owner | Priority | May block? | May allocate? |
|---|---|---|---|---|
| UI / main | Qt / Android main looper | normal | no (never >16 ms) | yes |
| **Audio RT callback** | The active `IAudioSink` | time-critical / RT | **NO** | **NO** |
| Decoder (×2: current + prefetch) | Audio engine | above normal | yes | yes, off the hot path |
| Analysis worker pool | Library / analysis | below normal | yes | yes |
| Scanner pool (N = min(4, cores)) | Library | low / background | yes | yes |
| Network | `net/` | normal | yes | yes |
| DB writer (serialised) | Library index | normal | yes | yes |

`REQ-GEN-052` `[v1.0]` There MUST be exactly one writer thread for the library database. All writes are funnelled through it as serialised commands. This eliminates SQLite write contention as a class of bug.

### 7.4 Data flow — playback

```
User intent ──▶ PlaybackSession (L4)
                     │  resolves MediaSource (file | cue slice | stream)
                     ▼
                IDecoder (L2 → FFmpeg L1)
                     │  packets → float32 planar frames
                     ▼
                DecodeBuffer  ── lock-free SPSC ring, pre-allocated ──┐
                                                                      │
                     ┌────────────────────────────────────────────────┘
                     ▼
                RT callback (L1 sink) ──▶ GaplessScheduler ──▶ DspChain ──▶ Sink
                     │                         │                  │
                     │                         │                  ├─▶ spectrum tap ─▶ (lock-free) ─▶ visualizer
                     │                         │                  └─▶ true-peak limiter
                     │                         └─ track boundary events ─▶ (lock-free) ─▶ L4
                     └─ underrun / device-loss events ────────────▶ (lock-free) ─▶ L4
```

`REQ-GEN-053` `[v1.0]` Every arrow crossing into or out of the RT callback MUST be a lock-free, wait-free, pre-allocated channel. No mutex, no allocation, no logging call, and no syscall may appear on that path. Violations are correctness bugs, not performance nits.

### 7.5 Data flow — library scan

```
Scan request (manual | watcher | WorkManager)
      ▼
Enumerate roots ──▶ filter by extension + size ──▶ stat() batch
      ▼
Compare (path, mtime, size) with DB ──▶ classify: NEW | CHANGED | UNCHANGED | GONE
      ▼                                              │
   UNCHANGED ──▶ skip (no tag read at all)           │
      ▼                                              ▼
NEW/CHANGED ──▶ tag read (TagLib) ──▶ art extract ──▶ normalise (artist/album keys)
      ▼
Batched upsert commands ──▶ DB writer thread ──▶ transaction per 500 rows
      ▼
FTS index update ──▶ progress events (throttled to 10 Hz) ──▶ UI
      ▼
GONE ──▶ mark missing (never hard-delete; see REQ-LIB-030)
```

### 7.6 Module boundary contract

`REQ-GEN-054` `[v1.0]` Every module MUST expose exactly one public header (desktop) or one public API file (Android) that defines its surface. Everything else is internal. `docs/API.md` MUST document every public surface, and MUST be verified by a CI check that every public symbol has a doc-comment.

---

## 8 · Audio Engine Specification

> This is the heart of the product and the section with the least tolerance for ambiguity. Everything here is testable, and §8.11 says how.

### 8.1 Signal chain and internal format

`REQ-AUD-001` `[v1.0]` The internal processing format is **32-bit IEEE-754 float, planar (de-interleaved), non-normalised**, with nominal full scale at ±1.0.

`REQ-AUD-002` `[v1.0]` Values **above** ±1.0 MUST be permitted throughout the DSP chain. Intermediate stages MUST NOT clamp. Only the final output stage limits (`REQ-AUD-060`). Clamping mid-chain destroys the headroom that ReplayGain boost and EQ gain legitimately need.

`REQ-AUD-003` `[v1.0]` Planar float is chosen because every per-channel filter (biquad EQ, crossfeed, balance) operates on contiguous channel data. Interleaving happens exactly once, at the sink boundary.

**Canonical signal chain (v1.0), in this order:**

```
 1. Decode              → planar float32 @ source rate/channels
 2. Gapless splice      → sample-exact concatenation across track boundaries (§8.4)
 3. ReplayGain          → single scalar gain, per-track or per-album (§8.9.4)
 4. Channel matrix      → balance, mono downmix, channel remap (§8.9.5)
 5. Equalizer           → cascaded biquads, per channel (§8.9.1)
 6. Effects chain       → bass boost, stereo widener, reverb — each bypassable (§8.9.3)
 7. Tempo / pitch       → SoundTouch, bypassed entirely at 1.0×/0 cents (§8.9.2)
 8. Resample            → libsamplerate, only if source rate ≠ device rate (§8.6)
 9. Fade / crossfade    → transport and boundary envelopes (§8.5)
10. Volume              → user volume, applied AFTER fades so fades are volume-independent
11. True-peak limiter   → transparent unless engaged (§8.9.8)
12. Dither + convert    → to device format, dither only when reducing to fixed point (§8.6.4)
13. Interleave → Sink
```

`REQ-AUD-004` `[v1.0]` The order above is normative. Notably: **ReplayGain precedes EQ** (so EQ operates on loudness-normalised material), **volume follows fades** (so a fade sounds identical at any volume), and **the limiter is last before format conversion** (so nothing downstream can reintroduce clipping).

`REQ-AUD-005` `[v1.0]` Every stage MUST be individually bypassable, and bypass MUST be a *true* bypass — the stage is skipped, not run with neutral parameters. Running a biquad at 0 dB still costs cycles and still perturbs the last bits. Assert this in tests.

`REQ-AUD-006` `[v1.0]` In **bit-perfect mode** (§8.8), stages 3–12 are all bypassed and stage 1 output is converted to the device format with no arithmetic other than the integer format change.

### 8.2 Thread model and real-time safety

#### 8.2.1 Topology

```
  Decoder thread A (current track) ──┐
                                     ├──▶ 2 × lock-free SPSC ring ──▶ RT callback ──▶ Sink
  Decoder thread B (next track)   ──┘                                    │
                                                                          ├──▶ event ring ──▶ UI
                                                                          └──▶ spectrum ring ──▶ visualizer
```

`REQ-AUD-010` `[v1.0]` There MUST be exactly **two** decoder threads: one for the currently playing source, one prefetching the next. This is what makes gapless and crossfade possible without allocation on the boundary.

`REQ-AUD-011` `[v1.0]` Each decoder thread owns a **single-producer / single-consumer lock-free ring buffer** of pre-allocated planar float frames. Implementation MUST use `std::atomic` with explicit `memory_order_acquire`/`memory_order_release` on the read and write indices. `memory_order_seq_cst` is permitted only where profiling shows it is free.

`REQ-AUD-012` `[v1.0]` Ring capacity MUST be **≥ 4 × the device period** and **≥ 250 ms** of audio, whichever is larger, and MUST be allocated once when the stream opens. Resizing a ring while streaming is FORBIDDEN; a format change closes and reopens the stream (§8.10.3).

#### 8.2.2 Prefetch policy

`REQ-AUD-013` `[v1.0]` The next track's decoder MUST begin filling its ring when the current track has **≤ max(5 s, crossfade duration + 2 s)** remaining, so that a gapless splice or a crossfade never waits on I/O or on codec initialisation.

`REQ-AUD-014` `[v1.0]` If prefetch has not produced at least one full device period by the boundary, the engine MUST insert silence rather than block the RT thread, MUST emit `AudioEvent::PrefetchUnderrun`, and MUST log it once per occurrence (from the UI thread, never the RT thread).

#### 8.2.3 Real-time thread constraints — absolute

`REQ-AUD-015` `[v1.0]` Inside the audio callback the following are **FORBIDDEN**, without exception:

| Forbidden | Why |
|---|---|
| `malloc`, `free`, `new`, `delete`, any container that may allocate | Unbounded latency, may take a global lock |
| `std::mutex`, `std::condition_variable`, any blocking primitive | Priority inversion → dropout |
| File I/O, socket I/O, any syscall that may block | Unbounded latency |
| Logging of any kind | Allocates, locks, does I/O |
| C++ exceptions (`throw`) | Unbounded unwinding, may allocate |
| `std::shared_ptr` copy on the hot path | Atomic refcount contention |
| Anything time-unbounded (`std::regex`, sorting unbounded input) | Deadline miss |
| Reading a `std::string` owned by another thread | Data race |

`REQ-AUD-016` `[v1.0]` Parameter changes (volume, EQ gains, bypass flags, ReplayGain) MUST reach the RT thread through a **lock-free parameter snapshot**: the UI thread writes a fully-populated, pre-allocated parameter block and publishes it with a single `release` store of an index; the RT thread reads it with a single `acquire` load. Never mutate individual parameters that the RT thread is reading.

`REQ-AUD-017` `[v1.0]` Every function callable from the RT thread MUST carry `/// RT-SAFE:` in its doc-comment, stating why. A function without that annotation MUST NOT be called from the callback. Add a CI grep that flags calls from `audio/graph/rt_*` into functions lacking the annotation.

`REQ-AUD-018` `[v1.0]` The RT callback MUST be exercised under **ThreadSanitizer** in CI with a mock sink driving it at realistic rates, concurrently with parameter changes, seeks, and track transitions (§23.5).

#### 8.2.4 Latency budget

`REQ-AUD-019` `[v1.0]` Default and permitted buffer sizes:

| Mode | Default period | User-selectable range | Note |
|---|---|---|---|
| Shared (WASAPI / PulseAudio / ALSA `plughw`) | **20 ms** | 5–200 ms | Safe default across consumer hardware. |
| Exclusive (WASAPI excl. / ALSA `hw:`) | Device default period | Device-reported min–max | Never override below what the device reports. |
| Android (AAudio) | Device-preferred burst | multiples of the burst | Use `AAudioStreamBuilder_setPerformanceMode(LOW_LATENCY)` only when DSP is bypassed. |

`REQ-AUD-020` `[v1.0]` The **total** engine latency (decode ring + DSP + sink) MUST be reported in the UI to the millisecond, and MUST be within 15 % of measured wall-clock latency verified by the loopback test in §8.11.5.

`REQ-AUD-021` `[v1.0]` The RT callback's worst-case execution time MUST stay **below 50 % of the period** with the full DSP chain engaged (18-band EQ + all effects) at 192 kHz / 2 ch on the reference hardware in §20.1. CI MUST benchmark this and fail on regression >10 %.

### 8.3 Decoder port

`REQ-AUD-025` `[v1.0]` `IDecoder` (layer 2) is the only route to decoded audio. Required surface:

| Member | Contract |
|---|---|
| `open(MediaSource) → Result<StreamInfo>` | Probes and initialises. MUST NOT block >2 s for a local file; MUST honour a cancellation token for network sources. |
| `StreamInfo` | sample rate, channel count, channel layout, source bit depth, container format, codec name, total frames (or `unknown`), seekability, `GaplessInfo` (§8.4), `ReplayGainTags`, `is_lossless`. |
| `read(FrameBuffer&) → Result<size_t>` | Fills with planar float32. Returns 0 only at true end of stream. |
| `seek(frame_index) → Result<frame_index>` | Returns the **actual** landed frame. Callers MUST handle inexact seeks by discarding the difference (§8.3.3). |
| `close()` | Idempotent. |

`REQ-AUD-026` `[v1.0]` **Format detection MUST be content-based, not extension-based.** Probe with `av_probe_input_format3`. A `.mp3` that is actually FLAC MUST play. A file whose extension is unknown but whose content is decodable MUST play when opened explicitly (though the scanner MAY filter by extension for speed — §9.1.3).

`REQ-AUD-027` `[v1.0]` Decoder errors MUST be classified, never generic:

| Class | Meaning | UI behaviour |
|---|---|---|
| `FileNotFound` / `PermissionDenied` | Path issue | Mark track missing, skip, do not stop playback |
| `UnsupportedFormat` | No decoder | Mark unplayable with the detected codec name, skip |
| `CorruptStream` | Decode failed mid-file | Attempt resync up to 3× / 1 s of audio, then skip and mark |
| `NetworkTimeout` / `NetworkRefused` | Stream issue | Retry with backoff per §17.1.4 |
| `DeviceFormatUnsupported` | Sink rejected the format | Renegotiate per §8.10.3 |

`REQ-AUD-028` `[v1.0]` **Seek accuracy.** For formats with sample-accurate seek (FLAC with seektable, WAV, WavPack, uncompressed), a seek MUST land on the exact requested frame. For formats without (MP3 without an index, VBR without a TOC), the decoder MUST seek to the nearest preceding frame and **discard the difference** so the caller observes an exact landing. The engine MUST NOT expose seek inaccuracy to the user or to the position display.

#### 8.3.1 Format support matrix — v1.0

`REQ-AUD-029` `[v1.0]` These MUST decode correctly, verified against the golden corpus (§29.4):

| Format | Container(s) | Tier | Gapless method (§8.4) |
|---|---|---|---|
| MP3 (MPEG-1/2/2.5 Layer III) | raw, ID3-wrapped | `[v1.0]` | Xing/LAME tag |
| FLAC | native, Ogg | `[v1.0]` | Native (exact frame count) |
| WAV / RF64 | RIFF | `[v1.0]` | Native |
| AIFF / AIFF-C | IFF | `[v1.0]` | Native |
| AAC (LC, HE-AAC v1/v2) | MP4/M4A, ADTS | `[v1.0]` | `iTunSMPB` / `esds` priming |
| ALAC | MP4/M4A, CAF | `[v1.0]` | Native |
| Ogg Vorbis | Ogg | `[v1.0]` | Granule position |
| Opus | Ogg, MP4 | `[v1.0]` | `OpusHead` pre-skip |
| WavPack | `.wv` | `[v1.0]` | Native |
| Monkey's Audio (APE) | `.ape` | `[v1.0]` | Native |
| Musepack | `.mpc` | `[v1.0]` | Native |
| WMA (v1/v2/Pro) | ASF | `[v1.0]` | Best-effort |
| WMA Lossless | ASF | `[v1.x]` | Native |
| AC-3 / E-AC-3 | raw, MP4 | `[v1.x]` | Best-effort |
| TAK, TTA, Shorten | native | `[v1.x]` | Native |
| DSD → PCM | DSF, DFF | `[v1.x]` | Native (decode only — see `REQ-AUD-030`) |
| Audio CD (CD-DA) | — | `[v2]` | — |
| Tracker modules (MOD/XM/S3M/IT) | — | `[v2]` | — |
| MIDI / KAR | — | `[v2]` | Requires a synth; out of v1 scope |

`REQ-AUD-030` `[v1.0]` **DSD honesty.** The v1.x document said *"DSD where the decode library permits"*, conflating two different features. They MUST be separated:

- **DSD decode-to-PCM** `[v1.x]`: FFmpeg's `dsd_*` decoders convert DSD to PCM. Straightforward. The UI MUST state that the output is PCM.
- **DoP / native DSD passthrough** `[v2]`: requires exclusive-mode output, DoP marker encoding, and DAC-specific handling. It MUST NOT be claimed, advertised, or implied before it exists.

### 8.4 Sample-exact gapless playback

> This is the single most-faked feature in music players. "Gapless" here means a byte-comparable result, and §8.11.3 proves it.

`REQ-AUD-035` `[v1.0]` **Definition.** For two tracks that were encoded from one continuous source, the concatenated decoded output of Eclipse Player MUST be **sample-identical** to the decoded output of the original continuous source, with **zero** inserted silence and **zero** dropped samples at the boundary.

`REQ-AUD-036` `[v1.0]` `GaplessInfo` MUST be extracted at `open()` time:

```
struct GaplessInfo {
    uint32_t skip_start_frames;   // encoder delay / priming to discard at the head
    uint32_t skip_end_frames;     // padding to discard at the tail
    uint64_t valid_frames;        // exact playable frame count, or UNKNOWN
    Source   source;              // Native | XingLame | ITunSMPB | OpusHead | Granule | None
};
```

#### 8.4.1 MP3 — Xing/Info + LAME tag

`REQ-AUD-037` `[v1.0]` Parse the first frame for a `Xing` or `Info` header. If a LAME extension is present (`LAME` magic at the documented offset within that frame), read the 12-bit **encoder delay** and 12-bit **encoder padding** fields. Then:

```
skip_start_frames = encoder_delay + DECODER_DELAY
skip_end_frames   = max(0, encoder_padding - DECODER_DELAY)
where DECODER_DELAY = 529   // MPEG-1 Layer III polyphase/MDCT filterbank delay, in samples
```

`REQ-AUD-038` `[v1.0]` If no LAME tag exists, fall back to `skip_start_frames = DECODER_DELAY`, `skip_end_frames = 0`, and set `source = None`. The UI MUST be able to report *why* a given pair of tracks is not gapless — "source file lacks a LAME gapless tag" is a legitimate, honest answer and MUST be surfaced in the track's technical-info panel.

`REQ-AUD-039` `[v1.0]` Validate the LAME tag's CRC where present. A tag failing CRC MUST be ignored, not trusted.

#### 8.4.2 AAC / ALAC in MP4 — `iTunSMPB`

`REQ-AUD-040` `[v1.0]` Read the ID3v2 / MP4 `----:com.apple.iTunes:iTunSMPB` free-form tag. Its value is a space-separated sequence of hexadecimal fields; the engine MUST use:

- field 2 → **priming / encoder delay** frames → `skip_start_frames`
- field 3 → **remainder / padding** frames → `skip_end_frames`
- field 4 → **original sample count** (64-bit) → `valid_frames`

`REQ-AUD-041` `[v1.0]` If `iTunSMPB` is absent, fall back to the `esds`/decoder-reported priming (commonly 1024 or 2048 frames for AAC-LC) and set `source = None`.

`REQ-AUD-042` `[v1.0]` The parser MUST be defensive: wrong field count, non-hex characters, or values exceeding the file's frame count MUST cause the tag to be rejected, never trusted into a negative or overflowing skip. This parser is a fuzz target (§21.6).

#### 8.4.3 Opus

`REQ-AUD-043` `[v1.0]` Read `pre_skip` from the `OpusHead` identification packet and discard exactly that many frames **at 48 kHz** from the head, converting if the output rate differs. Also read `output_gain` and apply it as specified by RFC 7845 (independently of, and in addition to, ReplayGain — see `REQ-AUD-056`).

#### 8.4.4 FLAC, WavPack, APE, WAV, ALAC

`REQ-AUD-044` `[v1.0]` These carry exact frame counts natively. `skip_start = skip_end = 0`; `valid_frames` comes from `STREAMINFO`/header. For FLAC, prefer the seektable for seeking; if absent, fall back to binary search over frame headers, and record in the technical-info panel that the file lacks a seektable.

#### 8.4.5 Ogg Vorbis

`REQ-AUD-045` `[v1.0]` Use the granule position of the final page to determine exact length and truncate the final block accordingly. A negative initial granule position (encoder-trimmed start) MUST be honoured.

#### 8.4.6 The splice itself

`REQ-AUD-046` `[v1.0]` The `GaplessScheduler` runs **inside** the RT callback and MUST:

1. Consume from ring A until `valid_frames - skip_end_frames` is reached.
2. Switch to ring B **within the same callback invocation**, mid-buffer, filling the remainder of the output block from ring B starting after `skip_start_frames`.
3. Emit a `TrackChanged` event through the event ring — never call into layer 4 directly.
4. Do this with **no allocation, no lock, and no branch on file I/O**.

`REQ-AUD-047` `[v1.0]` **Crossfade and gapless are mutually exclusive per boundary.** Precedence, highest first:

| Situation | Behaviour |
|---|---|
| User pressed *next* manually | Crossfade (if enabled), regardless of gapless metadata |
| Both tracks belong to the same album **and** both carry valid gapless metadata | **Gapless splice**, crossfade suppressed |
| Crossfade enabled, gapless metadata absent or albums differ | Crossfade |
| Crossfade disabled | Gapless splice if metadata allows, else hard cut with the 8.5 boundary fade |

`REQ-AUD-048` `[v1.0]` A user-facing setting **"Always gapless within an album"** MUST exist, default **on**, implementing the second row above. This is the behaviour listeners actually want and almost no player gets right by default.

`REQ-AUD-049` `[v1.0]` Format changes across a boundary (44.1 kHz → 96 kHz, stereo → mono) make a sample-exact splice impossible. In that case the engine MUST perform a **short crossfade of 40 ms** to mask the device reconfiguration, and MUST emit `AudioEvent::BoundaryFormatChange` so the technical-info panel can explain it.

### 8.5 Fades and crossfade

`REQ-AUD-050` `[v1.0]` The following fades MUST all exist and be independently configurable, each with its own duration. This matches AIMP's extended fade settings and is what makes transport feel click-free:

| Event | Default | Range | Curve |
|---|---|---|---|
| Fade in on **play** | 150 ms | 0–2000 ms | equal-power |
| Fade out on **pause** | 150 ms | 0–2000 ms | equal-power |
| Fade out on **stop** | 300 ms | 0–3000 ms | equal-power |
| Fade around **seek** (out then in) | 60 ms each | 0–500 ms | linear |
| Fade on **manual next/previous** | 200 ms | 0–3000 ms | equal-power |
| **Crossfade** on automatic track change | 0 ms (off) | 0–15000 ms | equal-power |
| Fade out on **sleep-timer expiry** | 20 s | 0–120 s | linear |

`REQ-AUD-051` `[v1.0]` **Equal-power crossfade** MUST be used, not linear, so perceived loudness stays constant through the transition:

```
gain_out(t) = cos(t · π/2)
gain_in (t) = sin(t · π/2)      for t ∈ [0,1]
```

Linear fades sum to a mid-transition dip of about −3 dB and sound like a hole. Only the seek and sleep-timer fades use a linear ramp, where the dip is inaudible or desired.

`REQ-AUD-052` `[v1.0]` Fades MUST be applied at stage 9 — **after** all DSP and **before** volume — so a fade sounds identical at any user volume and is unaffected by EQ settings.

`REQ-AUD-053` `[v1.0]` Pausing MUST fade out, **then** stop pulling from the ring, and MUST NOT discard buffered audio. Resuming MUST fade in from the exact same sample position. A pause/resume cycle MUST be sample-lossless — assert this in a test.

`REQ-AUD-054` `[v1.0]` A crossfade MUST NOT begin if the outgoing track has less remaining audio than the crossfade duration; in that case shorten the crossfade to the available audio rather than truncating the track.

`REQ-AUD-055` `[v1.x]` **Silence removal at boundaries.** Optionally trim leading and trailing samples below a threshold (default −60 dBFS, configurable −80…−40 dBFS) before splicing, with a hard cap of 5 s trimmed per end. MUST be off by default and MUST be automatically disabled when gapless metadata is present, since trimming would break a sample-exact splice.

### 8.6 Resampling

`REQ-AUD-056` `[v1.0]` Resampling MUST occur **only** when the source rate differs from the negotiated device rate. When they match, the resampler MUST be bypassed entirely (`REQ-AUD-005`).

`REQ-AUD-057` `[v1.0]` Quality tiers, mapped to libsamplerate converters and exposed with plain-language labels:

| UI label | libsamplerate converter | Use |
|---|---|---|
| **Best** | `SRC_SINC_BEST_QUALITY` | Default on desktop |
| **Balanced** | `SRC_SINC_MEDIUM_QUALITY` | Default on Android |
| **Fast** | `SRC_SINC_FASTEST` | Low-power / battery saver |
| **Minimal (linear)** | `SRC_LINEAR` | Diagnostics only; MUST be labelled as low quality in the UI |

`REQ-AUD-058` `[v1.0]` **Device-rate policy**, user-selectable:

| Policy | Behaviour | Default |
|---|---|---|
| **Follow source** | Reconfigure the device to the source rate on every track; resample only when the device refuses. | Default when the device supports it (best fidelity) |
| **Fixed rate** | Pin the device to a chosen rate; resample everything to it. | Fallback |
| **Follow source family** | Switch only between the 44.1 kHz family (44.1/88.2/176.4) and the 48 kHz family (48/96/192), resampling within a family. | Recommended compromise; avoids frequent device restarts |

`REQ-AUD-059` `[v1.0]` **Dither.** When converting float32 to a fixed-point device format of **16 bits or fewer**, apply TPDF dither at 1 LSB amplitude. For 24-bit and 32-bit output, dither MUST NOT be applied (it is inaudible and it breaks bit-perfect verification). Dither MUST be bypassed in bit-perfect mode. Noise-shaped dither is `[v1.x]`.

### 8.7 Output sinks

`REQ-AUD-060` `[v1.0]` `IAudioSink` surface:

| Member | Contract |
|---|---|
| `enumerate() → vector<DeviceInfo>` | Never blocks >500 ms. `DeviceInfo` carries id, human name, backend, max channels, supported rates, supported formats, min/default/max period, `supports_exclusive`. |
| `open(DeviceId, StreamFormat, Mode) → Result<NegotiatedFormat>` | `Mode` ∈ {`Shared`, `Exclusive`}. Returns what was actually granted, which MAY differ from what was requested. |
| `start()` / `stop()` / `close()` | `stop()` MUST drain or flush per an explicit flag; both behaviours are needed. |
| `set_callback(fn)` | The RT entry point. |
| `latency_frames() → uint32` | Reported end-to-end, for §8.11.5 verification. |
| `on_device_lost(fn)` | Fired from a non-RT thread. Drives §8.10.2. |

`REQ-AUD-061` `[v1.0]` Sink selection MUST be user-overridable and MUST persist per device. The default order is defined per platform below.

#### 8.7.1 Windows — WASAPI

`REQ-AUD-062` `[v1.0]` Implement directly against `IAudioClient` / `IAudioClient3` / `IAudioRenderClient` via C++/WinRT or raw COM.

- **Shared mode** (default): `AUDCLNT_SHAREMODE_SHARED` with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`. Use `IAudioClient3::InitializeSharedAudioStream` where available for a lower period.
- **Exclusive mode**: `AUDCLNT_SHAREMODE_EXCLUSIVE` + `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`, with the device period from `GetDevicePeriod`. On `AUDCLNT_E_UNSUPPORTED_FORMAT`, probe candidate formats in a defined order (source-exact → same rate/higher depth → next higher rate in the same family) and report exactly what was granted.
- On `AUDCLNT_E_DEVICE_IN_USE` when requesting exclusive, surface a clear, actionable message naming exclusive mode as the cause, and offer to fall back to shared.

`REQ-AUD-063` `[v1.0]` Use `IMMNotificationClient` to detect default-device changes, device arrival/removal, and property changes, and drive §8.10.2 from it.

`REQ-AUD-064` `[v1.0]` Raise the RT thread priority with `AvSetMmThreadCharacteristics(L"Audio")`. Do **not** use `SetThreadPriority(THREAD_PRIORITY_TIME_CRITICAL)` alone — MMCSS is the correct mechanism and avoids starving the system.

#### 8.7.2 Linux — ALSA

`REQ-AUD-065` `[v1.0]` Implement against `libasound` with both:

- `plughw:` / `default` — shared, with ALSA's own conversion. Convenient, not bit-perfect.
- **`hw:` — direct**, no plug layer, no conversion. This is the bit-perfect path. The engine MUST match the device's native format exactly or fail the open and report why.

`REQ-AUD-066` `[v1.0]` Use `snd_pcm_set_params` for the simple path but the full `snd_pcm_hw_params` API for `hw:` so period and buffer sizes are explicit. Prefer event-driven operation via `snd_pcm_poll_descriptors`; recover from `-EPIPE` (underrun) with `snd_pcm_prepare` and count the event.

`REQ-AUD-067` `[v1.0]` Because PipeWire and PulseAudio both provide ALSA compatibility devices, the ALSA backend MUST report whether the opened device is a real hardware node or a compatibility shim, and the UI MUST NOT advertise bit-perfect for a shim.

#### 8.7.3 Linux — PulseAudio

`REQ-AUD-068` `[v1.0]` Implement against `libpulse` (async API, not simple) for the mainstream desktop path. Shared mode only; MUST be labelled as non-bit-perfect. Handle server disconnect and default-sink changes.

#### 8.7.4 Linux — PipeWire

`REQ-AUD-069` `[v1.x]` Native `libpipewire` backend using `pw_stream` with `PW_KEY_NODE_LATENCY`. This is the correct modern low-latency path on Ubuntu 24.04+.

**Correction of record:** the v1.x document claimed PipeWire support "via RtAudio". **RtAudio has no PipeWire backend** — its Linux backends are OSS, ALSA, JACK, and PulseAudio. Until `REQ-AUD-069` lands, PipeWire is reached through its ALSA or PulseAudio compatibility layers, and this MUST be stated in §29.2 rather than glossed over.

#### 8.7.5 Optional pro-audio backends

`REQ-AUD-070` `[v1.x]` **JACK** on Linux: `jack_client_open`, transport-aware, exposes Eclipse as a JACK client with named ports.

`REQ-AUD-071` `[v1.x]` **ASIO** on Windows: behind `ECLIPSE_ENABLE_ASIO=OFF` by default, SDK user-supplied, never redistributed (§4.6).

#### 8.7.6 Android

`REQ-AUD-072` `[v1.0]` Use Media3's `DefaultAudioSink`, which targets `AudioTrack`/AAudio. Additionally:

- Enable **audio offload** (`setEnableAudioOffload`) when the DSP chain is fully bypassed, for battery. It MUST be disabled the instant any DSP stage engages, since offload bypasses our processing.
- Honour `AudioManager.getProperty(PROPERTY_OUTPUT_FRAMES_PER_BUFFER)` for burst sizing.
- Implement full audio-focus handling per `REQ-OSI-030`.

#### 8.7.7 RtAudio fallback

`REQ-AUD-073` `[v1.x]` RtAudio MAY be compiled in as a last-resort Linux fallback behind `ECLIPSE_ENABLE_RTAUDIO` (default `OFF`). When active, the UI MUST show a persistent "compatibility output — bit-perfect unavailable" indicator. It MUST NOT be the default on any platform, and MUST NOT be presented as capable of exclusive-mode output.

### 8.8 Bit-perfect mode — binding contract

`REQ-AUD-075` `[v1.0]` When bit-perfect mode is engaged and the indicator is lit, **all** of the following MUST hold. If any cannot, the mode MUST refuse to engage and MUST state which condition failed.

| # | Guarantee |
|---|---|
| 1 | The sink is in exclusive/direct mode (WASAPI exclusive, ALSA `hw:`, or ASIO). |
| 2 | The device rate equals the source rate exactly. No resampling. |
| 3 | The device channel count equals the source channel count. No mixing or upmixing. |
| 4 | The device bit depth is ≥ the source bit depth. |
| 5 | Volume is fixed at unity and the software volume control is **disabled in the UI**, not merely set to 100 %. |
| 6 | ReplayGain, EQ, all effects, tempo/pitch, balance, and the limiter are bypassed (true bypass). |
| 7 | No dither is applied. |
| 8 | No fade is applied, including transport fades. Crossfade is unavailable. |
| 9 | The only arithmetic between decoder output and the device is the integer format conversion, which MUST be exact and MUST be covered by a unit test asserting bit equality. |

`REQ-AUD-076` `[v1.0]` Bit-perfect mode MUST be **verified, not asserted**: the loopback test in §8.11.5 MUST demonstrate byte-identical round-trip for at least one device configuration in CI or on documented reference hardware. If it cannot be verified in CI, the limitation MUST be recorded in §29.2 and the manual test MUST be in the release checklist.

`REQ-AUD-077` `[v1.0]` The UI MUST display a bit-perfect indicator with three states — **active**, **available but not active**, **unavailable (reason)** — and the reason MUST be the specific failed condition from `REQ-AUD-075`, not a generic message.

### 8.9 DSP chain

#### 8.9.1 Equalizer

`REQ-AUD-080` `[v1.0]` Two modes MUST be provided:

**Graphic, 10-band** (ISO octave centres, Hz):
```
31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
```

**Graphic, 18-band** (half-octave spacing, ratio √2, Hz):
```
31.5, 44.5, 63, 89, 125, 177, 250, 354, 500,
707, 1000, 1414, 2000, 2828, 4000, 5657, 8000, 11314
```

`REQ-AUD-081` `[v1.0]` Gain range **±12.0 dB**, step **0.1 dB**, per band. A separate **pre-amp** of **−12.0…+12.0 dB** MUST exist for headroom management.

`REQ-AUD-082` `[v1.0]` Filter topology is a **cascade of RBJ peaking-EQ biquads**, one per band, per channel, in Direct Form I with float64 state (float32 state accumulates audible error at low frequencies). Coefficients:

```
A     = 10^(gain_dB / 40)
w0    = 2π · f0 / Fs
alpha = sin(w0) / (2Q)

b0 =  1 + alpha·A
b1 = -2·cos(w0)
b2 =  1 - alpha·A
a0 =  1 + alpha/A
a1 = -2·cos(w0)
a2 =  1 - alpha/A

// normalise by a0 before use
```

`REQ-AUD-083` `[v1.0]` **Q** is derived from band spacing, not hard-coded:

```
Q = sqrt(2^N) / (2^N − 1)     where N = bandwidth in octaves
  → N = 1.0  (10-band):  Q ≈ 1.4142
  → N = 0.5  (18-band):  Q ≈ 2.8710
```

`REQ-AUD-084` `[v1.0]` Bands whose centre frequency exceeds `Fs/2 · 0.95` MUST be bypassed, not clamped, to avoid coefficient instability near Nyquist. At 44.1 kHz the 16 kHz band is valid; at 22.05 kHz it is not.

`REQ-AUD-085` `[v1.0]` Gain changes MUST be **smoothed** to prevent zipper noise: recompute coefficients off the RT thread, publish per `REQ-AUD-016`, and cross-ramp between old and new coefficient sets over **32 ms**. Never swap coefficients instantaneously.

`REQ-AUD-086` `[v1.0]` A **parametric mode** MUST be provided: up to 10 user-defined bands, each with independently settable type (`peaking`, `lowshelf`, `highshelf`, `lowpass`, `highpass`, `notch`, `bandpass`), frequency (20 Hz–20 kHz), gain (±24 dB), and Q (0.1–18.0). Use the standard RBJ formulas for each type.

`REQ-AUD-087` `[v1.0]` Built-in presets MUST include at minimum: Flat, Rock, Pop, Jazz, Classical, Dance, Hip-Hop, Metal, Acoustic, Vocal Boost, Bass Boost, Treble Boost, Loudness, Small Speakers, Headphones. Users MUST be able to save, rename, delete, export, and import presets as JSON.

`REQ-AUD-088` `[v1.0]` The EQ curve MUST be displayed as a **computed frequency-response graph** of the actual cascaded transfer function, not as a cosmetic spline through the slider tops. Compute `|H(e^{jω})|` for the cascade.

#### 8.9.2 Tempo, pitch, and speed — three separate controls

`REQ-AUD-090` `[v1.0]` Following AIMP, these MUST be independent, not conflated:

| Control | Range | Default | Effect |
|---|---|---|---|
| **Tempo** | 25 %…400 % | 100 % | Duration changes, pitch preserved |
| **Pitch** | −12…+12 semitones, 1-cent resolution | 0 | Pitch changes, duration preserved |
| **Speed (rate)** | 25 %…400 % | 100 % | Both change together, like varispeed |

`REQ-AUD-091` `[v1.0]` Implement with SoundTouch. The whole stage MUST be **truly bypassed** when tempo = 100 %, pitch = 0, and speed = 100 %, because SoundTouch is not bit-transparent even at unity.

`REQ-AUD-092` `[v1.0]` Changing tempo or speed alters the relationship between decoded frames and wall-clock time. The position display, remaining-time display, sleep timer, crossfade scheduling, and scrobble threshold MUST all account for it. This is a common source of bugs; it MUST have a dedicated test.

`REQ-AUD-093` `[v1.x]` **BPM detection** via SoundTouch's BPM algorithm, run in the analysis worker pool, persisted to `tracks.bpm`, usable in smart-playlist rules (§9.6).

#### 8.9.3 Effects chain

`REQ-AUD-095` `[v1.0]` Each effect is individually bypassable and ordered as listed. All parameters persist per profile.

| Effect | Implementation | Parameters |
|---|---|---|
| **Bass boost** | Low-shelf biquad, not a gain stage | frequency 40–200 Hz (default 80), gain 0…+12 dB, Q 0.5–1.5 |
| **Treble boost** | High-shelf biquad | frequency 2–12 kHz (default 6 k), gain 0…+12 dB, Q 0.5–1.5 |
| **Stereo widener** | Mid/side with side gain | width 0…200 % (100 % = neutral); MUST preserve mono compatibility at ≤100 % |
| **Crossfeed** `[v1.x]` | Bauer-style, for headphones | level, cutoff |
| **Reverb** `[v1.x]` | Freeverb-derived | room size, damping, wet/dry, pre-delay |
| **Mono downmix** | Channel matrix | on/off, with −3 dB or −6 dB summing law |
| **Balance** | Per-channel gain | −100…+100, constant-power law |

`REQ-AUD-096` `[v1.0]` **DSP profiles.** The whole chain (EQ mode, gains, effects, parameters) MUST be storable as a named profile, switchable in one action, and assignable per output device (`REQ-AUD-105`).

#### 8.9.4 ReplayGain / loudness normalisation

`REQ-AUD-100` `[v1.0]` Implement **ReplayGain 2.0**, i.e. loudness measured per **ITU-R BS.1770-4** with a reference of **−18 LUFS**.

`REQ-AUD-101` `[v1.0]` Read these tags, in this precedence order:

| Priority | Tag | Format |
|---|---|---|
| 1 | `R128_TRACK_GAIN` / `R128_ALBUM_GAIN` | Opus. Q7.8 fixed point, relative to −23 LUFS. MUST be converted to our −18 LUFS reference. |
| 2 | `REPLAYGAIN_TRACK_GAIN` / `REPLAYGAIN_ALBUM_GAIN` + matching `_PEAK` | Vorbis comments, APEv2, ID3v2 `TXXX` |
| 3 | `----:com.apple.iTunes:replaygain_track_gain` | MP4 free-form |
| 4 | None | Fall back per `REQ-AUD-103` |

`REQ-AUD-102` `[v1.0]` Modes: **Off**, **Track**, **Album**, **Smart** (album gain when the queue is playing a contiguous album, track gain otherwise). Default **Smart**. Plus a **pre-amp** (−15…+15 dB, default 0) and a **fallback gain** for untagged tracks (−15…+15 dB, default 0).

`REQ-AUD-103` `[v1.0]` **Clipping prevention** MUST be a first-class option, default on: when the tagged peak multiplied by the applied gain would exceed 1.0, reduce the gain so the result peaks at exactly 1.0 (a "peak-limited ReplayGain"), and prefer this over engaging the limiter. If no peak tag exists, rely on the limiter (§8.9.8).

`REQ-AUD-104` `[v1.0]` **ReplayGain scanner** — Eclipse MUST *compute* loudness, not merely read it. Requirements: scan selected tracks or albums in the analysis pool; compute track and album gain and true peak; write standard tags via TagLib (or store in the DB if the user declines tag writing); show progress; be cancellable; be resumable. Reading without scanning leaves most libraries unnormalised, so a reader-only implementation does not satisfy this requirement.

#### 8.9.5 Channel matrix

`REQ-AUD-105` `[v1.0]` A general channel matrix stage MUST handle: balance, mono downmix, channel swap, and downmix of multichannel sources to stereo using the ITU-R BS.775 coefficients (C at −3 dB, surrounds at −3 dB). Upmixing is `[NON-GOAL]`.

#### 8.9.6 Per-device DSP presets

`REQ-AUD-106` `[v1.x]` Each output device MUST be able to hold its own DSP profile and EQ preset, applied automatically when that device becomes active — matching AIMP 4.30's per-output-device equalizer presets. Headphones and speakers need different curves, and re-adjusting on every switch is the friction this removes.

#### 8.9.7 Android DSP parity

`REQ-AUD-107` `[v1.0]` Android MUST implement the EQ and effects as **custom Media3 `AudioProcessor` instances sharing the identical biquad coefficient formulas** from `REQ-AUD-082`. `android.media.audiofx.Equalizer` MUST NOT be used for the main EQ: it typically exposes only 5 bands, its band frequencies are device-dependent, and its behaviour varies by OEM — none of which can produce parity with desktop.

`REQ-AUD-108` `[v1.0]` A conformance test MUST feed the same impulse and the same EQ settings through the desktop and Android DSP implementations and assert the outputs match within **−90 dBFS** RMS error. This is the mechanism that makes "same DSP on both platforms" a fact rather than a claim.

#### 8.9.8 True-peak limiter

`REQ-AUD-110` `[v1.0]` The final limiter MUST be **transparent when not engaged** — a pure passthrough, not a compressor sitting at unity.

`REQ-AUD-111` `[v1.0]` Specification: 4× oversampled true-peak detection, ceiling **−0.3 dBTP** (configurable −3.0…0.0), lookahead **2 ms**, attack instantaneous, release **50 ms**, soft knee. It MUST expose a gain-reduction meter so the user can see when it acts.

`REQ-AUD-112` `[v1.0]` The limiter MUST be bypassed in bit-perfect mode and MUST be independently switchable off, with a clear warning that clipping becomes possible.

### 8.10 Robustness and recovery

#### 8.10.1 Underruns

`REQ-AUD-115` `[v1.0]` On ring underrun the RT callback MUST output silence for the missing frames, increment an atomic counter, and continue. It MUST NOT block, retry, or attempt recovery inside the callback.

`REQ-AUD-116` `[v1.0]` The engine MUST maintain rolling underrun statistics and expose them in a diagnostics panel. If underruns exceed **3 within 10 seconds**, the engine MUST automatically increase the buffer period by one step (up to the configured maximum) and inform the user once, non-modally.

#### 8.10.2 Device loss and hot-plug

`REQ-AUD-117` `[v1.0]` The engine MUST recover from all of: device unplugged, default device changed, device format changed by another application, exclusive mode stolen, PulseAudio/PipeWire server restart, Windows session change, Bluetooth disconnect, and system suspend/resume.

`REQ-AUD-118` `[v1.0]` Recovery state machine — this MUST be implemented as an explicit state machine, not as scattered `if` statements:

```
        ┌──────────┐   open ok    ┌─────────┐   start   ┌─────────┐
        │  Closed  │─────────────▶│ Opened  │──────────▶│ Running │
        └──────────┘              └─────────┘           └─────────┘
             ▲                         ▲                   │
             │                         │      device lost / │
             │  give up after          │      format change │
             │  N attempts             │                    ▼
             │                    ┌──────────┐  backoff ┌──────────┐
             └────────────────────│  Failed  │◀─────────│Recovering│
                                  └──────────┘  exhausted└──────────┘
                                                            │ retry
                                                            └──▶ (reopen)
```

`REQ-AUD-119` `[v1.0]` Recovery policy: retry with backoff **100 ms, 250 ms, 500 ms, 1 s, 2 s, 5 s** (6 attempts). Preserve exact playback position and playing/paused state across recovery. On reaching `Failed`, keep the queue and position intact, enter paused state, and show an actionable error naming the device.

`REQ-AUD-120` `[v1.0]` If the *default* device changes while Eclipse is using "system default", follow it. If the user pinned a specific device, do **not** silently move to another device — report that the pinned device is gone.

#### 8.10.3 Format renegotiation

`REQ-AUD-121` `[v1.0]` When a track's format cannot be played on the currently open stream, the engine MUST: fade out (40 ms) → stop → close → reopen with the new format → fade in, preserving position. This MUST complete within **300 ms** on typical hardware, and MUST fall back to resampling into the existing stream if reopening fails.

### 8.11 Verification — how each audio claim is proven

`REQ-TST-001` `[v1.0]` Each of the following MUST exist as an automated test. An audio feature without its verification test is not done.

| # | Claim | Verification |
|---|---|---|
| 1 | **Decode correctness** | For every format in §8.3.1, decode a corpus file and compare against a reference WAV decoded by an independent tool, asserting RMS error < **−120 dBFS** for lossless formats and bit-exactness for WAV/FLAC/ALAC/WavPack. §29.4. |
| 2 | **Null test** | Decode → full DSP chain with every stage bypassed → capture. Assert the output is **bit-identical** to the decoder output. This is the single most valuable test in the suite: it proves bypass is real. |
| 3 | **Gapless boundary** | Take one continuous source; split it into two files with a known encoder; play them through the engine and capture; assert the concatenation is **sample-identical** to the continuous source's decode. Run for MP3 (LAME-tagged), AAC (`iTunSMPB`), FLAC, Opus, Vorbis, WavPack. |
| 4 | **Crossfade equal-power** | Capture a crossfade of two full-scale sine tones; assert the summed RMS through the transition stays within **±0.5 dB** of the steady-state RMS (a linear fade would dip ≈3 dB and MUST fail this test). |
| 5 | **Bit-perfect loopback** | With a loopback device or a file-backed mock sink in exclusive mode, assert the bytes delivered to the sink are **identical** to the source file's PCM payload. |
| 6 | **EQ transfer function** | Feed white noise, measure the magnitude response by FFT, compare to the analytically computed cascade response; assert agreement within **±0.25 dB** across 20 Hz–20 kHz. |
| 7 | **THD+N** | 1 kHz full-scale sine → DSP bypassed → assert THD+N < **−100 dB**. With EQ flat and enabled, assert < **−90 dB**. |
| 8 | **No clipping** | Feed material that would clip with +6 dB ReplayGain; assert with the limiter on that no sample exceeds the ceiling and no inter-sample peak exceeds it at 4× oversampling. |
| 9 | **RT safety** | Run under TSan with a mock sink at 5 ms periods while another thread continuously changes volume, EQ, presets, seeks, and skips tracks for 60 s. Assert zero TSan findings and zero underruns. |
| 10 | **RT allocation freedom** | Override the global allocator during a callback-driven test; assert **zero** allocations occur inside the callback. |
| 11 | **Latency accuracy** | Compare reported latency with measured loopback latency; assert within **15 %**. |
| 12 | **Recovery** | Programmatically simulate device loss, format change, and server restart mid-playback; assert position preservation within **±1 frame** and state restoration. |
| 13 | **Pause losslessness** | Pause and resume 100× at random positions; assert the captured output is sample-identical to uninterrupted playback. |
| 14 | **Desktop/Android DSP parity** | `REQ-AUD-108`. |
| 15 | **Seek exactness** | Seek to 1000 random frame positions in each format; assert the first decoded frame after each seek matches the reference at that exact index. |

---

## 9 · Library, Metadata & Playlist Specification

### 9.1 Scanner

`REQ-LIB-010` `[v1.0]` **Sources.** The library is defined by a user-managed list of *sources* (roots). Desktop sources are filesystem paths; Android sources are persisted SAF tree URIs (`REQ-GEN-006`). Each source records: id, display name, URI/path, enabled, recursive, last-scan timestamp, last-scan result, watch-enabled.

`REQ-LIB-011` `[v1.0]` **Scan modes:**

| Mode | Trigger | Behaviour |
|---|---|---|
| **Full** | User action, first run, or schema migration | Walk everything; re-read tags for all files. |
| **Incremental** | App start (optional), user action, scheduled | Walk everything; re-read tags **only** for files whose `(size, mtime)` changed. |
| **Watch-driven** | Filesystem event | Process only the affected paths, debounced. |
| **Single-item** | File opened directly, drag-and-drop | Read one file; add it as a transient entry not attached to any source. |

`REQ-LIB-012` `[v1.0]` **Change classification.** For each discovered path, compare `(path, size, mtime_ns)` against the database and classify as `NEW`, `CHANGED`, `UNCHANGED`, or `GONE`. `UNCHANGED` files MUST NOT have their tags re-read — this is the single biggest determinant of re-scan speed.

`REQ-LIB-013` `[v1.0]` **Extension prefilter.** The walker MAY filter by extension for speed, using a configurable allowlist. Files with unknown extensions MUST still be playable when opened explicitly (`REQ-AUD-026`). The allowlist MUST be visible and editable in settings, because collections contain oddities.

`REQ-LIB-014` `[v1.0]` **Exclusions.** Support per-source ignore patterns (glob), a global ignore list, and automatic skipping of: hidden files and directories, `.@__thumb`, `LOST.DIR`, `.trash*`, `$RECYCLE.BIN`, `System Volume Information`, and files smaller than **1 KiB**.

`REQ-LIB-015` `[v1.0]` **Symlink and loop safety.** The walker MUST track visited `(device, inode)` pairs on POSIX and file IDs on Windows, and MUST NOT recurse into a directory already visited. A symlink loop MUST NOT hang or exhaust memory. This is a fuzz/robustness target.

`REQ-LIB-016` `[v1.0]` **Cancellation and resumability.** A scan MUST be cancellable within **200 ms** of the request, MUST commit the work already done, and MUST resume from where it stopped rather than restarting.

`REQ-LIB-017` `[v1.0]` **Batching.** Database writes MUST be batched into transactions of **500 rows** (or 2 s of accumulation, whichever first) through the single writer thread (`REQ-GEN-052`).

`REQ-LIB-018` `[v1.0]` **Progress reporting** MUST be throttled to **10 Hz** maximum and MUST include: files seen, files added, files updated, files skipped, errors, current path, and an ETA once ≥5 % complete. Unthrottled progress events are a classic cause of UI jank during scans.

`REQ-LIB-019` `[v1.0]` **Filesystem watching.**

| Platform | Mechanism | Notes |
|---|---|---|
| Windows | `ReadDirectoryChangesW`, recursive | Coalesce bursts; handle buffer-overflow notification by scheduling an incremental re-scan of that source. |
| Linux | `inotify`, recursive with per-directory watches | Handle `IN_Q_OVERFLOW` the same way. Respect and report `max_user_watches` exhaustion instead of failing silently. |
| Android | WorkManager periodic scan + `ContentObserver` on `MediaStore` | True recursive watching is unavailable; a periodic scan is the honest approach and MUST be documented in §29.2. |

`REQ-LIB-020` `[v1.0]` Watch events MUST be debounced by **1000 ms** per directory. A file still being written (size changing between two stats 500 ms apart) MUST be deferred, not read mid-write.

`REQ-LIB-021` `[v1.0]` **Missing files are never hard-deleted.** A `GONE` file gets `missing_since` set. Its play counts, ratings, playlist memberships, and bookmarks MUST survive. Purging requires an explicit user action ("Remove missing tracks"), which MUST state how many rows and which playlists are affected before proceeding. Silent data loss when a drive is unmounted is unacceptable.

`REQ-LIB-022` `[v1.0]` Scan throughput target and measurement method are in §20.3.

### 9.2 Tags and metadata

#### 9.2.1 Read matrix

`REQ-LIB-025` `[v1.0]` TagLib is the sole tag reader/writer (§6.6). Required coverage:

| Container | Tag formats read | Written |
|---|---|---|
| MP3 | ID3v2.4, ID3v2.3, ID3v2.2, ID3v1.1, APEv2, Lyrics3v2 | ID3v2.4 (configurable to 2.3) |
| FLAC | Vorbis comments, embedded `PICTURE`, ID3 (tolerated) | Vorbis comments |
| Ogg (Vorbis/Opus/FLAC) | Vorbis comments, `METADATA_BLOCK_PICTURE` | Vorbis comments |
| MP4 / M4A | iTunes atoms, free-form `----` atoms | iTunes atoms |
| WAV | `INFO` chunks, embedded ID3, BWF `bext` | ID3 chunk |
| AIFF | ID3 chunk | ID3 chunk |
| WavPack | APEv2, ID3v1 | APEv2 |
| APE | APEv2, ID3v1 | APEv2 |
| Musepack | APEv2, ID3v1 | APEv2 |
| ASF / WMA | ASF attributes | ASF attributes |

`REQ-LIB-026` `[v1.0]` **Field set.** The following MUST be read where present: title, artist, album, album artist, composer, conductor, remixer, genre (multi-valued), date/year (full ISO-8601 date where available), original date, track number and total, disc number and total, comment, grouping, subtitle, BPM, key, ISRC, MusicBrainz IDs (recording, release, release-group, artist, album-artist), ReplayGain tags (§8.9.4), compilation flag, lyrics (`USLT`/`LYRICS`), embedded artwork, rating (`POPM`), media type, catalogue number, barcode, label, encoder, and encoding settings.

`REQ-LIB-027` `[v1.0]` **Custom / flexible tags.** Arbitrary user-defined tags MUST be readable and writable via ID3v2.4 `TXXX`, Vorbis comments, and APEv2, and MUST be queryable in smart playlists (§9.6). This is the "tag flexible" capability both AIMP and foobar2000 provide, and it is what lets power users build their own taxonomies.

#### 9.2.2 Multi-valued fields

`REQ-LIB-028` `[v1.0]` Artist and genre are **multi-valued**. Parse using, in order: the container's native multi-value support (Vorbis repeated keys, ID3v2.4 null-separated); then a configurable separator list (default `;`, ` / `, `feat.`, `ft.`, `&`, `,` — with `,` **off** by default because it destroys names like "Earth, Wind & Fire"). The separator list MUST be user-editable, and a preview of the split MUST be shown before it is applied.

#### 9.2.3 Normalisation and sort keys

`REQ-LIB-029` `[v1.0]` For every artist, album artist, album, and title, store both the display string and a **normalised sort key** computed as: Unicode NFKD → strip diacritics → casefold → strip leading articles per locale (`the`, `a`, `an`, `der`, `die`, `das`, `le`, `la`, `les`, `el`, `los`, `las` — configurable, defaulting to the UI language) → collapse whitespace. Sorting MUST use the sort key; display MUST use the original.

`REQ-LIB-030` `[v1.0]` `ARTISTSORT`, `ALBUMSORT`, `ALBUMARTISTSORT`, and `TITLESORT` tags, where present, MUST override the computed sort key.

`REQ-LIB-031` `[v1.0]` **Album identity** MUST be `(normalised album title, normalised album artist, date)` — **not** the folder path and **not** the album title alone. Where a MusicBrainz release ID is present it MUST take precedence. Getting this wrong produces the "album split into six albums" bug that plagues most players.

`REQ-LIB-032` `[v1.0]` `compilation`/`albumartist` handling: when a track has no album artist and the album contains more than one distinct artist, the album artist MUST resolve to "Various Artists" (localised) rather than to the first track's artist.

#### 9.2.4 Encoding and Unicode correctness

`REQ-LIB-033` `[v1.0]` **This MUST be correct on day one.** Requirements:

1. All internal strings are UTF-8 (desktop) / Kotlin `String` UTF-16 (Android). Filesystem paths are handled as `std::filesystem::path` / platform-native wide strings on Windows — **never** as narrow `char*` on Windows.
2. ID3v2.3 `Latin-1`-declared frames that contain invalid Latin-1 but valid UTF-8 or a legacy code page MUST be handled by a **heuristic decoder** offering the detected candidates. Default to the declared encoding; offer a per-source override for the common CP1251/CP1252/Shift-JIS/GBK/EUC-KR cases.
3. The encoding override MUST be applicable in bulk, with preview, and MUST be reversible.
4. Filenames containing emoji, RTL text, combining characters, or surrogate pairs MUST round-trip through scan, display, playlist export, and playback.
5. Test corpus MUST include: Cyrillic CP1251-in-ID3v2.3, Japanese Shift-JIS, Korean EUC-KR, Arabic RTL, emoji in filename, a 255-byte-name file, and a path longer than 260 characters on Windows (requires `\\?\` long-path handling or the long-path manifest — both MUST be implemented).

#### 9.2.5 Tag writing

`REQ-LIB-034` `[v1.0]` All tag writes MUST be **atomic and crash-safe**: write to a temporary file in the same directory, `fsync`, then atomically rename over the original. A power loss MUST never leave a truncated music file.

`REQ-LIB-035` `[v1.0]` Tag writing MUST be **off by default** for files inside read-only or removable sources, and the app MUST verify write permission before offering to edit.

`REQ-LIB-036` `[v1.0]` **Batch tag editor with undo.** Requirements: multi-select any number of tracks; edit any field; show `<multiple values>` for differing fields and only write fields the user actually touched; support find-and-replace across a field; support numbering (auto track numbers); preview every change before commit; **full undo of an entire batch as one operation**, restoring prior tag bytes. Undo history MUST persist for the session at minimum.

`REQ-LIB-037` `[v1.x]` **Tag-from-filename and filename-from-tag**, both driven by EFS patterns (§10), with preview and undo. Example patterns: `%tracknumber% - %artist% - %title%` and `%albumartist%/%album% (%year%)/%tracknumber:02% %title%`.

`REQ-LIB-038` `[v1.x]` **Rename/organise files on disk** from tags, with a dry-run that lists every planned move, collision detection, and a rollback log.

### 9.3 Cue sheets and multi-track single files

`REQ-LIB-040` `[v1.0]` Both **external `.cue`** files and **embedded cue sheets** (the `CUESHEET` Vorbis comment, FLAC's `CUESHEET` metadata block, and APEv2 `Cuesheet`) MUST be supported. Collections of single-file albums are common among the users this player targets, and ignoring cue sheets excludes them.

`REQ-LIB-041` `[v1.0]` **Model.** A cue-referenced track is a row in `tracks` with `container_path` pointing at the audio file, `subtrack_index` ≥ 1, and explicit `start_frame` / `frame_count`. Cue tracks and normal tracks MUST be indistinguishable to the rest of the application — the same playback, the same tagging UI (with disk-writes disabled where the container cannot hold per-track tags), the same playlist behaviour.

`REQ-LIB-042` `[v1.0]` The cue parser MUST handle: `PERFORMER`, `TITLE`, `SONGWRITER`, `FILE`, `TRACK`, `INDEX 00`/`01`, `PREGAP`, `POSTGAP`, `REM DATE`, `REM GENRE`, `REM DISCID`, `REM REPLAYGAIN_*`, `FLAGS`, `ISRC`, and `CATALOG`. `INDEX 01` defines the track start; `INDEX 00` is the pregap. `MM:SS:FF` frames are **75 per second** — a classic off-by-a-factor bug.

`REQ-LIB-043` `[v1.0]` Cue parsing MUST be defensive against: missing `FILE`, a `FILE` that does not exist (try case-insensitive and extension-swap resolution before failing), overlapping indexes, non-monotonic timestamps, BOM-prefixed files, CRLF and CR line endings, and non-UTF-8 encodings. It is a fuzz target (§21.6).

`REQ-LIB-044` `[v1.0]` Playback within a cue-defined track MUST be **sample-accurate at the boundaries**, and consecutive cue tracks in one file MUST play **gaplessly by construction** (they are one continuous decode; the scheduler simply reports a track change). Assert this in §8.11 test 3.

### 9.4 Database schema

`REQ-LIB-050` `[v1.0]` This schema is normative for **both** platforms (§6.8). Identical table names, column names, and semantics. SQLite DDL is canonical; Room entities MUST mirror it exactly.

```sql
-- ===========================================================================
--  Eclipse Player library schema — v1
--  PRAGMAs required at every connection open:
--    PRAGMA journal_mode = WAL;      PRAGMA foreign_keys = ON;
--    PRAGMA synchronous = NORMAL;    PRAGMA busy_timeout = 5000;
--  Rationale: WAL for concurrent readers with the single writer (REQ-GEN-052).
-- ===========================================================================

CREATE TABLE schema_version (
    version     INTEGER NOT NULL PRIMARY KEY,
    applied_at  INTEGER NOT NULL          -- unix epoch seconds
);

-- --------------------------------------------------------------------- sources
CREATE TABLE sources (
    id             INTEGER PRIMARY KEY,
    uuid           TEXT    NOT NULL UNIQUE,   -- stable across devices, for sync
    display_name   TEXT    NOT NULL,
    location       TEXT    NOT NULL UNIQUE,   -- absolute path, or SAF tree URI
    kind           INTEGER NOT NULL,          -- 0=local dir, 1=SAF tree, 2=network share
    recursive      INTEGER NOT NULL DEFAULT 1,
    enabled        INTEGER NOT NULL DEFAULT 1,
    watch_enabled  INTEGER NOT NULL DEFAULT 1,
    ignore_globs   TEXT,                      -- JSON array of glob strings
    last_scan_at   INTEGER,
    last_scan_ms   INTEGER,
    last_scan_code INTEGER,                   -- 0=ok, else ScanError enum
    offline_since  INTEGER                    -- non-null when the root is unreachable
);

-- --------------------------------------------------------------------- artists
CREATE TABLE artists (
    id            INTEGER PRIMARY KEY,
    uuid          TEXT    NOT NULL UNIQUE,
    name          TEXT    NOT NULL,
    sort_key      TEXT    NOT NULL,           -- REQ-LIB-029
    mbid          TEXT,                       -- MusicBrainz artist id
    created_at    INTEGER NOT NULL
);
CREATE UNIQUE INDEX idx_artists_sort ON artists(sort_key);
CREATE INDEX        idx_artists_mbid ON artists(mbid) WHERE mbid IS NOT NULL;

-- ---------------------------------------------------------------------- albums
CREATE TABLE albums (
    id                INTEGER PRIMARY KEY,
    uuid              TEXT    NOT NULL UNIQUE,
    title             TEXT    NOT NULL,
    sort_key          TEXT    NOT NULL,
    album_artist_id   INTEGER REFERENCES artists(id) ON DELETE SET NULL,
    release_date      TEXT,                   -- ISO-8601, may be 'YYYY' or 'YYYY-MM-DD'
    original_date     TEXT,
    disc_total        INTEGER,
    is_compilation    INTEGER NOT NULL DEFAULT 0,
    mbid_release      TEXT,
    mbid_release_group TEXT,
    label             TEXT,
    catalogue_number  TEXT,
    barcode           TEXT,
    artwork_id        INTEGER REFERENCES artwork(id) ON DELETE SET NULL,
    created_at        INTEGER NOT NULL
);
-- REQ-LIB-031: album identity
CREATE UNIQUE INDEX idx_albums_identity
    ON albums(sort_key, IFNULL(album_artist_id, -1), IFNULL(release_date, ''));
CREATE INDEX idx_albums_mbid ON albums(mbid_release) WHERE mbid_release IS NOT NULL;

-- --------------------------------------------------------------------- artwork
CREATE TABLE artwork (
    id           INTEGER PRIMARY KEY,
    sha256       TEXT    NOT NULL UNIQUE,     -- content address; dedupes across the library
    source_kind  INTEGER NOT NULL,            -- 0=embedded, 1=folder image, 2=online, 3=user
    origin_path  TEXT,
    mime_type    TEXT    NOT NULL,
    width        INTEGER NOT NULL,
    height       INTEGER NOT NULL,
    byte_size    INTEGER NOT NULL,
    cache_path   TEXT,                        -- path to the full-size cached copy
    created_at   INTEGER NOT NULL
);

-- ---------------------------------------------------------------------- tracks
CREATE TABLE tracks (
    id               INTEGER PRIMARY KEY,
    uuid             TEXT    NOT NULL UNIQUE, -- stable identity for sync (§18)
    source_id        INTEGER REFERENCES sources(id) ON DELETE CASCADE,

    -- Location. container_path is the file on disk; subtrack_index > 0 means
    -- this row is a cue-sheet slice of that file (REQ-LIB-041).
    container_path   TEXT    NOT NULL,
    subtrack_index   INTEGER NOT NULL DEFAULT 0,
    start_frame      INTEGER NOT NULL DEFAULT 0,
    frame_count      INTEGER,                 -- NULL = whole file / unknown

    -- Filesystem identity, used for change detection (REQ-LIB-012)
    file_size        INTEGER,
    file_mtime_ns    INTEGER,
    content_hash     TEXT,                    -- optional; only computed on demand

    -- Descriptive metadata
    title            TEXT    NOT NULL,
    title_sort       TEXT    NOT NULL,
    artist_id        INTEGER REFERENCES artists(id) ON DELETE SET NULL,
    album_id         INTEGER REFERENCES albums(id)  ON DELETE SET NULL,
    composer         TEXT,
    conductor        TEXT,
    grouping         TEXT,
    subtitle         TEXT,
    comment          TEXT,
    track_number     INTEGER,
    track_total      INTEGER,
    disc_number      INTEGER,
    disc_total       INTEGER,
    release_date     TEXT,
    isrc             TEXT,
    mbid_recording   TEXT,

    -- Technical
    duration_ms      INTEGER NOT NULL,
    sample_rate      INTEGER,
    channels         INTEGER,
    bit_depth        INTEGER,
    bitrate_kbps     INTEGER,
    is_vbr           INTEGER,
    codec            TEXT,                    -- 'mp3', 'flac', 'opus', ...
    container        TEXT,                    -- 'mp3', 'ogg', 'mp4', ...
    is_lossless      INTEGER NOT NULL DEFAULT 0,

    -- Gapless (§8.4) — cached so playback never re-parses
    gapless_source   INTEGER NOT NULL DEFAULT 0,
    skip_start       INTEGER NOT NULL DEFAULT 0,
    skip_end         INTEGER NOT NULL DEFAULT 0,

    -- Loudness (§8.9.4)
    rg_track_gain    REAL, rg_track_peak REAL,
    rg_album_gain    REAL, rg_album_peak REAL,
    rg_scanned_at    INTEGER,

    -- Analysis
    bpm              REAL,
    music_key        TEXT,
    fingerprint_id   INTEGER REFERENCES fingerprints(id) ON DELETE SET NULL,

    -- User state — MUST survive file loss (REQ-LIB-021)
    rating           INTEGER,                 -- 0..100, NULL = unrated
    is_loved         INTEGER NOT NULL DEFAULT 0,
    play_count       INTEGER NOT NULL DEFAULT 0,
    skip_count       INTEGER NOT NULL DEFAULT 0,
    last_played_at   INTEGER,
    added_at         INTEGER NOT NULL,
    resume_position_ms INTEGER,               -- auto-resume (REQ-PLS-040)

    artwork_id       INTEGER REFERENCES artwork(id) ON DELETE SET NULL,

    -- Lifecycle
    missing_since    INTEGER,                 -- non-null = file not found
    unplayable_code  INTEGER,                 -- non-null = decode failed; see REQ-AUD-027
    updated_at       INTEGER NOT NULL,

    UNIQUE(container_path, subtrack_index)
);
CREATE INDEX idx_tracks_album      ON tracks(album_id, disc_number, track_number);
CREATE INDEX idx_tracks_artist     ON tracks(artist_id, title_sort);
CREATE INDEX idx_tracks_added      ON tracks(added_at DESC);
CREATE INDEX idx_tracks_played     ON tracks(last_played_at DESC) WHERE last_played_at IS NOT NULL;
CREATE INDEX idx_tracks_playcount  ON tracks(play_count DESC);
CREATE INDEX idx_tracks_source     ON tracks(source_id);
CREATE INDEX idx_tracks_missing    ON tracks(missing_since) WHERE missing_since IS NOT NULL;
CREATE INDEX idx_tracks_container  ON tracks(container_path);

-- Multi-valued relations (REQ-LIB-028)
CREATE TABLE track_artists (
    track_id   INTEGER NOT NULL REFERENCES tracks(id)  ON DELETE CASCADE,
    artist_id  INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE,
    role       INTEGER NOT NULL DEFAULT 0,   -- 0=primary,1=featured,2=remixer,3=composer
    position   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (track_id, artist_id, role)
);
CREATE INDEX idx_track_artists_artist ON track_artists(artist_id);

CREATE TABLE genres (
    id       INTEGER PRIMARY KEY,
    name     TEXT NOT NULL UNIQUE,
    sort_key TEXT NOT NULL
);
CREATE TABLE track_genres (
    track_id INTEGER NOT NULL REFERENCES tracks(id)  ON DELETE CASCADE,
    genre_id INTEGER NOT NULL REFERENCES genres(id)  ON DELETE CASCADE,
    PRIMARY KEY (track_id, genre_id)
);
CREATE INDEX idx_track_genres_genre ON track_genres(genre_id);

-- Arbitrary user tags (REQ-LIB-027)
CREATE TABLE track_custom_tags (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    key      TEXT    NOT NULL,
    value    TEXT    NOT NULL,
    PRIMARY KEY (track_id, key, value)
);
CREATE INDEX idx_custom_tags_key ON track_custom_tags(key, value);

-- ------------------------------------------------------------------- playlists
CREATE TABLE playlists (
    id           INTEGER PRIMARY KEY,
    uuid         TEXT    NOT NULL UNIQUE,
    name         TEXT    NOT NULL,
    kind         INTEGER NOT NULL,      -- 0=manual, 1=smart, 2=folder-mirror
    description  TEXT,
    artwork_id   INTEGER REFERENCES artwork(id) ON DELETE SET NULL,
    sort_order   INTEGER NOT NULL DEFAULT 0,
    rule_json    TEXT,                  -- smart playlists only; §9.6
    auto_refresh INTEGER NOT NULL DEFAULT 1,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL
);
CREATE TABLE playlist_items (
    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    track_id    INTEGER REFERENCES tracks(id) ON DELETE CASCADE,
    -- For entries that are not in the library (a stream, or a file outside any
    -- source) keep the raw URI so imported playlists never lose entries.
    external_uri TEXT,
    added_at    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, position),
    CHECK (track_id IS NOT NULL OR external_uri IS NOT NULL)
);
CREATE INDEX idx_playlist_items_track ON playlist_items(track_id);

-- ------------------------------------------------------------------ user state
CREATE TABLE play_history (
    id           INTEGER PRIMARY KEY,
    track_id     INTEGER REFERENCES tracks(id) ON DELETE CASCADE,
    played_at    INTEGER NOT NULL,
    ms_played    INTEGER NOT NULL,
    completed    INTEGER NOT NULL,      -- 1 if the scrobble threshold was met
    device_uuid  TEXT,                  -- which device; used by sync (§18)
    scrobbled    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_history_time  ON play_history(played_at DESC);
CREATE INDEX idx_history_track ON play_history(track_id, played_at DESC);

CREATE TABLE bookmarks (               -- REQ-PLS-041
    id          INTEGER PRIMARY KEY,
    uuid        TEXT    NOT NULL UNIQUE,
    track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    position_ms INTEGER NOT NULL,
    label       TEXT,
    created_at  INTEGER NOT NULL
);
CREATE INDEX idx_bookmarks_track ON bookmarks(track_id, position_ms);

CREATE TABLE lyrics (
    track_id    INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    kind        INTEGER NOT NULL,      -- 0=plain, 1=LRC, 2=enhanced LRC
    content     TEXT    NOT NULL,
    source_kind INTEGER NOT NULL,      -- 0=embedded, 1=sidecar file, 2=online, 3=user
    offset_ms   INTEGER NOT NULL DEFAULT 0,
    updated_at  INTEGER NOT NULL
);

CREATE TABLE fingerprints (            -- REQ-LIB-070
    id          INTEGER PRIMARY KEY,
    algorithm   TEXT    NOT NULL,      -- 'chromaprint-1'
    duration_s  INTEGER NOT NULL,
    fp_data     BLOB    NOT NULL,
    fp_hash     INTEGER NOT NULL,      -- 32-bit bucket for fast candidate lookup
    computed_at INTEGER NOT NULL
);
CREATE INDEX idx_fingerprints_hash ON fingerprints(fp_hash, duration_s);

-- ------------------------------------------------------------------ networking
CREATE TABLE radio_stations (          -- §17.1
    id          INTEGER PRIMARY KEY,
    uuid        TEXT    NOT NULL UNIQUE,
    name        TEXT    NOT NULL,
    stream_url  TEXT    NOT NULL,
    homepage    TEXT,
    genre       TEXT,
    codec       TEXT,
    bitrate_kbps INTEGER,
    artwork_id  INTEGER REFERENCES artwork(id) ON DELETE SET NULL,
    is_favorite INTEGER NOT NULL DEFAULT 0,
    last_played_at INTEGER,
    added_at    INTEGER NOT NULL
);

CREATE TABLE podcasts (                -- §17.2  [v1.x]
    id           INTEGER PRIMARY KEY,
    uuid         TEXT    NOT NULL UNIQUE,
    feed_url     TEXT    NOT NULL UNIQUE,
    title        TEXT    NOT NULL,
    author       TEXT,
    description  TEXT,
    artwork_id   INTEGER REFERENCES artwork(id) ON DELETE SET NULL,
    etag         TEXT,
    last_modified TEXT,
    last_checked_at INTEGER,
    auto_download INTEGER NOT NULL DEFAULT 0,
    keep_episodes INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE podcast_episodes (
    id            INTEGER PRIMARY KEY,
    podcast_id    INTEGER NOT NULL REFERENCES podcasts(id) ON DELETE CASCADE,
    guid          TEXT    NOT NULL,
    title         TEXT    NOT NULL,
    enclosure_url TEXT    NOT NULL,
    published_at  INTEGER,
    duration_ms   INTEGER,
    local_path    TEXT,
    play_position_ms INTEGER NOT NULL DEFAULT 0,
    is_played     INTEGER NOT NULL DEFAULT 0,
    UNIQUE(podcast_id, guid)
);

-- ------------------------------------------------------------------- sync (§18)
CREATE TABLE change_log (
    id          INTEGER PRIMARY KEY,
    entity      TEXT    NOT NULL,      -- 'track','playlist','bookmark',...
    entity_uuid TEXT    NOT NULL,
    field       TEXT,                  -- NULL = whole-entity create/delete
    op          INTEGER NOT NULL,      -- 0=upsert, 1=delete
    value_json  TEXT,
    lamport     INTEGER NOT NULL,      -- logical clock, for LWW resolution
    device_uuid TEXT    NOT NULL,
    created_at  INTEGER NOT NULL
);
CREATE INDEX idx_changelog_entity ON change_log(entity, entity_uuid, lamport);
CREATE INDEX idx_changelog_lamport ON change_log(lamport);

-- ------------------------------------------------------------------ full text
CREATE VIRTUAL TABLE tracks_fts USING fts5(
    title, artist, album, album_artist, genre, composer, comment,
    content = '',                      -- contentless: we own the sync triggers
    tokenize = "unicode61 remove_diacritics 2"
);
```

`REQ-LIB-051` `[v1.0]` **Migrations.** Every schema change ships as a numbered, forward-only migration applied inside a transaction, on both platforms, in the same commit, with the same number (`REQ-LIB-001`). Each migration MUST have a test that builds the previous schema, populates representative data, migrates, and asserts data integrity. Destructive migrations MUST create a timestamped database backup first.

`REQ-LIB-052` `[v1.0]` **Corruption recovery.** On `SQLITE_CORRUPT`, the app MUST: not crash; move the corrupt file aside with a timestamp; create a fresh database; offer to re-scan; and explicitly tell the user what user state (ratings, counts, playlists) could not be recovered. A player that silently loses a decade of play counts is worse than one that admits it.

### 9.5 Search

`REQ-LIB-060` `[v1.0]` **Two-stage search.** Stage 1 is FTS5 prefix search over `tracks_fts`, which handles typed input. Stage 2 is a fuzzy pass (Damerau–Levenshtein, max edit distance 2 for terms ≥4 characters) applied to the top FTS candidates plus artist/album name lists, for typo tolerance.

`REQ-LIB-061` `[v1.0]` **Latency budget:** first results rendered within **80 ms** of the last keystroke for a 100,000-track library on the reference hardware (§20.1). Search MUST be debounced at 120 ms and MUST cancel the in-flight query on new input.

`REQ-LIB-062` `[v1.0]` Search MUST be **diacritic- and case-insensitive** (`remove_diacritics 2`), MUST match on any indexed field, MUST support quoted phrases, and MUST support field-scoped queries (`artist:radiohead album:kid`).

`REQ-LIB-063` `[v1.0]` Results MUST be grouped by entity type (Tracks / Albums / Artists / Playlists / Genres) with a relevance ranking that boosts exact prefix matches on title and artist above mid-string matches.

`REQ-LIB-064` `[v1.0]` FTS index maintenance MUST be driven by explicit code in the writer thread, not by SQL triggers, so that batch scans can defer index updates to the end of a transaction batch. Trigger-driven FTS updates are a known scan-throughput killer.

### 9.6 Smart playlists

`REQ-PLS-010` `[v1.0]` Smart playlists are rule-based, stored as JSON in `playlists.rule_json`, validated against `shared-spec/schemas/smart-playlist.schema.json`, and compiled to a parameterised SQL query. **String interpolation into SQL is FORBIDDEN** — every literal MUST be a bound parameter (§21.5).

`REQ-PLS-011` `[v1.0]` Grammar, canonically at `shared-spec/grammars/smart-playlist.ebnf`:

```ebnf
query        = expression [ "ORDER" "BY" order-list ] [ "LIMIT" integer ] ;

expression   = or-expr ;
or-expr      = and-expr { "OR" and-expr } ;
and-expr     = unary { "AND" unary } ;
unary        = [ "NOT" ] ( "(" expression ")" | comparison ) ;

comparison   = field operator operand
             | field "BETWEEN" operand "AND" operand
             | field "IN" "(" operand { "," operand } ")"
             | field "IS" [ "NOT" ] "NULL" ;

field        = builtin-field | "custom:" identifier ;
builtin-field= "title"    | "artist"      | "albumartist" | "album"
             | "genre"    | "composer"    | "comment"     | "grouping"
             | "year"     | "date"        | "tracknumber" | "discnumber"
             | "duration" | "bitrate"     | "samplerate"  | "bitdepth"
             | "channels" | "codec"       | "container"   | "islossless"
             | "rating"   | "loved"       | "playcount"   | "skipcount"
             | "lastplayed"| "added"      | "bpm"         | "key"
             | "path"     | "filename"    | "filesize"    | "source"
             | "playlist" | "missing"     | "hasartwork"  | "haslyrics"
             | "rgtrackgain" ;

operator     = "=" | "!=" | ">" | "<" | ">=" | "<="
             | "CONTAINS" | "NOTCONTAINS"
             | "STARTSWITH" | "ENDSWITH"
             | "MATCHES" ;                      (* glob, not regex — see REQ-PLS-013 *)

operand      = string | number | boolean | duration-lit | date-lit | relative-date ;
duration-lit = number ( "s" | "m" | "h" ) ;      (* 3m30s *)
date-lit     = "'" iso-8601 "'" ;
relative-date= "-" number ( "d" | "w" | "m" | "y" ) ;   (* -30d = 30 days ago *)

order-list   = order-term { "," order-term } ;
order-term   = ( field | "random" ) [ "ASC" | "DESC" ] ;
```

`REQ-PLS-012` `[v1.0]` Worked examples that MUST evaluate correctly (and MUST appear as fixtures in `shared-spec/conformance/smart-playlist-cases.json`):

| Intent | Rule |
|---|---|
| Neglected favourites | `rating >= 80 AND (lastplayed < -180d OR lastplayed IS NULL)` |
| Fresh and unheard | `added > -14d AND playcount = 0 ORDER BY added DESC LIMIT 100` |
| Good jazz, well-played | `genre CONTAINS 'Jazz' AND playcount > 5` |
| Hi-res only | `islossless = true AND (samplerate > 48000 OR bitdepth > 16)` |
| Long-form | `duration > 10m AND NOT genre CONTAINS 'Podcast'` |
| Workout | `bpm BETWEEN 120 AND 140 AND loved = true ORDER BY random LIMIT 50` |
| Custom taxonomy | `custom:mood = 'melancholy' AND year < 1990` |
| Needs attention | `hasartwork = false OR haslyrics = false` |

`REQ-PLS-013` `[v1.0]` `MATCHES` is **glob**, not a regular expression. Regex is deliberately excluded: it is a denial-of-service surface (catastrophic backtracking) inside a query the user can share in a skin or playlist file.

`REQ-PLS-014` `[v1.0]` **Refresh semantics.** Smart playlists MUST refresh: on open, on library change affecting a referenced field, and on app launch when `auto_refresh` is set (matching AIMP 4.30's behaviour). Refresh MUST be incremental where possible and MUST NOT block the UI thread.

`REQ-PLS-015` `[v1.0]` The rule editor MUST offer both a **visual builder** (rows of field/operator/value with AND/OR grouping) and a **text mode** using the grammar above, kept in sync bidirectionally, with live match-count feedback and inline validation errors.

### 9.7 Playlists, queue, and playback state

`REQ-PLS-020` `[v1.0]` **Multiple simultaneous playlists** with a tab strip. Each playlist keeps its own scroll position, sort, filter, and current-item marker. This is core to both reference players' workflow.

`REQ-PLS-021` `[v1.0]` **The queue is a distinct entity from the playlist.** "Play next" and "Add to queue" insert into an ephemeral queue that takes precedence over the playlist's natural order and is consumed as it plays. The queue MUST be viewable, reorderable, and clearable, and MUST survive app restart. Conflating the two is a common and much-complained-about design mistake.

`REQ-PLS-022` `[v1.0]` **Playback order** MUST support: repeat off / repeat all / repeat one, and shuffle off / shuffle tracks / shuffle albums. Shuffle MUST use a **shuffled-order permutation**, not random selection, so no track repeats before the list is exhausted, and *previous* during shuffle MUST retrace the actual history rather than pick another random track.

`REQ-PLS-023` `[v1.0]` **Playlist import/export matrix:**

| Format | Import | Export | Notes |
|---|---|---|---|
| M3U | ✔ | ✔ | Legacy; encoding heuristic per §9.2.4 |
| M3U8 | ✔ | ✔ | UTF-8; **default export format** |
| PLS | ✔ | ✔ | |
| XSPF | ✔ | ✔ | |
| CUE | ✔ | — | Handled as a library construct (§9.3), not as a playlist |
| ASX / WAX / WVX | ✔ | — | Read-only; commonly encountered |
| Eclipse native (`.ecpl`, JSON) | ✔ | ✔ | Preserves ratings, custom order, queue state, and per-item metadata |

`REQ-PLS-024` `[v1.0]` **Path portability.** On export the user MUST be able to choose relative or absolute paths. On import, resolution MUST try, in order: the path as given → relative to the playlist file → relative to each configured source → basename match within the library (with a disambiguation prompt on multiple hits). Unresolvable entries MUST be **retained** as `external_uri` and shown as missing, never silently dropped.

`REQ-PLS-025` `[v1.0]` **Playlist undo.** Every mutating playlist operation (add, remove, reorder, sort, clear, dedupe) MUST be undoable, with a depth of at least 50 operations, matching AIMP 4.30's playlist undo. Clearing a 5,000-item playlist by accident MUST be recoverable.

`REQ-PLS-026` `[v1.0]` Playlist operations MUST include: sort by any column, reverse, shuffle-in-place, remove duplicates, remove missing, remove played, invert selection, and "crop to selection".

`REQ-PLS-027` `[v1.0]` The currently playing track MUST be visually highlighted in every view that contains it, and a "scroll to current track" action MUST exist and be bound to a default shortcut (§13).

`REQ-PLS-040` `[v1.0]` **Auto-resume.** On launch, restore the exact queue, playlist tabs, current track, and playback position. A per-track `resume_position_ms` MUST be kept for tracks longer than a configurable threshold (default 20 minutes, covering audiobooks, mixes, and DJ sets) and offered on next play.

`REQ-PLS-041` `[v1.0]` **Bookmarks.** Named position markers on any track, listable and sortable by creation date or title (matching AIMP 4.30), with jump-to. Distinct from the queue and from resume position.

`REQ-PLS-042` `[v1.0]` **A-B repeat.** Set point A and point B, loop between them with sample accuracy, with a visual indication on the seek bar and a single-key clear.

`REQ-PLS-043` `[v1.0]` **Sleep timer.** By duration, by end-of-track, or by end-of-queue, with the fade-out from `REQ-AUD-050`, and an optional "extend by 15 minutes" action while the fade is running.

`REQ-PLS-044` `[v1.x]` **Stop after current track** as a toggle, and **shutdown / sleep / hibernate on playback completion** on desktop (AIMP parity), guarded by a 30-second cancellable countdown dialog.

### 9.8 Artwork pipeline

`REQ-LIB-065` `[v1.0]` Artwork resolution order, first hit wins, all configurable:

1. Embedded picture in the audio file, preferring `PICTURE`/`APIC` type 3 (front cover), then type 0 (other), then the first available.
2. Sidecar image in the same folder, matched against a configurable name list: `cover`, `folder`, `front`, `album`, `albumart`, `artwork`, `<album name>`, `<filename>` — with extensions `jpg`, `jpeg`, `png`, `webp`, `bmp`.
3. Any single image file in the folder, if exactly one exists.
4. Online lookup — **only if explicitly enabled** (§17.3).
5. Generated placeholder derived deterministically from the album identity (so the same album always yields the same placeholder).

`REQ-LIB-066` `[v1.0]` Artwork MUST be **content-addressed by SHA-256** (`artwork.sha256`), so one image shared by 200 tracks is stored once. Cache thumbnails at **64, 128, 256, 512, and 1024 px** on the long edge, generated lazily, stored as WebP quality 88 (with a PNG fallback where transparency exists).

`REQ-LIB-067` `[v1.0]` The artwork cache MUST have a **configurable size cap** (default 512 MiB desktop, 128 MiB Android) with LRU eviction, MUST be fully clearable from settings, and MUST live in the OS cache directory so system cleaners can reclaim it (§19.2).

`REQ-LIB-068` `[v1.0]` Image decoding MUST be hardened: enforce maximum dimensions (**8192 × 8192**) and maximum decoded size before allocating, reject malformed data without crashing, and never decode on the UI thread. Embedded artwork is attacker-controlled data (§21.2).

`REQ-LIB-069` `[v1.x]` The user MUST be able to set custom artwork per track and per album, and to remove artwork, without modifying the audio files unless they explicitly ask for it to be embedded.

### 9.9 Duplicate detection

`REQ-LIB-070` `[v1.0]` Duplicate detection MUST be **content-based via Chromaprint**, not filename matching. The v1.x document said "by audio fingerprint, not filename matching" without naming an algorithm; the algorithm is now specified.

`REQ-LIB-071` `[v1.0]` Method: compute a Chromaprint fingerprint over the first **120 s** of audio; bucket by a coarse hash plus duration (±3 s) to shortlist candidates; compare shortlisted pairs by the fingerprint's bit-error rate; report a match above a configurable confidence (default 0.90). Fingerprinting is **local and offline**; no network call is involved.

`REQ-LIB-072` `[v1.0]` The duplicate review UI MUST show, for each group: format, bitrate, sample rate, bit depth, duration, file size, path, and tag completeness, and MUST offer keep-rules (keep highest bitrate / keep lossless / keep oldest / keep newest / keep by preferred source) with a **preview of every deletion before anything is deleted**. Deletion MUST default to the recycle bin/trash, never an unlinking, unless the user opts in.

`REQ-LIB-073` `[v1.0]` Fingerprinting MUST run in the background analysis pool, be cancellable, be resumable, and be throttled so it never competes with playback for CPU.

### 9.10 Audio converter

`REQ-LIB-080` `[v1.x]` A converter/transcoder module, matching AIMP's converter plus its Encoders addon category:

- **Targets:** FLAC, ALAC, Opus, Ogg Vorbis, WavPack, WAV/PCM, and MP3 (subject to `REQ-GEN-016`).
- **Per-target settings** with sane presets (e.g. Opus 128 kbps VBR; FLAC compression 5; MP3 V0).
- **Tag propagation**, including artwork and custom tags, into the output.
- **Output naming** via an EFS pattern (§10), with collision handling.
- **Resample and channel-map** options, reusing the §8.6 converters so quality is consistent with playback.
- **Batch with a job queue**, parallelism defaulting to `cores − 1`, pause/resume/cancel, and a per-item error report that never aborts the whole batch.
- **Cue-sheet splitting**: convert a single-file album into per-track files using its cue sheet.
- **ReplayGain** computed and written on the output.
- **Never overwrite the source** without an explicit, separately-confirmed opt-in.

`REQ-LIB-081` `[v1.x]` **Audio cutter** (AIMP Android parity): trim a track to a selected region and export, with sample-accurate boundaries and lossless copy where the format permits.

---

## 10 · Eclipse Format Strings (Display-String Language)

> Winamp called this Advanced Title Formatting; AIMP calls them playlist macros; foobar2000's title formatting is the best-designed of the three, and its semantics are what we adopt. This is a small language, and specifying it precisely is cheap insurance against a decade of inconsistency.

### 10.1 Purpose and scope

`REQ-EFS-001` `[v1.0]` EFS is used for: playlist column contents, the window title, the tray tooltip, the Now Playing text, notification text, file-naming patterns in the converter and the file organiser, and skin `Text` component bindings (§11.4). One language, one engine, one test suite.

`REQ-EFS-002` `[v1.0]` EFS is **pure and total**: no side effects, no I/O, no loops, no recursion, no user-defined functions. Evaluation of any input MUST terminate in time linear in the length of the pattern. This is a hard requirement because patterns arrive inside downloadable skins.

### 10.2 Grammar

Canonical at `shared-spec/grammars/eclipse-format-strings.ebnf`:

```ebnf
pattern      = { element } ;
element      = literal | field-ref | function | optional-block ;

literal      = { any-char - ( "%" | "$" | "[" | "]" | "'" ) }
             | "'" { any-char - "'" } "'" ;      (* single quotes escape literals *)

field-ref    = "%" identifier [ ":" format-spec ] "%" ;
format-spec  = digit { digit }                    (* zero-pad width: %tracknumber:02% *)
             | "u" | "l" | "t" ;                  (* upper | lower | title case *)

function     = "$" identifier "(" [ arg { "," arg } ] ")" ;
arg          = pattern ;                          (* arguments are themselves patterns *)

optional-block = "[" pattern "]" ;
```

### 10.3 Semantics

`REQ-EFS-003` `[v1.0]` **Optional blocks are the core idea.** A `[...]` block evaluates to the empty string if **every** field reference inside it resolved to an absent value. If at least one resolved, the block renders in full. This is what makes one pattern work for tracks with and without, say, a disc number, with no conditionals.

```
%artist% - %title%[ (%album%)]
   Track with album    → "Radiohead - Idioteque (Kid A)"
   Track without album → "Radiohead - Idioteque"
```

`REQ-EFS-004` `[v1.0]` **Absent vs. empty.** A field is *absent* if the underlying value is NULL or the empty string after trimming. Absent propagates into optional-block logic; empty-string literals do not.

`REQ-EFS-005` `[v1.0]` Every function MUST return a string. There is no numeric type at the surface; numeric functions parse, compute, and re-render. Non-numeric input to a numeric function yields absent, never an error dialog.

`REQ-EFS-006` `[v1.0]` **Errors never throw.** A malformed pattern MUST render as much as it can and MUST expose a parse-error message to the *editor* UI, but a malformed pattern in a skin MUST NOT crash, block, or blank the whole surface — it renders the literal remainder.

### 10.4 Fields

`REQ-EFS-007` `[v1.0]` All `builtin-field` names from `REQ-PLS-011` MUST be available, plus these presentation and playback fields:

| Field | Meaning |
|---|---|
| `%length%` | Duration as `m:ss`, or `h:mm:ss` when ≥1 h |
| `%length_seconds%` | Duration in whole seconds |
| `%position%` / `%remaining%` | Current playback position / remaining, formatted like `%length%` |
| `%playing_state%` | `playing` \| `paused` \| `stopped` |
| `%queue_index%` / `%queue_total%` | 1-based position in the queue, and its size |
| `%list_index%` / `%list_total%` | 1-based position in the visible list, and its size |
| `%codec%`, `%bitrate%`, `%samplerate%`, `%bitdepth%`, `%channels%` | Technical |
| `%filesize_natural%` | e.g. `8.4 MB`, localised |
| `%rating_stars%` | Rating rendered as star glyphs |
| `%replaygain_applied%` | The gain actually applied, in dB, or absent |
| `%is_bitperfect%` | `1` when bit-perfect is active (§8.8) |

### 10.5 Function library

`REQ-EFS-008` `[v1.0]` The following MUST be implemented. This set is deliberately closed: adding a function is a spec change, not an implementation detail.

**Conditional**

| Function | Behaviour |
|---|---|
| `$if(cond,then[,else])` | `then` if `cond` is non-empty, else `else` |
| `$if2(a,b)` | `a` if non-empty, else `b` |
| `$if3(a,b,…)` | first non-empty argument |
| `$ifequal(x,y,then,else)` | numeric equality |
| `$ifgreater(x,y,then,else)` / `$ifless(...)` | numeric comparison |
| `$iflonger(s,n,then,else)` | string-length comparison |

**String**

`$upper(s)` · `$lower(s)` · `$title(s)` · `$trim(s)` · `$len(s)` · `$sub(s,start[,len])` · `$left(s,n)` · `$right(s,n)` · `$pad(s,n[,ch])` · `$padright(s,n[,ch])` · `$cut(s,n)` (truncate, no ellipsis) · `$abbr(s[,n])` (initials) · `$replace(s,find,repl)` · `$strchr(s,ch)` · `$strstr(s,sub)` · `$insert(s,ins,at)` · `$repeat(s,n)` (with `n ≤ 256`, enforced) · `$caps(s)` · `$meta_sep(field[,sep])` (join a multi-valued field)

**Numeric**

`$add(a,b,…)` · `$sub2(a,b)` · `$mul(a,b,…)` · `$div(a,b)` (÷0 → absent) · `$mod(a,b)` · `$min(a,…)` · `$max(a,…)` · `$num(n,width)` (zero-pad) · `$round(n[,dp])` · `$abs(n)`

**Time**

`$time(seconds)` → `m:ss` / `h:mm:ss` · `$timems(ms)` · `$date(iso[,fmt])` (locale-aware) · `$year(iso)` · `$age(iso)` (e.g. `3 days ago`, localised)

**Presentation**

`$char(codepoint)` · `$crlf()` · `$tab()` · `$progress(pos,total,width[,fill,empty])` → a text progress bar · `$stars(rating[,max])` · `$fixed(s,n)` (pad or cut to exactly `n`)

`REQ-EFS-009` `[v1.0]` `$repeat` and `$progress` MUST enforce a hard output cap (**4096 characters** per pattern evaluation). A pattern inside a downloaded skin MUST NOT be able to produce a gigabyte string.

### 10.6 Defaults and presets

`REQ-EFS-010` `[v1.0]` Ship these defaults, all user-editable, all resettable:

| Context | Default pattern |
|---|---|
| Playlist row (single-line) | `[%tracknumber:02%. ]%artist% - %title%` |
| Playlist row (two-line, primary) | `%title%` |
| Playlist row (two-line, secondary) | `%artist%[ — %album%]` |
| Window title | `$if(%playing_state%,%artist% - %title% — ,)Eclipse Player` |
| Tray tooltip | `%title%$crlf()%artist%$crlf()[%album%$crlf()]%position% / %length%` |
| Now Playing | `%title%` / `%artist%` / `[%album%[ (%year%)]]` |
| Notification | `%title%` / `%artist% — %album%` |
| Converter output name | `%albumartist%/%album%[ (%year%)]/[%discnumber%-]%tracknumber:02% %title%` |
| Grouping header | `%album%[ — %albumartist%][ (%year%)]` |

`REQ-EFS-011` `[v1.0]` The pattern editor MUST provide: a **live preview** against the currently selected track (or a built-in sample track), a searchable field/function reference, inline error highlighting, and one-click restore-to-default.

### 10.7 Conformance

`REQ-EFS-012` `[v1.0]` `shared-spec/conformance/efs-cases.json` MUST contain **at least 150** cases covering: every function, absent-value propagation, nested optional blocks, quoting and escaping, Unicode (including combining marks and RTL), numeric edge cases (division by zero, overflow, negatives), the output cap, and malformed-input recovery. **Both** the desktop and Android engines MUST pass every case identically. This file is the definition of the language; the implementations follow it.

---

## 11 · Theme & Skin Engine Specification

> This is differentiator #1 (§2.2). The v1.x document called `theme-schema.json` the "single source of truth" without defining it, and proposed shipping "optional custom QML layout regions" inside user-installable packages. That combination is an arbitrary-code-execution vulnerability in a privacy-first application. This section replaces it with a design that keeps genuine layout freedom and removes the code-execution surface.

### 11.1 Two-tier model and its rationale

`REQ-THM-001` `[v1.0]` There are exactly two tiers of customisation:

| | **Tier 1 — Theme** | **Tier 2 — Skin** |
|---|---|---|
| Contains | Design tokens only | A Theme, **plus** layout documents, icons, images, fonts |
| Format | `theme.json` | `.eclipseskin` package (§11.3) |
| Changes | Colour, type, spacing, radii, elevation, motion, opacity | All of Tier 1, **plus** component arrangement, sizing, visibility, and which surfaces exist |
| Code | None possible | **None possible** |
| Trust | Safe to apply from any source | Safe to apply from any source, after validation |
| Both platforms | Yes | Desktop `[v1.0]`, Android `[v1.x]` |

`REQ-THM-002` `[v1.0]` **Neither tier may contain executable code.** No QML, no JavaScript, no Maki-style script, no shader source, no expression language beyond EFS (§10) and the restricted `when:` comparison grammar (`REQ-THM-030`). This is non-negotiable and MUST be recorded as ADR `0003-no-code-in-skins.md`.

**Rationale** (this reasoning MUST appear in `docs/SKIN-AUTHORING.md` so authors understand the constraint rather than fighting it):

- QML is Turing-complete and has JavaScript semantics. A `.eclipseskin` containing QML could read the user's filesystem, open network sockets, exfiltrate the library database, or execute native code through imports. Sandboxing QML reliably is an unsolved problem, and "best effort" sandboxing in a privacy-first player is worse than an honest limitation.
- Winamp's modern skins shipped a scripting language (Maki). That is exactly the capability we are refusing, and Winamp's skin ecosystem was never audited for it.
- Declarative layout with a fixed component vocabulary covers the overwhelming majority of what skin authors actually do: rearrange, resize, restyle, hide, and swap imagery. It does not cover authors who want to invent new interactive behaviour — those authors are served by the **plugin SDK** (§16), which is an explicit, informed, separately-consented trust decision.

`REQ-THM-003` `[v1.0]` The distinction MUST be visible in the UI: applying a Theme is a one-click action with no warning; installing a Skin shows what the package contains (layout override? custom fonts? images? how many?) before it is applied. Users deserve to know what changed.

### 11.2 `theme-schema.json`

`REQ-THM-010` `[v1.0]` `shared-spec/schemas/theme-schema.json` is the single source of truth for Tier 1, is JSON Schema draft 2020-12, and MUST be consumed by the desktop validator, the Android validator, the `tools/theme-validate` CLI, and the skin editor. No implementation may accept a token the schema rejects, or reject one it accepts.

```jsonc
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://eclipse-player.org/schemas/theme/v1",
  "title": "Eclipse Player Theme",
  "type": "object",
  "required": ["schemaVersion", "id", "name", "version", "mode", "color", "typography"],
  "additionalProperties": false,

  "properties": {
    "schemaVersion": { "const": 1 },

    "id":      { "type": "string", "pattern": "^[a-z0-9]([a-z0-9-]{1,62}[a-z0-9])?$" },
    "name":    { "type": "string", "minLength": 1, "maxLength": 64 },
    "author":  { "type": "string", "maxLength": 128 },
    "version": { "type": "string", "pattern": "^\\d+\\.\\d+\\.\\d+$" },
    "license": { "type": "string", "maxLength": 64, "description": "SPDX identifier" },
    "homepage":{ "type": "string", "format": "uri", "maxLength": 512 },
    "description": { "type": "string", "maxLength": 512 },

    "minAppVersion": { "type": "string", "pattern": "^\\d+\\.\\d+\\.\\d+$" },
    "extends": { "type": "string", "description": "id of a built-in theme to inherit unset tokens from" },

    "mode": { "enum": ["light", "dark"] },

    // ---------------------------------------------------------------- colour
    // Every value is "#RRGGBB" or "#RRGGBBAA". Semantic names only:
    // authors never address a widget, they address a role.
    "color": {
      "type": "object",
      "required": ["background", "surface", "text", "accent", "border"],
      "additionalProperties": false,
      "properties": {
        "background": {
          "type": "object", "additionalProperties": false,
          "required": ["base"],
          "properties": {
            "base":    { "$ref": "#/$defs/color" },
            "sunken":  { "$ref": "#/$defs/color" },
            "raised":  { "$ref": "#/$defs/color" },
            "overlay": { "$ref": "#/$defs/color" },
            "scrim":   { "$ref": "#/$defs/color" }
          }
        },
        "surface": {
          "type": "object", "additionalProperties": false,
          "required": ["base"],
          "properties": {
            "base":     { "$ref": "#/$defs/color" },
            "hover":    { "$ref": "#/$defs/color" },
            "pressed":  { "$ref": "#/$defs/color" },
            "selected": { "$ref": "#/$defs/color" },
            "disabled": { "$ref": "#/$defs/color" }
          }
        },
        "text": {
          "type": "object", "additionalProperties": false,
          "required": ["primary", "secondary"],
          "properties": {
            "primary":   { "$ref": "#/$defs/color" },
            "secondary": { "$ref": "#/$defs/color" },
            "tertiary":  { "$ref": "#/$defs/color" },
            "disabled":  { "$ref": "#/$defs/color" },
            "inverse":   { "$ref": "#/$defs/color" },
            "onAccent":  { "$ref": "#/$defs/color" },
            "link":      { "$ref": "#/$defs/color" }
          }
        },
        "accent": {
          "type": "object", "additionalProperties": false,
          "required": ["base"],
          "properties": {
            "base":    { "$ref": "#/$defs/color" },
            "hover":   { "$ref": "#/$defs/color" },
            "pressed": { "$ref": "#/$defs/color" },
            "subtle":  { "$ref": "#/$defs/color" },
            "muted":   { "$ref": "#/$defs/color" }
          }
        },
        "border": {
          "type": "object", "additionalProperties": false,
          "required": ["base"],
          "properties": {
            "base":   { "$ref": "#/$defs/color" },
            "subtle": { "$ref": "#/$defs/color" },
            "strong": { "$ref": "#/$defs/color" },
            "focus":  { "$ref": "#/$defs/color" }
          }
        },
        "state": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "success": { "$ref": "#/$defs/color" },
            "warning": { "$ref": "#/$defs/color" },
            "error":   { "$ref": "#/$defs/color" },
            "info":    { "$ref": "#/$defs/color" }
          }
        },
        "playback": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "progress":       { "$ref": "#/$defs/color" },
            "progressTrack":  { "$ref": "#/$defs/color" },
            "buffered":       { "$ref": "#/$defs/color" },
            "waveform":       { "$ref": "#/$defs/color" },
            "waveformPlayed": { "$ref": "#/$defs/color" },
            "peakMeter":      { "$ref": "#/$defs/color" },
            "peakMeterClip":  { "$ref": "#/$defs/color" }
          }
        },
        "visualizer": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "palette": {
              "type": "array", "minItems": 1, "maxItems": 16,
              "items": { "$ref": "#/$defs/color" }
            },
            "background": { "$ref": "#/$defs/color" }
          }
        }
      }
    },

    // ------------------------------------------------------------ typography
    "typography": {
      "type": "object",
      "required": ["fontFamily", "scale"],
      "additionalProperties": false,
      "properties": {
        "fontFamily": {
          "type": "object", "additionalProperties": false,
          "required": ["sans"],
          "properties": {
            "sans": { "$ref": "#/$defs/fontStack" },
            "mono": { "$ref": "#/$defs/fontStack" },
            "display": { "$ref": "#/$defs/fontStack" }
          }
        },
        "baseSize": { "type": "number", "minimum": 8, "maximum": 24, "default": 14 },
        "scale": {
          "type": "object",
          "required": ["body", "label"],
          "additionalProperties": false,
          "properties": {
            "display":  { "$ref": "#/$defs/typeStyle" },
            "headline": { "$ref": "#/$defs/typeStyle" },
            "title":    { "$ref": "#/$defs/typeStyle" },
            "body":     { "$ref": "#/$defs/typeStyle" },
            "label":    { "$ref": "#/$defs/typeStyle" },
            "caption":  { "$ref": "#/$defs/typeStyle" },
            "mono":     { "$ref": "#/$defs/typeStyle" }
          }
        }
      }
    },

    // ----------------------------------------------------------------- shape
    "shape": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "radius": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "none": { "const": 0 },
            "sm":   { "type": "number", "minimum": 0, "maximum": 64 },
            "md":   { "type": "number", "minimum": 0, "maximum": 64 },
            "lg":   { "type": "number", "minimum": 0, "maximum": 64 },
            "xl":   { "type": "number", "minimum": 0, "maximum": 64 },
            "full": { "const": 9999 }
          }
        },
        "borderWidth": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "hairline": { "type": "number", "minimum": 0, "maximum": 8 },
            "thin":     { "type": "number", "minimum": 0, "maximum": 8 },
            "thick":    { "type": "number", "minimum": 0, "maximum": 8 }
          }
        }
      }
    },

    // --------------------------------------------------------------- spacing
    "spacing": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "unit":  { "type": "number", "minimum": 1, "maximum": 16, "default": 4 },
        "scale": {
          "type": "array", "minItems": 4, "maxItems": 16,
          "items": { "type": "number", "minimum": 0, "maximum": 256 }
        },
        "density": { "enum": ["compact", "comfortable", "spacious"], "default": "comfortable" }
      }
    },

    // ------------------------------------------------------------- elevation
    "elevation": {
      "type": "array", "maxItems": 6,
      "items": {
        "type": "object", "additionalProperties": false,
        "required": ["offsetY", "blur", "color"],
        "properties": {
          "offsetX": { "type": "number", "minimum": -64, "maximum": 64, "default": 0 },
          "offsetY": { "type": "number", "minimum": -64, "maximum": 64 },
          "blur":    { "type": "number", "minimum": 0,   "maximum": 128 },
          "spread":  { "type": "number", "minimum": -64, "maximum": 64, "default": 0 },
          "color":   { "$ref": "#/$defs/color" }
        }
      }
    },

    // ---------------------------------------------------------------- motion
    "motion": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "duration": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "instant": { "type": "integer", "minimum": 0, "maximum": 2000 },
            "fast":    { "type": "integer", "minimum": 0, "maximum": 2000 },
            "normal":  { "type": "integer", "minimum": 0, "maximum": 2000 },
            "slow":    { "type": "integer", "minimum": 0, "maximum": 2000 }
          }
        },
        "easing": {
          "type": "object", "additionalProperties": false,
          "properties": {
            "standard":   { "$ref": "#/$defs/cubicBezier" },
            "decelerate": { "$ref": "#/$defs/cubicBezier" },
            "accelerate": { "$ref": "#/$defs/cubicBezier" },
            "emphasized": { "$ref": "#/$defs/cubicBezier" }
          }
        }
      }
    },

    "opacity": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "disabled": { "$ref": "#/$defs/unitInterval" },
        "hover":    { "$ref": "#/$defs/unitInterval" },
        "pressed":  { "$ref": "#/$defs/unitInterval" },
        "scrim":    { "$ref": "#/$defs/unitInterval" },
        "ghost":    { "$ref": "#/$defs/unitInterval" }
      }
    },

    // ----------------------------------------------------------------- icons
    "icons": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "setId":       { "type": "string", "pattern": "^[a-z0-9-]{1,64}$" },
        "style":       { "enum": ["outline", "filled", "duotone"] },
        "strokeWidth": { "type": "number", "minimum": 0.5, "maximum": 4 },
        "sizeScale":   { "type": "number", "minimum": 0.5, "maximum": 2 }
      }
    },

    "assets": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "background":     { "$ref": "#/$defs/assetRef" },
        "backgroundFit":  { "enum": ["cover", "contain", "tile", "stretch", "center"] },
        "backgroundOpacity": { "$ref": "#/$defs/unitInterval" },
        "logo":           { "$ref": "#/$defs/assetRef" }
      }
    },

    // ------------------------------------------------------- accessibility
    // Enforced by the loader, not merely declared. See REQ-THM-016.
    "a11y": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "contrastTarget":       { "enum": ["AA", "AAA"], "default": "AA" },
        "respectsReducedMotion":{ "type": "boolean", "default": true },
        "minTouchTarget":       { "type": "number", "minimum": 24, "maximum": 96, "default": 44 },
        "focusRingWidth":       { "type": "number", "minimum": 1, "maximum": 8, "default": 2 }
      }
    }
  },

  "$defs": {
    "color":  { "type": "string", "pattern": "^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$" },
    "unitInterval": { "type": "number", "minimum": 0, "maximum": 1 },
    "cubicBezier": {
      "type": "array", "minItems": 4, "maxItems": 4,
      "items": { "type": "number", "minimum": -2, "maximum": 2 }
    },
    "fontStack": {
      "type": "array", "minItems": 1, "maxItems": 8,
      "items": { "type": "string", "maxLength": 64 }
    },
    "typeStyle": {
      "type": "object", "additionalProperties": false,
      "required": ["size"],
      "properties": {
        "size":          { "type": "number", "minimum": 6, "maximum": 96 },
        "lineHeight":    { "type": "number", "minimum": 0.8, "maximum": 3 },
        "weight":        { "type": "integer", "minimum": 100, "maximum": 900 },
        "letterSpacing": { "type": "number", "minimum": -2, "maximum": 8 },
        "transform":     { "enum": ["none", "uppercase", "lowercase", "capitalize"] }
      }
    },
    "assetRef": {
      "type": "string",
      "pattern": "^(images|icons|fonts)/[A-Za-z0-9._-]+$",
      "description": "Package-relative path. Absolute paths, URLs, and '..' are rejected."
    }
  }
}
```

`REQ-THM-011` `[v1.0]` **Token inheritance.** A theme MAY set `extends` to a built-in theme id. Unset tokens inherit from it; unset tokens with no `extends` inherit from the built-in **Dark** or **Light** theme according to `mode`. An author MUST be able to ship a five-line theme that only changes the accent colour.

`REQ-THM-012` `[v1.0]` The `assetRef` pattern is a security control, not a convenience: it forbids absolute paths, URLs, and traversal. A theme MUST NOT be able to reference anything outside its own package.

### 11.3 `.eclipseskin` package format

`REQ-THM-015` `[v1.0]` A skin is a ZIP archive (deflate or stored; **no** encryption, **no** other compression methods) with the extension `.eclipseskin` and this layout:

```
my-skin.eclipseskin  (ZIP)
├── manifest.json          REQUIRED — validated against skin-manifest.schema.json
├── theme.json             REQUIRED — validated against theme-schema.json
├── LICENSE                REQUIRED — the skin's own licence text
├── preview.png            REQUIRED — 1280×800 gallery preview
├── layout/                optional
│   ├── main-window.eclayout
│   ├── now-playing.eclayout
│   ├── mini-player.eclayout
│   └── library.eclayout
├── icons/                 optional — SVG only, sanitised (REQ-THM-042)
│   └── *.svg
├── images/                optional — PNG / WebP / JPEG
│   └── *
├── fonts/                 optional — TTF / OTF / WOFF2
│   ├── *.ttf
│   └── LICENSE-fonts      REQUIRED if fonts/ is non-empty
└── i18n/                  optional — translations for skin-authored strings
    └── <lang>.json
```

`REQ-THM-016` `[v1.0]` `manifest.json` MUST declare, in addition to identity fields: `capabilities` (which of `theme`, `layout`, `icons`, `images`, `fonts` it uses), `targetSurfaces` (which layouts it overrides), `minAppVersion`, and `checksums` (SHA-256 of every other file in the package). The installer MUST verify every checksum and MUST refuse a package containing a file not listed in `checksums`.

`REQ-THM-017` `[v1.0]` **Hard limits, enforced before extraction:**

| Limit | Value | Reason |
|---|---|---|
| Total uncompressed size | 32 MiB | Zip bomb |
| Entry count | 2,000 | Zip bomb / inode exhaustion |
| Compression ratio, any single entry | 100 : 1 | Zip bomb |
| Single file uncompressed | 8 MiB | Memory |
| Path depth | 4 | Sanity |
| Path length | 200 bytes | Cross-platform safety |
| Image dimensions | 8192 × 8192 | Decoder memory (`REQ-LIB-068`) |
| Font count | 8 | Memory, licence review |
| Layout documents | 16 | Parse cost |

`REQ-THM-018` `[v1.0]` **Zip-slip prevention.** Every entry path MUST be: relative, normalised, free of `..` segments, free of absolute prefixes and drive letters, free of NUL and control characters, not a symlink, not a hard link, not a device node, and confined to one of the permitted top-level directories. Entries MUST be extracted by **resolving the final path and asserting it is inside the destination directory**, never by trusting the archive's own path. This is a fuzz target (§21.6).

`REQ-THM-019` `[v1.0]` Extraction MUST be **atomic**: extract to a temporary directory, validate everything, then move into place. A failed or malicious package MUST leave no residue.

### 11.4 Layout DSL (`.eclayout`)

`REQ-THM-025` `[v1.0]` Layout documents are JSON, validated against `shared-spec/schemas/layout.schema.json`, and interpreted — never compiled or evaluated as code. The interpreter maps them onto QML `Item` trees on desktop and onto Compose composables on Android.

`REQ-THM-026` `[v1.0]` **Closed component vocabulary.** Only these components exist. Adding one is a schema-version change:

| Layout | Content | Interactive | Media |
|---|---|---|---|
| `Stack` (z-order) | `Text` | `Button` | `AlbumArt` |
| `Row` | `Icon` | `ToggleButton` | `Visualizer` |
| `Column` | `Image` | `Slider` | `SeekBar` |
| `Grid` | `Marquee` | `VolumeControl` | `PeakMeter` |
| `Panel` | `Divider` | `TransportBar` | `WaveformView` |
| `Spacer` | `Badge` | `SearchField` | `LyricsView` |
| `ScrollArea` | `Rating` | `TabBar` | |
| `SplitPane` | `ProgressBar` | `ListView` | |

`REQ-THM-027` `[v1.0]` **Bindings are read-only paths** into a whitelisted state model. An author writes `{ "bind": "track.title" }` or `{ "efs": "%artist% - %title%" }`. The whitelist MUST be exhaustively documented in `docs/SKIN-AUTHORING.md` and MUST expose nothing beyond presentation state — no filesystem paths beyond what a UI already shows, no settings values, no library queries.

`REQ-THM-028` `[v1.0]` **Actions are enum-only.** An author writes `{ "action": "player.playPause" }`. The action set is closed and mirrors the command registry from §13.2. An unknown action MUST fail validation at install time, not silently do nothing at runtime.

`REQ-THM-029` `[v1.0]` **No arithmetic, no expressions** beyond: the EFS engine (§10, itself total and capped), and a restricted `when:` predicate for conditional visibility.

`REQ-THM-030` `[v1.0]` `when:` grammar — deliberately a strict subset of the smart-playlist comparison grammar, with no functions and no nesting beyond one level of `and`/`or`/`not`:

```ebnf
when      = clause { ("and"|"or") clause } ;
clause    = [ "not" ] atom ;
atom      = state-path operator literal | state-path ;
operator  = "==" | "!=" | ">" | "<" | ">=" | "<=" ;
```

Example: `"when": "player.state == playing and track.hasArtwork"`.

`REQ-THM-031` `[v1.0]` Worked example, illustrating the whole surface:

```jsonc
{
  "schemaVersion": 1,
  "surface": "mini-player",
  "minSize": { "width": 320, "height": 96 },
  "root": {
    "type": "Row",
    "padding": "md",
    "spacing": "sm",
    "background": "color.background.raised",
    "children": [
      {
        "type": "AlbumArt",
        "size": { "width": 72, "height": 72 },
        "radius": "md",
        "fallback": "placeholder"
      },
      {
        "type": "Column",
        "sizing": { "width": "fill" },
        "spacing": "xs",
        "justify": "center",
        "children": [
          {
            "type": "Marquee",
            "efs": "%title%",
            "style": "typography.scale.title",
            "color": "color.text.primary"
          },
          {
            "type": "Text",
            "efs": "%artist%[ — %album%]",
            "style": "typography.scale.caption",
            "color": "color.text.secondary",
            "overflow": "ellipsis"
          },
          {
            "type": "SeekBar",
            "showBuffered": true,
            "height": 4
          }
        ]
      },
      {
        "type": "TransportBar",
        "buttons": ["previous", "playPause", "next"],
        "iconSize": 24
      },
      {
        "type": "Visualizer",
        "style": "bars",
        "sizing": { "width": 64, "height": "fill" },
        "when": "settings.showMiniVisualizer"
      }
    ]
  }
}
```

`REQ-THM-032` `[v1.0]` **Graceful degradation is mandatory.** If a layout omits a surface, the built-in layout for that surface is used. If a layout omits a *control* the application considers essential (play/pause, seek, volume, close), the interpreter MUST still expose it — via the keyboard shortcut, the context menu, and the OS media controls — and MUST warn the author at install time. A skin must never be able to make the player unusable or unclosable.

`REQ-THM-033` `[v1.0]` **Resource budget per layout.** Maximum 500 component instances, maximum nesting depth 24, maximum 64 bindings per component tree. Exceeding a budget fails validation with a precise message naming the offending node.

`REQ-THM-034` `[v1.0]` The interpreter MUST be **incremental and non-blocking**: parsing and instantiating a layout MUST NOT block the UI thread for more than 16 ms; larger trees are built across frames with a placeholder shown.

### 11.5 Validation and trust

`REQ-THM-040` `[v1.0]` Validation pipeline, in order, with the package rejected at the first failure and a precise, human-readable reason given:

1. ZIP structural integrity; limits from `REQ-THM-017`.
2. Path safety for every entry (`REQ-THM-018`).
3. `manifest.json` against its schema; checksum verification of every file.
4. `theme.json` against `theme-schema.json`.
5. **Contrast enforcement** (`REQ-THM-041`).
6. Every `.eclayout` against `layout.schema.json`, plus component/binding/action whitelist checks and resource budgets.
7. Every SVG sanitised (`REQ-THM-042`).
8. Every raster image probed for dimensions **before** decode.
9. Fonts: format check, and `fonts/LICENSE-fonts` presence.
10. Every EFS pattern parsed and its output cap verified.

`REQ-THM-041` `[v1.0]` **Contrast is enforced, not requested.** The v1.x document claimed "contrast ratios enforced by the design tokens themselves" without a mechanism. The mechanism is: the loader computes the WCAG 2.2 contrast ratio for every (text colour, background colour) pair the theme can produce, and:

| Ratio | Behaviour |
|---|---|
| ≥ 4.5 : 1 normal text, ≥ 3 : 1 large text (≥18.66 px regular / ≥14 px bold) | Accept |
| Below the AA floor | Accept **with a warning**, listing the offending pairs, unless the user has enabled *Enforce accessible contrast* in settings |
| *Enforce accessible contrast* enabled (default **off**, forced **on** by the high-contrast theme and by the OS high-contrast setting) | **Reject**, or offer auto-correction that nudges lightness until AA is met |

`REQ-THM-042` `[v1.0]` **SVG sanitisation.** Strip, and reject if present: `<script>`, `<foreignObject>`, `<use>` with an external reference, `<image>` with a non-`data:` href, any `on*` event attribute, any `href`/`xlink:href` that is not an internal fragment, `<style>` with `@import`, and any external entity or DOCTYPE declaration. Cap the element count at 10,000. Parse with entity expansion disabled (billion-laughs defence).

`REQ-THM-043` `[v1.0]` **Trust model.** There is deliberately **no signature requirement** for skins, because §11.1 removed the code-execution surface that would make signing necessary. Instead:

- Skins from the curated gallery are marked **Verified** (reviewed by maintainers).
- Skins installed from a file are marked **Unverified** and their capabilities are shown before applying.
- The verification badge is about *provenance and taste*, never about safety — safety comes from validation, which is identical for both.
- `docs/SKIN-AUTHORING.md` MUST state this explicitly so nobody mistakes "unverified" for "dangerous".

`REQ-THM-044` `[v1.0]` Installed skins MUST be **fully removable**, leaving no files behind, and removing the active skin MUST fall back to the default without restarting the app.

### 11.6 Loading, hot-reload, and compatibility

`REQ-THM-050` `[v1.0]` Applying a theme or skin MUST take effect **without restarting the application**, and MUST NOT interrupt playback by even one sample. Skin switching MUST be visually smooth: cross-fade the affected surfaces over `motion.duration.normal`.

`REQ-THM-051` `[v1.0]` **Hot-reload for authors:** a developer mode that watches an unpacked skin directory and reapplies on change within 300 ms, with validation errors shown as an in-app overlay rather than a dialog. Without this, skin authoring is unbearable, and skin authoring is the ecosystem.

`REQ-THM-052` `[v1.0]` **Compatibility policy.**

| Situation | Behaviour |
|---|---|
| Skin's `schemaVersion` < current | Load through a documented migration path; warn nothing to the user. |
| Skin's `schemaVersion` > current | Refuse, and state the app version needed. |
| Skin's `minAppVersion` > app version | Refuse, and state the app version needed. |
| Unknown token present | **Ignore it and warn the author** in dev mode. Forward compatibility must not be brittle. |
| Unknown component / action / binding | **Reject at install time.** Silent no-ops produce unexplainable skins. |

`REQ-THM-053` `[v1.0]` `theme-schema.json` and `layout.schema.json` are versioned artefacts under `shared-spec/`, and any change to them MUST follow §26.5's schema-versioning rules.

### 11.7 Authoring tooling and gallery

`REQ-THM-060` `[v1.0]` `tools/theme-validate` — a CLI that validates a theme, a layout, or a full `.eclipseskin`, and prints every error with a JSON Pointer to the offending node. It MUST run in CI over all bundled skins, and MUST be documented for authors.

`REQ-THM-061` `[v1.0]` **In-app skin browser and installer** with: bundled gallery, import from file, drag-and-drop of a `.eclipseskin`, live preview before applying, capability disclosure (`REQ-THM-003`), and one-click removal.

`REQ-THM-062` `[v1.x]` **`tools/skin-editor`** — a first-party visual authoring tool, following AIMP's precedent of shipping a Skin Editor rather than only a format. Requirements: token editing with live preview, layout tree editing with drag-and-drop, contrast checking inline, EFS pattern preview, package validation and export, and template projects for each surface.

`REQ-THM-063` `[v1.x]` **Icon packs** as a separate installable artefact (`.eclipseicons`), following AIMP's addon taxonomy, so an icon set can be reused across themes.

`REQ-THM-064` `[v1.x]` A public gallery with screenshots, search, and author pages. Screenshots MUST be generated automatically from `preview.png` plus rendered captures, and packages MUST be deduplicated by content hash.

### 11.8 Built-in themes and skins

`REQ-THM-070` `[v1.0]` Ship these **four themes**, all authored against the same schema authors use (dogfooding is mandatory — no privileged internal path):

| Theme | Notes |
|---|---|
| **Eclipse Light** | Default in light mode. |
| **Eclipse Dark** | Default in dark mode, and the app default overall. |
| **Eclipse AMOLED** | True black `#000000` backgrounds, for OLED panels. |
| **Eclipse High Contrast** | Meets WCAG **AAA** (7:1) throughout; forces `enforceAccessibleContrast`; visible focus rings; no reliance on colour alone. |

`REQ-THM-071` `[v1.0]` Ship **three built-in skins** exercising genuinely different layouts, to prove the layout tier is real:

| Skin | Character |
|---|---|
| **Eclipse Modern** | The reference layout: sidebar navigation, large Now Playing, grid library. |
| **Eclipse Compact** | Single-column, dense list, minimal chrome — the "small window on a second monitor" skin. |
| **Eclipse Console** | Monospaced, text-forward, keyboard-first, ASCII progress bars via `$progress()` — proves EFS and the layout DSL together can produce a radically different product feel. |

`REQ-THM-072` `[v1.0]` Every built-in theme and skin MUST pass `tools/theme-validate` in CI, MUST pass contrast checks at the AA floor (AAA for High Contrast), and MUST be screenshot-tested at 3 window sizes and 2 DPI scales.

---

## 12 · UI/UX Specification

### 12.1 Design tokens — canonical values

`REQ-UIX-001` `[v1.0]` `shared-spec/design-system/tokens.json` holds these values. Both platforms MUST consume that file (Android via a build-time code generator, desktop via a `.qrc`-embedded resource) rather than duplicating numbers.

**Spacing** — 4 px base unit:
```
xs=4   sm=8   md=12   lg=16   xl=24   2xl=32   3xl=48   4xl=64
```

**Type scale** — 14 px base, 1.2 ratio, values in px:

| Token | Size | Line height | Weight | Letter spacing |
|---|---|---|---|---|
| `display` | 34 | 1.15 | 700 | −0.5 |
| `headline` | 24 | 1.25 | 600 | −0.25 |
| `title` | 18 | 1.3 | 600 | 0 |
| `body` | 14 | 1.5 | 400 | 0 |
| `label` | 13 | 1.4 | 500 | 0.1 |
| `caption` | 12 | 1.35 | 400 | 0.2 |
| `mono` | 13 | 1.45 | 400 | 0 |

**Radius:** `sm=4 md=8 lg=12 xl=16 full=9999`

**Elevation:** 5 levels, ascending `offsetY / blur / alpha`: `1/2/0.10`, `2/4/0.12`, `4/8/0.14`, `8/16/0.16`, `16/32/0.20`.

**Motion:**

| Token | Duration | Use |
|---|---|---|
| `instant` | 80 ms | State feedback on press |
| `fast` | 150 ms | Hover, small transitions |
| `normal` | 250 ms | Panel and view transitions, skin cross-fade |
| `slow` | 400 ms | Full-screen transitions |

| Easing | Curve |
|---|---|
| `standard` | `cubic-bezier(0.2, 0.0, 0.0, 1.0)` |
| `decelerate` | `cubic-bezier(0.0, 0.0, 0.2, 1.0)` |
| `accelerate` | `cubic-bezier(0.4, 0.0, 1.0, 1.0)` |
| `emphasized` | `cubic-bezier(0.2, 0.0, 0.0, 1.0)` with `slow` duration |

`REQ-UIX-002` `[v1.0]` **Motion discipline.** Animation exists to explain a change of state, never to decorate. Forbidden: animated album-art parallax on scroll, bouncing playback buttons, animated gradients behind text, anything that animates continuously while idle. Every animation MUST be interruptible and MUST honour reduced-motion (`REQ-UIX-060`).

### 12.2 Desktop surfaces

`REQ-UIX-010` `[v1.0]` **Main window** — resizable, with a persisted geometry per display configuration (so unplugging a monitor does not lose the window), a minimum size of 640 × 480, and these regions: title/menu chrome (Widgets), navigation sidebar, content area, Now Playing bar, status bar. Panels MUST be resizable with persisted splitter positions, and individually collapsible.

`REQ-UIX-011` `[v1.0]` **Library views** MUST include: Albums (grid and list), Artists, Album Artists, Genres, Years, Folders (a real filesystem tree, not a flattened list), Composers, Playlists, Favourites, Recently Added, Recently Played, Most Played, Never Played, and Missing. Every view MUST support sorting, grouping, and column configuration where applicable, with the state persisted per view.

`REQ-UIX-012` `[v1.0]` **Full-screen Now Playing** with large artwork, EFS-driven metadata, a live visualizer, synced lyrics, and a seek bar with a waveform overview where analysis is available. It MUST be reachable by one shortcut and dismissible by `Esc`.

`REQ-UIX-013` `[v1.0]` **Mini-player** — a compact always-available surface with transport, seek, volume, and metadata, with an **always-on-top** toggle, snapping to screen edges, and a persisted position.

`REQ-UIX-014` `[v1.x]` **Windowshade mode** — collapse the main window to a single strip showing only transport and a scrolling title, following Winamp's model. This is a distinct interaction pattern with real fans and no modern equivalent.

`REQ-UIX-015` `[v1.x]` **Detachable panels** — playlist, equalizer, and visualizer MUST be able to float as separate windows, dock back to the main window with magnetic snapping, and remember their state.

`REQ-UIX-016` `[v1.0]` **Dialogs** MUST exist for: Preferences (tabbed, searchable), Tag Editor, Equalizer, Converter `[v1.x]`, Skin Browser, Duplicate Finder, ReplayGain Scanner, About/Licences, and Technical Info for the current track. All MUST be keyboard-navigable and resizable where content warrants.

`REQ-UIX-017` `[v1.0]` **Track technical-info panel** MUST show: full path, container, codec, bitrate (and whether VBR), sample rate, bit depth, channels, duration in samples and in time, file size, tag formats present, gapless metadata source and skip values, ReplayGain values and whether scanned or read, artwork source and dimensions, and cue-sheet slice boundaries where applicable. This panel is where §8.4's honesty requirements surface to the user.

`REQ-UIX-018` `[v1.0]` **Drag-and-drop** MUST work in all of these directions: files/folders from the file manager into any playlist or the queue; tracks between playlists; tracks reordered within a playlist; a `.eclipseskin` onto the window to install it; a playlist file onto the window to import it.

`REQ-UIX-019` `[v1.0]` **HiDPI.** The UI MUST be correct at 100 %, 125 %, 150 %, 175 %, 200 %, and 250 % scaling, including mixed-DPI multi-monitor setups where the window is dragged between displays. All iconography MUST be vector.

### 12.3 Android surfaces

`REQ-UIX-025` `[v1.0]` Layouts for: phone portrait and landscape, tablet (two-pane), foldable (both folded and unfolded, honouring the hinge via `WindowSizeClass` and `FoldingFeature`), and Android Auto (§15).

`REQ-UIX-026` `[v1.0]` **Now Playing** MUST support a swipe-to-dismiss bottom-sheet expansion from the mini bar, with the artwork, transport, seek, queue, and lyrics reachable without leaving the sheet.

`REQ-UIX-027` `[v1.0]` **Notification** MUST use `MediaStyle` with artwork, up to 5 actions, correct `PlaybackState` for the lock screen, and a seek bar on API 29+. It MUST NOT be dismissible while playing, and MUST be dismissible when paused.

`REQ-UIX-028` `[v1.0]` **Home-screen widgets** in at least three sizes (2×1 transport, 4×1 with metadata, 4×2 with artwork), all themed by the active theme, all updating without waking the whole app.

`REQ-UIX-029` `[v1.x]` **Quick Settings tile** for play/pause, and a **Wear OS** companion is `[v2]`.

`REQ-UIX-030` `[v1.0]` **Predictive back** support, edge-to-edge layout with correct insets, and no content ever hidden behind system bars or the display cutout.

### 12.4 Visualizers

`REQ-UIX-035` `[v1.0]` Data for visualisers MUST come from a **lock-free spectrum tap** in the DSP chain (`REQ-GEN-053`), never by re-reading or re-decoding audio. The tap provides: raw PCM window, FFT magnitude bins, per-channel peak and RMS, and a beat-detection pulse.

`REQ-UIX-036` `[v1.0]` FFT configuration: **2048-point** window, Hann window, 50 % overlap, magnitude in dB, with a configurable smoothing time constant (default 0.7 attack / 0.85 release) and logarithmic frequency banding for display.

`REQ-UIX-037` `[v1.0]` Native visualizers, rendered via the Qt Quick scene graph on desktop and Compose/`AGSL` on Android: **Spectrum bars**, **Oscilloscope**, **Waveform**, and **Peak/VU meters**. All MUST use `color.visualizer.palette` from the active theme.

`REQ-UIX-038` `[v1.0]` **projectM integration** (§6.7) MUST provide MilkDrop `.milk` preset support: load a preset directory, browse and search presets, lock a preset, shuffle with a configurable interval, and hardware-accelerated rendering. Preset load failures MUST be reported and skipped, never fatal.

`REQ-UIX-039` `[v1.0]` Visualizer rendering MUST be **frame-budget-aware**: target 60 fps, automatically reduce resolution or bar count when the frame budget is exceeded, and **pause entirely** when the surface is not visible, when the window is minimised, or when on battery saver. A visualizer that drains a laptop battery while hidden is a bug.

`REQ-UIX-040` `[v1.0]` Visualisers MUST be disableable globally, and MUST be off by default in the Android Auto surface (§15) and in any always-on-top mini-player at small sizes.

### 12.5 Lyrics

`REQ-UIX-045` `[v1.0]` Sources, in precedence order: embedded (`USLT`, `LYRICS`, `©lyr`) → sidecar `.lrc` next to the audio file → sidecar `.txt` → the `lyrics` table (user-edited or previously fetched) → online lookup **if enabled** (§17.3).

`REQ-UIX-046` `[v1.0]` **LRC support** MUST include: standard time tags `[mm:ss.xx]`, multiple time tags on one line, metadata tags (`[ti:]`, `[ar:]`, `[al:]`, `[by:]`, `[offset:]`), and **enhanced (word-level) LRC** `<mm:ss.xx>` for karaoke-style intra-line highlighting.

`REQ-UIX-047` `[v1.0]` Rendering: the active line centred and emphasised, adjacent lines dimmed, smooth scrolling honouring reduced-motion, tap/click a line to seek to it, and a per-track **offset adjustment** in ±100 ms steps that persists to `lyrics.offset_ms`.

`REQ-UIX-048` `[v1.0]` The LRC parser MUST be defensive against malformed timestamps, out-of-order lines, negative offsets, and enormous files (cap at 1 MiB), and is a fuzz target (§21.6).

`REQ-UIX-049` `[v1.x]` In-app lyrics editing, including tapping along with playback to generate timestamps, saved as a sidecar `.lrc` or embedded on request.

### 12.6 Accessibility

`REQ-UIX-055` `[v1.0]` Target: **WCAG 2.2 level AA** for the application UI. This is an acceptance criterion, not an aspiration; §23.7 says how it is tested.

`REQ-UIX-056` `[v1.0]` **Screen readers.** Every interactive element MUST have an accessible name, role, value, and state. Verified against: **NVDA** and **Narrator** on Windows, **Orca** on Linux (via Qt's AT-SPI2 bridge), and **TalkBack** on Android. Qt Quick items MUST set `Accessible.*` properties; Compose MUST use `semantics {}`.

`REQ-UIX-057` `[v1.0]` **Keyboard-only operation.** Every function MUST be reachable without a pointer. Focus order MUST follow visual order. Focus MUST always be visible (a ring of `a11y.focusRingWidth` in `color.border.focus`). No focus traps: every dialog and popover MUST be escapable with `Esc`, and focus MUST return to the invoking element.

`REQ-UIX-058` `[v1.0]` **Contrast** per `REQ-THM-041`. Additionally, information MUST NOT be conveyed by colour alone — playback state, selection, ratings, and error states MUST each have a non-colour indicator (icon, glyph, text, or shape).

`REQ-UIX-059` `[v1.0]` **Text scaling.** The UI MUST remain usable and MUST not clip or truncate essential text at **200 %** text scale, honouring the OS setting (Windows text scaling, GNOME `text-scaling-factor`, Android `fontScale`). Layouts MUST reflow rather than overflow.

`REQ-UIX-060` `[v1.0]` **Reduced motion.** Honour `prefers-reduced-motion` equivalents: Windows "Show animations", GNOME `gtk-enable-animations`, Android "Remove animations". When set: disable all non-essential animation, replace cross-fades with instant swaps, and stop continuous visualizer motion unless the user explicitly re-enables it.

`REQ-UIX-061` `[v1.0]` **Touch targets** ≥ `a11y.minTouchTarget` (default 44 px) on touch-capable surfaces, and ≥ 24 px on pointer-only surfaces.

`REQ-UIX-062` `[v1.0]` No content may flash more than **3 times per second** — this includes visualizers, which MUST be clamped, and beat-synchronised UI effects, which MUST be rate-limited.

`REQ-UIX-063` `[v1.0]` Every icon-only control MUST have a tooltip **and** an accessible name, and the two MUST be the same string so they cannot drift.

### 12.7 Internationalisation and localisation

`REQ-UIX-070` `[v1.0]` **Every** user-visible string MUST be externalised from the first commit that introduces it. A hard-coded string is a build failure: CI MUST run a check that flags string literals in UI code outside the translation call.

`REQ-UIX-071` `[v1.0]` Pipeline:

| Platform | Mechanism |
|---|---|
| Desktop | Qt Linguist — `tr()` / `qsTr()` → `lupdate` → `.ts` → `lrelease` → `.qm`, loaded by locale with a fallback chain. |
| Android | Standard `strings.xml` resources with `plurals` and `string-array`. |

`REQ-UIX-072` `[v1.0]` **Ship English (`en`) and Indonesian (`id`) at minimum**, both complete, both reviewed by a speaker. Additional locales are community-contributed with a documented process.

`REQ-UIX-073` `[v1.0]` **Pluralisation** MUST use the platform's plural-forms machinery (`tr(..., n)` / `<plurals>`), never string concatenation or `if (n == 1)`. Languages with more than two plural forms MUST work.

`REQ-UIX-074` `[v1.0]` **Locale-aware formatting** for dates, times, numbers, durations, and file sizes. Never hand-format. `%length%` and friends in EFS MUST respect the locale.

`REQ-UIX-075` `[v1.0]` **RTL support.** Layouts MUST mirror correctly for RTL locales (Arabic, Hebrew, Persian). This MUST be verified with a pseudo-locale in CI even before an RTL translation exists, because retrofitting RTL is far more expensive than building for it.

`REQ-UIX-076` `[v1.0]` **Pseudo-localisation** build mode that lengthens strings by 40 % and wraps them in brackets, used in CI screenshot tests to catch truncation and clipping before translators find it.

`REQ-UIX-077` `[v1.x]` **Localization Editor** tool (AIMP parity) so translators do not need a development environment, plus a documented contribution flow.

`REQ-UIX-078` `[v1.0]` Translatable strings MUST carry **translator comments** wherever the string is ambiguous out of context (e.g. "Play" as a verb vs. a noun), and MUST NOT be assembled from fragments.

---

## 13 · Keyboard Shortcuts & Global Hotkeys

### 13.1 Principles

`REQ-KEY-001` `[v1.0]` Eclipse Player is **keyboard-first on desktop**. Every command MUST be reachable from the keyboard, and every command MUST be in a searchable command palette (`Ctrl+Shift+P`).

`REQ-KEY-002` `[v1.0]` Every shortcut MUST be **remappable**, conflicts MUST be detected and reported at assignment time (naming the conflicting command), and the whole map MUST be exportable and importable as JSON with the settings bundle (§19.4).

### 13.2 Command registry

`REQ-KEY-003` `[v1.0]` Commands live in one central registry, keyed by a stable dotted id. That same registry backs: menus, shortcuts, the command palette, tray menu, skin `action:` bindings (`REQ-THM-028`), MPRIS/SMTC mappings, and the plugin SDK's command surface. **One registry, six consumers** — a command added once appears everywhere, and an action a skin references cannot fail to exist.

### 13.3 Default local shortcuts

`REQ-KEY-004` `[v1.0]` These defaults MUST ship. (`Ctrl` is `Cmd` on any future macOS port.)

**Playback**

| Shortcut | Command |
|---|---|
| `Space` | `player.playPause` |
| `Ctrl+Right` / `Ctrl+Left` | `player.next` / `player.previous` |
| `Right` / `Left` | `player.seekForward` (5 s) / `player.seekBackward` (5 s) |
| `Shift+Right` / `Shift+Left` | `player.seekForwardLarge` (30 s) / `player.seekBackwardLarge` |
| `Ctrl+.` | `player.stop` |
| `Ctrl+Shift+.` | `player.stopAfterCurrent` |
| `Up` / `Down` | `player.volumeUp` / `player.volumeDown` (2 %) |
| `Ctrl+M` | `player.muteToggle` |
| `Ctrl+R` | `player.cycleRepeat` |
| `Ctrl+H` | `player.cycleShuffle` |
| `Ctrl+B` | `player.addBookmark` |
| `A` then `B` | `player.abRepeatSetA` / `player.abRepeatSetB` |
| `Ctrl+Shift+A` | `player.abRepeatClear` |
| `Ctrl+Up` / `Ctrl+Down` | `player.speedUp` / `player.speedDown` |
| `Ctrl+0` | `player.speedReset` |

**Navigation & views**

| Shortcut | Command |
|---|---|
| `Ctrl+F` | `view.focusSearch` |
| `Ctrl+Shift+P` | `view.commandPalette` |
| `Ctrl+1`…`Ctrl+9` | `view.gotoTab1`…`9` |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | `view.nextTab` / `view.previousTab` |
| `F11` | `view.fullScreenNowPlaying` |
| `Ctrl+Shift+M` | `view.toggleMiniPlayer` |
| `Ctrl+Shift+W` | `view.toggleWindowshade` `[v1.x]` |
| `Ctrl+Shift+T` | `view.toggleAlwaysOnTop` |
| `Ctrl+G` | `view.scrollToCurrentTrack` |
| `Ctrl+L` | `view.toggleLyrics` |
| `Ctrl+Shift+V` | `view.toggleVisualizer` |
| `Ctrl+E` | `view.showEqualizer` |
| `Esc` | `view.dismiss` |

**Library & playlist**

| Shortcut | Command |
|---|---|
| `Ctrl+O` | `file.openFiles` |
| `Ctrl+Shift+O` | `file.openFolder` |
| `Ctrl+U` | `file.openUrl` |
| `Ctrl+N` | `playlist.new` |
| `Ctrl+S` | `playlist.save` |
| `Ctrl+W` | `playlist.closeTab` |
| `Ctrl+A` | `playlist.selectAll` |
| `Delete` | `playlist.removeSelected` |
| `Shift+Delete` | `file.deleteFromDisk` (always confirmed) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | `playlist.undo` / `playlist.redo` |
| `Ctrl+Q` | `queue.addSelected` |
| `Ctrl+Shift+Q` | `queue.playNext` |
| `Alt+Enter` | `track.showTechnicalInfo` |
| `F2` | `track.editTags` |
| `Ctrl+Shift+R` | `library.rescanAll` |
| `0`…`5` | `track.setRating0`…`5` |
| `L` | `track.toggleLoved` |

`REQ-KEY-005` `[v1.0]` `Shift+Delete` (delete from disk) MUST always confirm, MUST default to the recycle bin/trash, and MUST never be the default binding for playlist removal. Conflating "remove from list" with "delete file" is a data-loss trap.

### 13.4 Global hotkeys and media keys

`REQ-KEY-010` `[v1.0]` Media keys MUST work while the window is unfocused or minimised: play/pause, stop, next, previous, volume up/down, mute.

`REQ-KEY-011` `[v1.0]` **Windows.** Handle `WM_APPCOMMAND` (`APPCOMMAND_MEDIA_PLAY_PAUSE`, `_MEDIA_STOP`, `_MEDIA_NEXTTRACK`, `_MEDIA_PREVIOUSTRACK`, `_VOLUME_*`) for media keys, and `RegisterHotKey` for user-defined global combinations. Registration failure (another app owns the combination) MUST be reported to the user with the combination named — silent failure here generates endless bug reports. A low-level keyboard hook MUST NOT be used: it trips anti-malware heuristics and is unnecessary.

`REQ-KEY-012` `[v1.0]` **Linux, X11.** Grab keys with XCB `xcb_grab_key` on the root window for each configured combination, handling `BadAccess` when another client already holds it, and reporting that clearly.

`REQ-KEY-013` `[v1.0]` **Linux, Wayland — honest limitation.** Wayland has **no protocol for an application to grab a global hotkey**. Therefore:

1. **MPRIS2 (§14.2.1) is the primary mechanism** on Wayland — GNOME and KDE route media keys to the MPRIS-registered player, so media keys work correctly with no hotkey grab at all. This MUST be implemented first and MUST be complete.
2. Where available, use the XDG Desktop Portal `org.freedesktop.portal.GlobalShortcuts` interface for user-defined combinations, degrading gracefully when the portal or that interface is absent.
3. If neither is available, the UI MUST state plainly that global hotkeys are unavailable on this session type and that media keys work through the desktop's media integration.
4. This limitation MUST appear in §29.2. It MUST NOT be papered over with an X11-only implementation that silently does nothing under Wayland.

`REQ-KEY-014` `[v1.0]` **Android.** Handle `MediaSession` transport controls, `ACTION_MEDIA_BUTTON`, Bluetooth AVRCP commands, and wired-headset button events (single press = play/pause, double = next, triple = previous).

`REQ-KEY-015` `[v1.0]` A global-hotkey configuration UI MUST show, per binding, whether registration currently **succeeded**, and if not, why.

---

## 14 · Operating-System Integration

> Native integration is the reason to build a native app. Each item here is something a web-based player cannot do, and each is a place where "mostly works" is indistinguishable from broken.

### 14.1 Windows

#### 14.1.1 System Media Transport Controls

`REQ-OSI-001` `[v1.0]` Integrate with `Windows.Media.SystemMediaTransportControls` (SMTC) so Eclipse appears in the Windows volume flyout and responds to hardware and OS-level media controls.

Implementation requirements:

- Obtain the instance for a Win32 window via `ISystemMediaTransportControlsInterop::GetForWindow(hwnd)`. The WinRT `GetForCurrentView()` path does not apply to a Win32 app.
- Set `IsPlayEnabled`, `IsPauseEnabled`, `IsStopEnabled`, `IsNextEnabled`, `IsPreviousEnabled` to reflect **actual current capability** — `IsNextEnabled` MUST be false at the end of a queue with repeat off, so the OS control greys out correctly.
- Set `PlaybackStatus` to `Playing` / `Paused` / `Stopped` / `Changing` on every state change.
- Populate `DisplayUpdater`: `Type = MediaPlaybackType.Music`, then `MusicProperties` (`Title`, `Artist`, `AlbumTitle`, `AlbumArtist`, `TrackNumber`, `Genres`), then `Thumbnail` from a `RandomAccessStreamReference`, then call `Update()`. Forgetting the final `Update()` is the single most common SMTC bug.
- Call `UpdateTimelineProperties` with `StartTime`, `EndTime`, `Position`, `MinSeekTime`, `MaxSeekTime` so the OS scrubber works, and refresh it at most **once per second** while playing.
- Handle `ButtonPressed` and `PlaybackPositionChangeRequested`, marshalling to the UI thread.
- Clear the display and set `PlaybackStatus = Closed` on shutdown so no stale entry lingers in the flyout.

`REQ-OSI-002` `[v1.0]` SMTC metadata MUST match what the app shows, including EFS-independent raw field values. Artwork MUST be provided at ≥ 300 × 300 px.

#### 14.1.2 Taskbar and shell

`REQ-OSI-003` `[v1.0]` **Thumbnail toolbar** (`ITaskbarList3::ThumbBarAddButtons`) with previous / play-pause / next, icons following the active theme's light-or-dark mode.

`REQ-OSI-004` `[v1.0]` **Taskbar progress** (`SetProgressValue`) during long operations: library scan, ReplayGain scan, fingerprinting, conversion. It MUST be cleared on completion or cancellation.

`REQ-OSI-005` `[v1.x]` **Jump list** with Recent (recently played tracks) and Tasks (Play/Pause, Next, Open file) categories.

`REQ-OSI-006` `[v1.0]` **System tray** (`QSystemTrayIcon`) with: single-click to show/hide, middle-click to play/pause, scroll-wheel to change volume, a hover tooltip driven by the EFS tray pattern (§10.6), and a context menu with transport, rating, queue, and quit. A configurable "minimise to tray" and "close to tray" behaviour MUST exist, both **off by default** — hijacking the close button without consent is user-hostile.

`REQ-OSI-007` `[v1.0]` **Toast on track change**, optional and off by default, using the native notification API, with artwork, EFS-driven text, and click-to-focus.

#### 14.1.3 File associations and shell verbs

`REQ-OSI-008` `[v1.0]` The installer MUST offer (never silently take) associations for all supported audio extensions plus `.m3u`, `.m3u8`, `.pls`, `.xspf`, `.cue`, `.ecpl`, and `.eclipseskin`, registered per-user under `HKCU\Software\Classes` using the modern `ProgID` + `Applications` pattern so Windows' default-app UI works properly.

`REQ-OSI-009` `[v1.0]` Register the shell verbs **Play with Eclipse Player** and **Enqueue in Eclipse Player** for audio files and folders.

`REQ-OSI-010` `[v1.0]` Uninstall MUST remove every registry key and association it created, and MUST NOT remove user data unless explicitly requested with a clear checkbox.

`REQ-OSI-011` `[v1.0]` **Long-path support** MUST be enabled via the application manifest (`longPathAware`) and by using `\\?\`-prefixed paths in the filesystem layer, so paths beyond 260 characters work (`REQ-LIB-033`).

#### 14.1.4 Single instance and IPC

`REQ-OSI-012` `[v1.0]` By default Eclipse runs as a **single instance**. A second launch MUST forward its arguments to the running instance and exit. Implement with a named mutex for detection plus `QLocalServer`/`QLocalSocket` (a named pipe) for the handoff.

`REQ-OSI-013` `[v1.0]` The IPC surface MUST be **local-only, authenticated by OS user**, and strictly limited to a fixed command set: enqueue paths, play paths, transport commands, and raise-window. It MUST NOT accept arbitrary commands, paths outside the user's reach, or anything resembling code. Treat it as an untrusted input boundary (§21.4) and fuzz it.

`REQ-OSI-014` `[v1.0]` Command-line interface, identical on both desktop platforms:

```
eclipse-player [OPTIONS] [FILE|FOLDER|URL]...

  --enqueue, -e         Add to the queue instead of replacing it
  --play, -p            Replace the queue and start playing
  --next / --previous / --play-pause / --stop / --toggle
  --volume <0-100>
  --new-instance        Bypass single-instance forwarding
  --portable            Force portable mode (§19.3)
  --config-dir <path>   Override the config directory
  --log-level <level>   trace|debug|info|warn|error
  --safe-mode           Start with default theme, no skins, no plugins
  --version / --help
```

`REQ-OSI-015` `[v1.0]` **Safe mode** is required for supportability: it MUST start with the built-in theme, no user skins, no plugins, and default settings, without destroying the user's configuration. It MUST be reachable from the CLI and by holding `Shift` during launch.

### 14.2 Linux

#### 14.2.1 MPRIS2

`REQ-OSI-020` `[v1.0]` Implement the full MPRIS2 D-Bus interface via QtDBus. This is the single highest-value Linux integration: it makes media keys work, and it puts Eclipse in the GNOME and KDE media widgets and lock screens.

- **Bus name:** `org.mpris.MediaPlayer2.eclipseplayer` (plus a `.instanceNNNN` suffix if multiple instances are permitted).
- **Object path:** `/org/mpris/MediaPlayer2`.

`org.mpris.MediaPlayer2` properties: `Identity` = `"Eclipse Player"`, `DesktopEntry` = `"eclipse-player"`, `SupportedUriSchemes` = `["file","http","https"]`, `SupportedMimeTypes` (the full audio list), `CanQuit`, `CanRaise`, `CanSetFullscreen`, `Fullscreen`, `HasTrackList`. Methods: `Raise`, `Quit`.

`org.mpris.MediaPlayer2.Player` properties: `PlaybackStatus` (`Playing`/`Paused`/`Stopped`), `LoopStatus` (`None`/`Track`/`Playlist`, **writable**), `Rate`, `MinimumRate`, `MaximumRate`, `Shuffle` (**writable**), `Metadata`, `Volume` (**writable**), `Position`, `CanGoNext`, `CanGoPrevious`, `CanPlay`, `CanPause`, `CanSeek`, `CanControl`. Methods: `Next`, `Previous`, `Pause`, `PlayPause`, `Stop`, `Play`, `Seek`, `SetPosition`, `OpenUri`. Signal: `Seeked`.

`REQ-OSI-021` `[v1.0]` `Metadata` MUST populate: `mpris:trackid` (a valid D-Bus **object path**, not a plain string — a frequent spec violation that breaks GNOME), `mpris:length` (microseconds, `int64`), `mpris:artUrl` (a `file://` URL to a cached artwork file — the cache MUST therefore write artwork to disk for this purpose), `xesam:title`, `xesam:artist` (array), `xesam:album`, `xesam:albumArtist` (array), `xesam:genre` (array), `xesam:trackNumber`, `xesam:discNumber`, `xesam:useCount`, `xesam:userRating` (0.0–1.0), `xesam:contentCreated`, `xesam:url`.

`REQ-OSI-022` `[v1.0]` Property changes MUST be emitted via `org.freedesktop.DBus.Properties.PropertiesChanged`, **coalesced** so that a seek or a rapid volume change does not flood the bus. `Position` MUST NOT be emitted on a timer — MPRIS clients poll it; only `Seeked` is signalled.

`REQ-OSI-023` `[v1.0]` MPRIS `LoopStatus`, `Shuffle`, and `Volume` writes from an external client MUST actually change Eclipse's state and MUST round-trip. Read-only stubs are a spec violation.

`REQ-OSI-024` `[v1.0]` A D-Bus integration test MUST drive every method and property from an external client process and assert the resulting application state (§23.6).

#### 14.2.2 Desktop entry, MIME, and icons

`REQ-OSI-025` `[v1.0]` Install a `.desktop` file with: `Name`, `GenericName=Music Player`, `Comment`, `Exec=eclipse-player %U`, `Icon=eclipse-player`, `Terminal=false`, `Type=Application`, `Categories=AudioVideo;Audio;Player;`, `MimeType=` (the complete supported list), `StartupNotify=true`, `StartupWMClass`, `Keywords`, and `X-GNOME-UsesNotifications=true`.

`REQ-OSI-026` `[v1.0]` Provide `MPRIS`-compatible desktop actions in the `.desktop` file (`Desktop Action Play`, `Pause`, `Next`, `Previous`) so right-clicking the launcher offers transport controls.

`REQ-OSI-027` `[v1.0]` Install a `shared-mime-info` XML defining the `.eclipseskin`, `.ecpl`, and `.eclipseicons` types, and install icons at 16, 22, 24, 32, 48, 64, 128, 256 px plus a scalable SVG into the hicolor theme.

#### 14.2.3 Tray on Linux

`REQ-OSI-028` `[v1.0]` Use `QSystemTrayIcon`, which routes to the StatusNotifierItem D-Bus specification on desktops that support it. Because **GNOME has no tray by default**, the app MUST:

1. Detect tray unavailability at runtime.
2. Never depend on the tray for any function (§3.1 `REQ-GEN-003`).
3. Not offer "minimise to tray" when no tray exists, rather than offering a setting that does nothing.
4. Rely on MPRIS2 for background control, which is the correct GNOME-native answer.

#### 14.2.4 XDG conformance

`REQ-OSI-029` `[v1.0]` Respect `$XDG_CONFIG_HOME`, `$XDG_DATA_HOME`, `$XDG_CACHE_HOME`, and `$XDG_STATE_HOME` with correct fallbacks (§19.2). Never write to `$HOME` directly. Never write to the installation directory outside portable mode.

`REQ-OSI-030` `[v1.0]` Support the XDG **inhibit** interface (`org.freedesktop.portal.Inhibit`, or the logind/GNOME/KDE fallbacks) to prevent system idle-suspend during playback, and release the inhibit on pause or stop. A music player that lets the machine sleep mid-album is broken; one that never releases the inhibit is worse.

### 14.3 Cross-platform desktop

`REQ-OSI-035` `[v1.0]` **Multi-output-device switching at runtime.** Enumerate devices per backend (§8.7), switch without stopping playback (fade → reconfigure → fade, per `REQ-AUD-121`), and remember the last-used device per backend. Device hot-plug MUST refresh the list live.

`REQ-OSI-036` `[v1.0]` **Session and power events.** Handle system suspend and resume (close and reopen the audio device rather than assuming it survived), session lock (continue playing by default, configurable), and shutdown (save state before the process is killed — this MUST use the platform's session-manager hooks, not only `atexit`).

`REQ-OSI-037` `[v1.0]` **Crash-resilient state saving.** Playback position, queue, and playlist tabs MUST be persisted at least every 10 seconds and on every state change, so an unclean shutdown loses at most a few seconds of context.

### 14.4 Android

`REQ-OSI-040` `[v1.0]` **`MediaLibraryService`** (Media3) hosting a `MediaLibrarySession`, with a foreground service of type `mediaPlayback`, correct notification (`REQ-UIX-027`), and correct lifecycle: the service MUST stop when playback ends and the notification is dismissed, and MUST NOT be killed while playing.

`REQ-OSI-041` `[v1.0]` **Audio focus** MUST be handled fully, using `AudioFocusRequest` with `AudioAttributes(USAGE_MEDIA, CONTENT_TYPE_MUSIC)`:

| Event | Behaviour |
|---|---|
| `AUDIOFOCUS_LOSS` | Pause; do not auto-resume. |
| `AUDIOFOCUS_LOSS_TRANSIENT` | Pause; auto-resume on regain if we were playing. |
| `AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK` | Duck to 20 % over 200 ms; restore on regain. Configurable to pause instead. |
| `AUDIOFOCUS_GAIN` | Restore volume; resume only if the loss was transient and we were playing. |
| Request denied | Do not start playback; inform the user. |

`REQ-OSI-042` `[v1.0]` **Becoming-noisy** (`ACTION_AUDIO_BECOMING_NOISY`) MUST pause playback — unplugging headphones must never blast audio from the speaker.

`REQ-OSI-043` `[v1.0]` **Bluetooth / AVRCP**: metadata and artwork MUST appear on car head units and Bluetooth speakers; AVRCP transport commands MUST work; and reconnection MUST restore metadata.

`REQ-OSI-044` `[v1.0]` **Playback resumption** (Android 11+): implement `onPlaybackResumption` so the system media-resumption UI can restart the last session, returning the last queue and position.

`REQ-OSI-045` `[v1.x]` **Alarm / wake-with-music**: a scheduled alarm using `AlarmManager` with exact alarms where permitted, gradual volume ramp, and a fallback if the audio source is unavailable at alarm time.

---

## 15 · Android Auto Specification

> Android Auto is a certification surface with driver-safety rules, not just another layout. Non-conformance means rejection.

### 15.1 Declaration and structure

`REQ-AUT-001` `[v1.0]` Declare Auto support with `res/xml/automotive_app_desc.xml`:

```xml
<automotiveApp>
    <uses name="media"/>
</automotiveApp>
```

referenced from the manifest via `<meta-data android:name="com.google.android.gms.car.application" android:resource="@xml/automotive_app_desc"/>`.

`REQ-AUT-002` `[v1.0]` The service MUST be a Media3 `MediaLibraryService` exposing a browsable tree, with an intent filter for `androidx.media3.session.MediaLibraryService` **and** `android.media.browse.MediaBrowserService` for compatibility.

`REQ-AUT-003` `[v1.0]` **Package validation.** `onGetSession`/`onGetLibraryRoot` MUST validate the connecting client against an allowlist of package name **plus signing certificate**, covering Android Auto (`com.google.android.projection.gearhead`), Automotive OS, Wear, Assistant, and the system UI. An unknown caller MUST receive a restricted or rejected root. Accepting any caller exposes the library to arbitrary apps.

### 15.2 Browse tree

`REQ-AUT-004` `[v1.0]` Root level MUST expose **at most 4 tabs** — Android Auto displays no more than four, and additional tabs are silently dropped. The four, in order:

| Tab | Contents |
|---|---|
| **Playlists** | User playlists and smart playlists |
| **Albums** | Albums, grid style |
| **Artists** | Artists → their albums → tracks |
| **Recent** | Recently played, then Recently added |

`REQ-AUT-005` `[v1.0]` Genres and Folders MUST be reachable one level deeper (under Playlists as pseudo-nodes, or via search), not at the root. The v1.x document listed six root categories, which exceeds the platform limit.

`REQ-AUT-006` `[v1.0]` **Content style hints** MUST be set so items render correctly, using the documented `BrowserRoot`/`MediaItem` extras:

| Extra | Value |
|---|---|
| `android.media.browse.CONTENT_STYLE_SUPPORTED` | `true` |
| `android.media.browse.CONTENT_STYLE_BROWSABLE_HINT` | `CONTENT_STYLE_GRID_ITEM_HINT_VALUE` (2) for Albums/Artists; `LIST` (1) for Playlists |
| `android.media.browse.CONTENT_STYLE_PLAYABLE_HINT` | `CONTENT_STYLE_LIST_ITEM_HINT_VALUE` (1) |
| `android.media.browse.CONTENT_STYLE_GROUP_TITLE_HINT` | Set per item to group ("Recently played", "Recently added") |
| `android.media.browse.SEARCH_SUPPORTED` | `true` |

`REQ-AUT-007` `[v1.0]` **Pagination.** `onLoadChildren` MUST honour the page and page-size hints. A 50,000-track library MUST NOT be returned in one list; unbounded lists cause head-unit timeouts and ANRs. Enforce a hard cap of **200 items per page**.

`REQ-AUT-008` `[v1.0]` Every browsable node MUST return within **1 second**. Database queries backing the Auto tree MUST be indexed for exactly these access patterns, and MUST NOT block on a running library scan.

`REQ-AUT-009` `[v1.0]` **Artwork for Auto** MUST be supplied as a local `content://` or `file://` URI at 320 × 320 px minimum, pre-cached. Auto does not fetch remote images, and passing a large bitmap through the binder transaction will fail — MUST use URIs, not bitmaps.

### 15.3 Graceful degradation

`REQ-AUT-010` `[v1.0]` The Auto surface MUST NOT expose: raw filesystem paths, tracks marked `missing_since`, tracks marked `unplayable_code`, entries from a source that is currently offline, or items still being scanned. Every leaf presented in the car MUST be playable when tapped.

`REQ-AUT-011` `[v1.0]` If the library is empty or a permission is missing, the Auto tree MUST show a single, clear, non-actionable-in-car message ("Open Eclipse Player on your phone to add music"), never an empty list and never a crash.

`REQ-AUT-012` `[v1.0]` Errors MUST be surfaced through `PlaybackState` with `STATE_ERROR` and a human-readable, short error message suitable for a glance — not a stack trace, not a code.

### 15.4 Voice search

`REQ-AUT-013` `[v1.0]` Implement `onSearch`/`onGetSearchResult` and handle `ACTION_MEDIA_PLAY_FROM_SEARCH` with `EXTRA_MEDIA_FOCUS`, distinguishing the focus types: artist, album, genre, playlist, and unstructured query.

`REQ-AUT-014` `[v1.0]` Search resolution order: exact match on the focused entity → fuzzy match on that entity → cross-entity search → "no results" with a spoken-friendly message. "Play [artist]" MUST start that artist's tracks shuffled; "Play [album]" MUST start the album in order — order matters and MUST differ by entity type.

`REQ-AUT-015` `[v1.0]` Voice search MUST work with **no network connection**, because the library is local. Search MUST NOT be gated on any online service.

### 15.5 Controls and driver safety

`REQ-AUT-016` `[v1.0]` Support steering-wheel and head-unit controls: play/pause, next, previous, and up to two custom actions. The custom actions MUST be **thumbs-up/love** and **shuffle toggle**, both with clear icons and both reflecting current state.

`REQ-AUT-017` `[v1.0]` Playback MUST continue reliably in the background for the whole drive: correct foreground service, correct wake-lock discipline, no battery-optimisation-induced death mid-drive, and correct behaviour across phone calls (pause and resume).

`REQ-AUT-018` `[v1.0]` **No visualizers, no animations, no scrolling marquees** in the Auto surface. Driver-distraction rules forbid them, and the app MUST NOT rely on the head unit to suppress them.

`REQ-AUT-019` `[v1.0]` Text in the Auto surface MUST come from raw metadata fields, **not** from user-configurable EFS patterns — a user's custom pattern could produce unreadable or overlong strings in a car.

### 15.6 Verification

`REQ-AUT-020` `[v1.0]` A manual Android Auto checklist MUST live in `docs/TESTING.md` and MUST be completed against the **Desktop Head Unit (DHU)** and at least one **physical head unit** before any release. Minimum items:

1. App appears in the Auto launcher with the correct name and icon.
2. All four root tabs load in under 1 s.
3. Deep navigation to a track works and plays.
4. A 20,000+ track library paginates without a timeout or ANR.
5. Artwork displays at every level.
6. Voice search works for artist, album, song, genre, and playlist — with the network disabled.
7. Steering-wheel next/previous/play-pause work.
8. Both custom actions work and reflect state.
9. Playback survives: screen off, phone call in and out, Bluetooth reconnect, 30 minutes of driving.
10. Metadata and elapsed time on the head unit stay correct across track changes.
11. Playback resumption restores the previous session.
12. An empty library and a revoked permission both degrade to a clear message.

`REQ-AUT-021` `[v1.0]` Automated instrumentation tests MUST cover `onGetLibraryRoot`, `onLoadChildren` for every node type, pagination, package validation (including a rejected caller), and search resolution. The tree logic is testable without a head unit and MUST be tested that way.

---

## 16 · Plugin & Extension SDK

> Winamp published a plugin SDK in early 1998 and had 66 plugins by November of that year. That ecosystem is why the player outlived its own company. Eclipse's skin engine deliberately contains no code (§11.1); the plugin SDK is where extensibility with real capability lives, behind an explicit, informed trust decision.

`REQ-PLG-001` `[v1.x]` The SDK ships as a stable **C ABI**, versioned independently of the application, with headers under `desktop/include/eclipse/plugin/`, dual-licensed `Apache-2.0 OR MPL-2.0` (`REQ-GEN-011`).

`REQ-PLG-002` `[v1.x]` **C ABI, not C++.** Rationale to record in an ADR: a C++ ABI is not stable across compilers or standard-library versions, which would mean a plugin built with GCC 13 could not load into an MSVC-built host. C structs of function pointers, opaque handles, and explicit versioning are the only approach that survives a decade.

### 16.1 Plugin categories

`REQ-PLG-003` `[v1.x]` Seven categories, adapted from Winamp's proven taxonomy to our architecture:

| Category | Purpose | Implements | Risk |
|---|---|---|---|
| **Decoder** | Add an input format | `IDecoder` (§8.3) | Parses untrusted data — highest risk |
| **Output** | Add an audio sink | `IAudioSink` (§8.7) | Runs on the RT thread |
| **DSP** | Insert into the effects chain | A frame-processing callback | Runs on the RT thread |
| **Visualizer** | Add a visualisation | Consumes the spectrum tap, renders to a surface | GPU/CPU cost |
| **Metadata** | Supply tags or artwork | Tag/artwork provider | Network access if it chooses |
| **Library view** | Add a browse category or column | Query + presentation contributor | DB read access |
| **Service** | Background integrations (scrobblers, remotes) | Lifecycle + event subscriptions | Broadest capability |

### 16.2 Addon taxonomy (user-facing)

`REQ-PLG-004` `[v1.x]` Following AIMP's clear addon categories, the app's Add-ons screen MUST present: **Skins**, **Icon packs**, **Plugins**, **Encoders**, and **Wallpapers** — with Skins, Icon packs, and Wallpapers being code-free data packages (§11) and Plugins and Encoders being native code with an explicit consent step.

### 16.3 ABI and lifecycle

`REQ-PLG-005` `[v1.x]` Every plugin exports one symbol:

```c
/* eclipse_plugin.h — abridged; the real header is fully documented. */

#define ECLIPSE_PLUGIN_ABI_VERSION 1

typedef struct EclipsePluginInfo {
    uint32_t    abi_version;      /* MUST equal ECLIPSE_PLUGIN_ABI_VERSION */
    uint32_t    category;         /* EclipsePluginCategory */
    const char *id;               /* reverse-DNS, e.g. "org.example.opusplus" */
    const char *name;
    const char *version;          /* semver */
    const char *author;
    const char *license;          /* SPDX identifier — REQUIRED */
    const char *homepage;
    uint64_t    capabilities;     /* bitmask the plugin requests — see REQ-PLG-008 */
} EclipsePluginInfo;

typedef struct EclipseHostApi {         /* what the host offers the plugin */
    uint32_t abi_version;
    void  (*log)(void *ctx, int level, const char *msg);
    /* ... explicitly enumerated, capability-gated functions ... */
} EclipseHostApi;

typedef struct EclipsePluginVTable {    /* what the plugin offers the host */
    int  (*initialize)(void *self, const EclipseHostApi *host, void *host_ctx);
    void (*shutdown)(void *self);
    /* category-specific function pointers follow */
} EclipsePluginVTable;

/* The single exported entry point. */
const EclipsePluginInfo *eclipse_plugin_query(void);
EclipsePluginVTable     *eclipse_plugin_create(void);
void                     eclipse_plugin_destroy(EclipsePluginVTable *);
```

`REQ-PLG-006` `[v1.x]` **ABI compatibility policy.** The host MUST refuse to load a plugin whose `abi_version` it does not know. Within one ABI version, the host MUST NOT reorder or remove struct members — only append, and only with a size field or a new version. Breaking the ABI requires incrementing `ECLIPSE_PLUGIN_ABI_VERSION` and a major application release.

`REQ-PLG-007` `[v1.x]` Lifecycle: discover → query (`eclipse_plugin_query`, which MUST NOT allocate, block, or touch the filesystem) → user consent → create → initialize → active → shutdown → destroy → unload. A plugin that fails or crashes in any phase MUST be quarantined: disabled, recorded, and reported, with the app continuing to run.

### 16.4 Capabilities and consent

`REQ-PLG-008` `[v1.x]` A plugin **declares** the capabilities it needs, and the host **grants** them explicitly. Capability set: `AUDIO_PROCESS`, `AUDIO_OUTPUT`, `DECODE`, `FILE_READ`, `FILE_WRITE`, `NETWORK`, `LIBRARY_READ`, `LIBRARY_WRITE`, `SETTINGS_READ`, `SETTINGS_WRITE`, `UI_CONTRIBUTE`, `GPU`.

`REQ-PLG-009` `[v1.x]` The install flow MUST show, in plain language, exactly what the plugin is asking for — "This plugin can read your music library and access the internet" — and MUST require an explicit action to enable. Capabilities MUST be revocable afterwards, and revoking one MUST disable the plugin rather than letting it fail unpredictably.

`REQ-PLG-010` `[v1.x]` Host API functions MUST be **capability-gated at the call site**, returning a permission error rather than trusting the manifest. Declaration is a UI affordance; enforcement is in the host.

### 16.5 Isolation and stability

`REQ-PLG-011` `[v1.x]` **Honest statement of limits**, to be documented rather than glossed over: a native in-process plugin cannot be fully sandboxed. Eclipse's mitigations are:

1. Explicit, informed consent before any plugin loads (`REQ-PLG-009`).
2. **Crash quarantine**: a plugin that crashes or hangs is disabled on next launch and the user is told which one, with a one-click permanent disable.
3. **Watchdog on the RT path**: a DSP or output plugin whose callback exceeds its time budget is bypassed automatically within one buffer and disabled, so a bad plugin degrades to silence-free playback rather than continuous dropouts.
4. **Safe mode** (`REQ-OSI-015`) always starts with no plugins, so a bad plugin can never brick the app.
5. `[v2]` Out-of-process hosting for Decoder and Metadata plugins — the two categories that parse untrusted data — over a shared-memory transport. This is the correct long-term answer and MUST be architecturally possible from v1.x by keeping those interfaces free of pointer-sharing beyond explicit buffers.

`REQ-PLG-012` `[v1.x]` Plugins MUST NOT be loaded from any directory writable by another user, and the plugin directory MUST be inside the user's data directory (§19.2) or the installation directory.

`REQ-PLG-013` `[v1.x]` `docs/PLUGIN-AUTHORING.md` MUST include a complete, compilable example for each of the seven categories, the RT-safety rules (§8.2.3) restated for plugin authors, and the ABI stability promise.

---

## 17 · Network Features

> Every feature in this section is **off by default** and MUST remain fully functional-when-absent. §19.5 is the binding privacy contract.

`REQ-NET-001` `[v1.0]` **Global network switch.** A single setting, **off by default**, gates every outbound connection in the application. With it off, the app MUST make **zero** network connections of any kind, verifiable by the firewall test in §23.9.

`REQ-NET-002` `[v1.0]` All HTTP MUST go through one internal client (`IHttpClient`) so that policy — the global switch, TLS settings, timeouts, retry, user-agent, proxy, and redaction — is enforced in exactly one place. FFmpeg's network layer is disabled (§4.4) precisely so there is no second path.

`REQ-NET-003` `[v1.0]` **TLS is mandatory** for all outbound requests. Certificate validation MUST NOT be disableable. `http://` URLs MUST be permitted **only** for user-entered radio streams, and the UI MUST mark such a stream as unencrypted.

`REQ-NET-004` `[v1.0]` A fixed, honest `User-Agent`: `EclipsePlayer/<version> (+https://eclipse-player.org)`. It MUST NOT include OS build details, hardware identifiers, a unique id, or anything usable for fingerprinting.

`REQ-NET-005` `[v1.0]` Respect the system proxy by default, with manual override (HTTP/HTTPS/SOCKS5).

### 17.1 Internet radio

`REQ-NET-010` `[v1.x]` Stream playback over HTTP/HTTPS for: raw MP3/AAC/Ogg/Opus/FLAC streams, Icecast and Shoutcast endpoints, HLS (`.m3u8`) playlists, and station playlists (`.pls`, `.m3u`) that resolve to streams.

`REQ-NET-011` `[v1.x]` **ICY metadata** MUST be parsed (`Icy-MetaData: 1` request, `icy-metaint` response) and the `StreamTitle` displayed as the current track, with `icy-name`, `icy-genre`, `icy-br`, and `icy-url` shown as station info. Track changes on a stream MUST update the OS media controls and notification like a local track change.

`REQ-NET-012` `[v1.x]` **Cancellable connect.** Connecting MUST be abortable by the user at any point — a direct AIMP 4.30 parity item, and the difference between a responsive player and a frozen one on a dead stream. Connect timeout 10 s, read timeout 15 s.

`REQ-NET-013` `[v1.x]` **Buffering** with a configurable pre-buffer (default 5 s, range 1–30 s), a visible buffer indicator, and automatic reconnection with backoff (1 s, 2 s, 5 s, 10 s, 30 s) preserving the station. Buffer underrun MUST pause and rebuffer, never emit noise.

`REQ-NET-014` `[v1.x]` **Stream recording** (AIMP parity): record the incoming stream to disk without re-encoding, split on ICY track-change boundaries with EFS-driven filenames, tag the resulting files with the ICY metadata, and show a clear recording indicator. `docs/` MUST note that the user is responsible for the legality of recording in their jurisdiction.

`REQ-NET-015` `[v1.x]` Station management: add by URL, import/export station lists, favourites, genre tags, and last-played ordering. A bundled starter list MAY ship, but MUST NOT be fetched from a remote service at startup.

### 17.2 Podcasts

`REQ-NET-020` `[v1.x]` RSS 2.0 and Atom feed subscription with iTunes podcast extensions: title, author, description, artwork, episode GUID, enclosure URL, duration, publication date, and season/episode numbers.

`REQ-NET-021` `[v1.x]` Requirements: conditional requests via `ETag`/`If-Modified-Since` (be a good citizen); configurable refresh interval with a floor of 15 minutes; optional auto-download over unmetered networks only; episode retention policy; per-episode resume position; variable-speed playback reusing §8.9.2; and a "mark as played" that respects a 95 % completion threshold.

`REQ-NET-022` `[v1.x]` The feed parser is untrusted input: it MUST be hardened against XXE (external entities disabled), billion-laughs, unbounded nesting, and enormous documents (cap at 8 MiB). It is a fuzz target (§21.6).

### 17.3 Metadata and artwork lookup

`REQ-NET-030` `[v1.x]` **MusicBrainz** and **Cover Art Archive** lookup, **opt-in only**, never enabled by default, with per-invocation user initiation for bulk operations.

`REQ-NET-031` `[v1.x]` MUST respect MusicBrainz's rate limit (**1 request per second**, enforced by a client-side token bucket, not by hope), send the required identifying `User-Agent`, and cache results locally so a given lookup is never repeated.

`REQ-NET-032` `[v1.x]` Lookups MUST send **only** the minimum needed: for a fingerprint lookup, the Chromaprint fingerprint and duration; for a text lookup, artist/album/title. It MUST NEVER send file paths, library statistics, play counts, device identifiers, or a list of the user's music.

`REQ-NET-033` `[v1.x]` Every proposed change from an online lookup MUST be **reviewed by the user before it is written**, field by field, with the ability to accept some and reject others. Automatic tag overwriting from an online source is forbidden.

### 17.4 Scrobbling

`REQ-NET-040` `[v1.x]` Optional scrobbling to **Last.fm** and **ListenBrainz**, disabled by default, requiring explicit authentication.

`REQ-NET-041` `[v1.x]` Scrobble threshold: the track has played for **≥ 50 % of its duration or ≥ 240 seconds**, whichever comes first, and the track is longer than 30 seconds — the conventional rule. Speed/tempo changes MUST be accounted for (`REQ-AUD-092`).

`REQ-NET-042` `[v1.x]` **Offline queue.** Scrobbles MUST be queued durably when offline and submitted later in batches, with the original timestamps preserved. A queue that silently drops listens is worse than no scrobbling.

`REQ-NET-043` `[v1.x]` Credentials MUST be stored in the OS secret store (Windows Credential Manager, Secret Service/`libsecret` on Linux, Android Keystore-backed `EncryptedSharedPreferences`) — **never** in a plain settings file. A "log out" action MUST delete them.

### 17.5 Update checking

`REQ-NET-050` `[v1.0]` An update check against the GitHub Releases API, **opt-in on first run via a clear prompt**, default **off**. It MUST send no data beyond the HTTP request itself, MUST run at most once per 24 hours, and MUST be fully disableable. Distribution packages (`.deb`, distro repos) MUST have it compiled out or default-off, since the package manager owns updates there.

`REQ-NET-051` `[v1.0]` The app MUST NOT auto-download or auto-install updates. It notifies; the user decides.

---

## 18 · Sync Module

`REQ-SYN-001` `[v1.0]` Sync is **an optional module, disabled by default**, and the application MUST be fully functional with it permanently off. It is compiled in but inert until enabled.

### 18.1 Model

`REQ-SYN-002` `[v1.0]` **LAN-first, no accounts, no cloud.** Two Eclipse instances on the same network discover each other, pair once, and sync directly. There is no server in the default topology and no identity beyond the device.

`REQ-SYN-003` `[v1.0]` **What syncs** (v1.0): playlists (manual and smart rules), play counts, skip counts, ratings, loved flags, bookmarks, resume positions, last-played timestamps, and the shortcut map. **What does not sync**: audio files, artwork cache, the library index itself, settings unrelated to the above, and secrets. Syncing media files is `[NON-GOAL]` — that is a file-sync tool's job.

`REQ-SYN-004` `[v1.0]` **Track identity across devices** is resolved in this order: MusicBrainz recording ID → Chromaprint fingerprint → normalised `(artist, album, title, duration ±2 s)` → relative path. Unmatched entries are retained as pending, never dropped and never merged into the wrong track. Ambiguity MUST be surfaced for user resolution rather than guessed.

### 18.2 Discovery and pairing

`REQ-SYN-005` `[v1.0]` Discovery via **mDNS/DNS-SD**, service type `_eclipsesync._tcp`, advertising: device name, device UUID, protocol version, and a public-key fingerprint. Discovery MUST be disableable independently of sync.

`REQ-SYN-006` `[v1.0]` **Pairing** uses a short-lived, human-readable **6-digit code** shown on device A and typed on device B, bound to an SPAKE2 or equivalent PAKE exchange, producing a long-term shared key stored in the OS secret store. Pairing MUST expire after 120 seconds and MUST rate-limit attempts (5 tries, then a 5-minute lockout) to defeat brute force.

`REQ-SYN-007` `[v1.0]` Post-pairing transport is **TLS 1.3 with mutual authentication** against the pinned peer keys from pairing. Unpaired peers MUST be refused before any library data is exchanged. Devices MUST be individually revocable, and revocation MUST take effect immediately.

### 18.3 Conflict resolution

`REQ-SYN-008` `[v1.0]` Sync is a **change-log merge**, using the `change_log` table (§9.4) with a **Lamport clock** per device. Wall-clock time MUST NOT be used for ordering — device clocks are wrong, and clock skew silently corrupts merges.

`REQ-SYN-009` `[v1.0]` Per-field resolution rules, chosen so that no rule can lose data:

| Field | Rule | Why |
|---|---|---|
| `play_count`, `skip_count` | **Sum of per-device deltas**, never last-writer-wins | LWW would discard plays made on the other device |
| `rating`, `is_loved` | Last-writer-wins by Lamport clock | Genuinely a single-value user opinion |
| `last_played_at` | Maximum | Monotonic by nature |
| `resume_position_ms` | From the device with the greatest `last_played_at` | The most recent listener is authoritative |
| Playlist membership | **Ordered set union** with tombstones; deletion wins only if its Lamport clock is later than every competing insert | Prevents resurrection and prevents accidental mass deletion |
| Playlist rename | LWW | Single value |
| Smart-playlist rule | LWW, with the losing version retained in history | Rules are hand-authored and expensive to lose |
| Bookmarks | Union by UUID, tombstones for deletes | Additive by nature |

`REQ-SYN-010` `[v1.0]` **Tombstones** MUST be retained for at least 90 days so a device that has been offline cannot resurrect deleted entities. Tombstone compaction MUST be documented and MUST never run below that window.

`REQ-SYN-011` `[v1.0]` Sync MUST be **idempotent and resumable**: applying the same change set twice MUST produce the same state, and an interrupted sync MUST resume without duplication. This MUST be proven by a property-based test that applies random change-log permutations and asserts convergence (§23.8).

### 18.4 Protocol and server

`REQ-SYN-012` `[v1.0]` `shared-spec/sync-protocol.md` MUST fully specify: the wire format (length-prefixed JSON or CBOR — chosen and recorded in an ADR), message types (`Hello`, `Capabilities`, `ChangesSince`, `Changes`, `Ack`, `Error`), the version-negotiation rules, maximum message size, timeouts, and the complete state machine. It MUST be detailed enough for a third party to write a compatible implementation.

`REQ-SYN-013` `[v1.x]` **Optional self-hosted relay server** for cross-network sync: a small Node.js or Go service with Postgres or SQLite, documented as a separate opt-in deployable. It MUST be a **store-and-forward relay of already-encrypted change sets** — the server MUST NOT be able to read library contents. It MUST NEVER be required for core functionality, and no default configuration may point at any hosted instance.

`REQ-SYN-014` `[v1.0]` `shared-spec/sync-protocol.md` MUST contain an explicit **threat model** covering: a malicious peer on the LAN, a peer that replays old change sets, a peer that floods the change log, a stolen paired device, and a compromised relay. Each MUST have a stated mitigation or an accepted-risk note.

---

## 19 · Settings, Data & Privacy

### 19.1 Settings inventory

`REQ-SET-001` `[v1.0]` Settings MUST be organised into these groups, all searchable from the Preferences dialog, every setting with a one-line explanation of what it does:

| Group | Contents |
|---|---|
| **General** | Language, startup behaviour, single instance, tray behaviour, portable mode, update checking |
| **Playback** | Gapless, crossfade, all seven fade durations (§8.5), silence removal, repeat/shuffle defaults, resume behaviour, sleep timer defaults, stop-after-track, A-B repeat |
| **Audio Output** | Backend, device, shared/exclusive, buffer size, device-rate policy, bit-perfect toggle, per-device profiles |
| **DSP** | EQ mode and presets, effects and parameters, ReplayGain mode/pre-amp/clipping, tempo/pitch/speed, limiter, resampler quality, dither |
| **Library** | Sources, scan behaviour, watch, extension allowlist, ignore patterns, multi-value separators, encoding overrides, sort articles, missing-file policy |
| **Metadata** | Tag write on/off, preferred ID3 version, artwork resolution order, artwork cache size, online lookup (off) |
| **Appearance** | Theme, skin, icon pack, density, font override, EFS patterns for every context, visualizer selection |
| **Accessibility** | Enforce accessible contrast, reduced motion override, text scale override, focus ring width, screen-reader verbosity |
| **Shortcuts** | Full remappable map, global hotkeys with registration status |
| **Network** | Global network switch (off), proxy, radio buffering, podcast refresh, scrobbling, metadata lookup |
| **Sync** | Enable (off), discovery, paired devices, what to sync, conflict log |
| **Plugins** | Installed plugins, capabilities, enable/disable, quarantine log |
| **Privacy** | Telemetry status (permanently off by default), crash reporting, log level, data locations, clear caches, delete all data |
| **Advanced** | Log level, database maintenance/vacuum, safe mode, reset all settings, export/import |

`REQ-SET-002` `[v1.0]` Every setting MUST have a **documented default**, and a **Reset to default** action at both the individual-setting and group level. `shared-spec/schemas/settings.schema.json` MUST enumerate every key with its type, range, and default — and CI MUST assert that the application's actual defaults match that schema.

### 19.2 Storage locations

`REQ-SET-003` `[v1.0]` Use `QStandardPaths` / Android `Context` APIs; never hard-code paths.

| Data | Windows | Linux | Android |
|---|---|---|---|
| Settings | `%APPDATA%\EclipsePlayer\` | `$XDG_CONFIG_HOME/eclipse-player/` | DataStore in app-private storage |
| Library DB | `%LOCALAPPDATA%\EclipsePlayer\` | `$XDG_DATA_HOME/eclipse-player/` | app-private `databases/` |
| Skins / plugins | `%APPDATA%\EclipsePlayer\skins\|plugins\` | `$XDG_DATA_HOME/eclipse-player/skins\|plugins/` | app-private `files/skins/` |
| Artwork cache | `%LOCALAPPDATA%\EclipsePlayer\cache\` | `$XDG_CACHE_HOME/eclipse-player/` | `context.cacheDir` |
| Logs | `%LOCALAPPDATA%\EclipsePlayer\logs\` | `$XDG_STATE_HOME/eclipse-player/logs/` | app-private `files/logs/` |

`REQ-SET-004` `[v1.0]` Caches MUST live in the OS cache location so system cleaners may reclaim them, and the app MUST tolerate its cache disappearing at any moment without data loss.

### 19.3 Portable mode

`REQ-SET-005` `[v1.0]` **Portable mode**: when a file named `portable.txt` exists next to the executable, or `--portable` is passed, **all** data (settings, database, skins, plugins, cache, logs) lives in a `data/` subdirectory of the installation folder, and nothing is written to the user profile or the registry.

`REQ-SET-006` `[v1.0]` Portable mode MUST be detectable and shown in About, MUST refuse to activate if the directory is not writable (with a clear message rather than silently falling back), and MUST NOT register file associations or write to the registry.

### 19.4 Export and import

`REQ-SET-007` `[v1.0]` A single **JSON** export containing: all settings, EQ and DSP presets, the shortcut map, EFS patterns, source definitions, playlists (with track identity per `REQ-SYN-004`), ratings, play counts, loved flags, bookmarks, and radio stations. Validated against `settings.schema.json`, versioned, and **human-readable** so a user can inspect exactly what leaves their machine.

`REQ-SET-008` `[v1.0]` Import MUST: validate before applying anything; show a summary of what will change; support merge or replace; be atomic (all or nothing); and **never** import secrets (`REQ-NET-043`) — those are re-authenticated on the new device.

`REQ-SET-009` `[v1.0]` Export MUST NOT include: absolute paths where a relative form suffices, machine identifiers, credentials, tokens, or the sync device key.

### 19.5 Privacy — binding contract

`REQ-SET-010` `[v1.0]` **Zero telemetry.** There is no analytics SDK, no crash-reporting SDK, no attribution SDK, no advertising id access, no fingerprinting. Not disabled by a flag — **absent from the dependency tree**. CI MUST assert this by scanning the resolved dependency graph against a denylist (§25.6). A policy that lives only in a README is not a guarantee; a build that fails is.

`REQ-SET-011` `[v1.0]` If analytics are ever introduced, they MUST be: opt-in with an explicit prompt that defaults to no; fully documented field-by-field in `docs/PRIVACY.md`; inspectable by the user before sending; and disableable permanently. Any change here requires a major-version bump and a changelog entry at the top of the release notes.

`REQ-SET-012` `[v1.0]` **Crash reporting is local-first.** A crash writes a report to the local logs directory. The user MAY then choose to view it, and MAY choose to send it — which opens the report in their browser pre-filled for a GitHub issue. Nothing is transmitted automatically, ever.

`REQ-SET-013` `[v1.0]` **Log redaction.** Logs MUST NOT contain: credentials, tokens, sync keys, or full filesystem paths at `info` level or above. Paths MUST be reduced to basenames unless the log level is `debug` or `trace`, and the log viewer MUST warn before a `debug`-level log is shared.

`REQ-SET-014` `[v1.0]` **Delete all data** MUST exist and MUST actually delete: database, settings, caches, logs, skins, plugins, and secret-store entries — with a confirmation naming what will be destroyed and an offer to export first.

`REQ-SET-015` `[v1.0]` `docs/PRIVACY.md` MUST state, in plain language: what data the app stores, where, what leaves the device and under exactly which user action, what never leaves, and how to verify the claims (the firewall test, the dependency-denylist check, and the source itself).

`REQ-SET-016` `[v1.0]` **No forced accounts anywhere**, including sync (which pairs devices, §18.2) and including any future gallery (browsing MUST require no account; only publishing may).

---

## 20 · Non-Functional Requirements & Performance Budgets

> A budget without a measurement method is a wish. Every number here names how it is measured, and §25.4 wires the measurement into CI.

### 20.1 Reference hardware

`REQ-NFR-001` `[v1.0]` All budgets are stated against these baselines, which MUST be recorded in `docs/TESTING.md`:

| Class | Specification |
|---|---|
| **Desktop reference** | 4-core / 8-thread x86-64 @ 2.5 GHz, 8 GB RAM, SATA SSD, integrated GPU, 1920×1080 |
| **Desktop floor** | 2-core x86-64 @ 1.6 GHz, 4 GB RAM, HDD — must remain *usable*, budgets ×2 permitted |
| **Android reference** | Snapdragon 7-series equivalent, 6 GB RAM, Android 13 |
| **Android floor** | Snapdragon 4-series equivalent, 3 GB RAM, Android 8 — budgets ×2 permitted |
| **Reference library** | 100,000 tracks / 8,000 albums / 12,000 artists, mixed formats, on local SSD |

### 20.2 Startup

`REQ-NFR-002` `[v1.0]`

| Metric | Budget | Measurement |
|---|---|---|
| Desktop cold start to interactive window | **≤ 1200 ms** | Timestamp from `main()` entry to the first frame presented, logged at `info`, asserted in a CI benchmark |
| Desktop warm start | ≤ 600 ms | Same, second launch |
| Time to first audio after `--play <file>` | **≤ 400 ms** | `main()` entry to the first non-silent sample reaching the sink |
| Android cold start to interactive | ≤ 1500 ms | Macrobenchmark `StartupTimingMetric`, `COLD` mode |
| Android warm start | ≤ 700 ms | Same, `WARM` mode |

`REQ-NFR-003` `[v1.0]` Startup MUST NOT block on: a library scan, a network request, artwork generation, plugin loading, or skin validation. All of these are deferred or asynchronous. The window appears first; content fills in.

### 20.3 Library operations

`REQ-NFR-004` `[v1.0]`

| Operation | Budget | Measurement |
|---|---|---|
| Full scan throughput | **≥ 500 tracks/sec** on SSD, ≥ 120 on HDD | Wall-clock over a 100k-track corpus scan, tags read |
| Incremental re-scan, no changes | **≤ 8 s** for 100k tracks | Wall-clock; dominated by `stat()` |
| Search first results | **≤ 80 ms** after last keystroke | Instrumented timer, 100k library |
| Album grid scroll | **≥ 58 fps sustained**, zero frames > 32 ms | Desktop frame timing; Android `FrameTimingMetric` jank ≤ 1 % |
| Playlist load, 10,000 items | ≤ 200 ms to first paint | Instrumented, virtualised list |
| Smart-playlist evaluation | ≤ 150 ms for a 4-clause rule over 100k | SQL `EXPLAIN QUERY PLAN` reviewed; no table scans on indexed fields |

`REQ-NFR-005` `[v1.0]` Scanning MUST NOT degrade playback: no underruns during a full scan, and UI frame rate MUST stay ≥ 45 fps. Scanner threads run at low priority and MUST yield.

### 20.4 Audio

`REQ-NFR-006` `[v1.0]`

| Metric | Budget | Measurement |
|---|---|---|
| RT callback worst case | **≤ 50 % of the period**, full DSP at 192 kHz/2 ch | Per-callback timing histogram; p99.9 asserted |
| Underruns during 24 h playback | **0** | Long-soak test, §23.10 |
| CPU during playback, DSP bypassed | ≤ 2 % of one core | Sampled over 60 s |
| CPU during playback, 18-band EQ + effects | ≤ 8 % of one core | Same |
| CPU with projectM visualizer | ≤ 20 % of one core + GPU | Same; auto-degrades per `REQ-UIX-039` |
| Seek to audible | ≤ 120 ms | Instrumented, local file |
| Track change latency (gapless) | **0 samples** | §8.11 test 3 |

### 20.5 Memory

`REQ-NFR-007` `[v1.0]`

| State | Budget | Measurement |
|---|---|---|
| Desktop idle, 100k library, no visualizer | **≤ 220 MB** RSS | Steady state after 5 min |
| Desktop playing + album grid + visualizer | ≤ 420 MB RSS | Same |
| Android idle, playing in background | **≤ 150 MB** PSS | `dumpsys meminfo` |
| Growth over 24 h continuous playback | **≤ 5 %** | Soak test; anything more is a leak and MUST be fixed, not budgeted |

`REQ-NFR-008` `[v1.0]` No leaks: desktop CI MUST run the unit and integration suites under **ASan + LSan** with zero leaks reported, and the audio soak test under **Valgrind or ASan** at least nightly.

### 20.6 Binary and package size

`REQ-NFR-009` `[v1.0]`

| Artifact | Budget |
|---|---|
| Windows installer | ≤ 60 MB |
| Linux `.deb` | ≤ 45 MB |
| Linux AppImage | ≤ 110 MB (bundles Qt) |
| Android APK per ABI | ≤ 25 MB |
| Android AAB | ≤ 40 MB |

`REQ-NFR-010` `[v1.0]` CI MUST report artifact sizes on every build and MUST fail if any grows more than **10 %** in one pull request without an explicit override label.

### 20.7 Battery and thermals

`REQ-NFR-011` `[v1.0]` Android background playback with the screen off MUST consume **≤ 2 % battery per hour** on the reference device, measured with Battery Historian. Requirements that follow from it: no wake locks beyond the playback foreground service, no polling loops, no periodic network activity, audio offload enabled when DSP is bypassed (`REQ-AUD-072`), and the visualizer stopped when not visible.

`REQ-NFR-012` `[v1.0]` Desktop MUST honour power state: on battery, default the resampler to Balanced, cap visualizer frame rate at 30 fps, and increase the audio buffer one step. All three MUST be overridable.

### 20.8 Reliability

`REQ-NFR-013` `[v1.0]` Crash-free session rate target **≥ 99.9 %**, measured from local crash reports voluntarily submitted plus the soak suite. Any crash reproducible from the test corpus is a release blocker.

`REQ-NFR-014` `[v1.0]` The app MUST survive, without data loss and without crashing: a source drive being unmounted mid-scan, the audio device disappearing mid-playback, the database file being made read-only, a corrupt music file in the library, a corrupt skin package, disk-full during a tag write, and being killed with `SIGKILL` at any moment.

---

## 21 · Security & Threat Model

`REQ-SEC-001` `[v1.0]` `SECURITY.md` MUST state the disclosure policy, supported versions, and expected response time. `docs/adr/` MUST contain the threat model summarised here.

### 21.1 Trust boundaries

| Boundary | Input | Trust |
|---|---|---|
| Audio files | Container/codec/tag/artwork bytes | **Untrusted** — arbitrary files from the internet |
| Playlist and cue files | Text | **Untrusted** |
| Skin packages | ZIP, JSON, SVG, images, fonts | **Untrusted** |
| Network responses | HTTP bodies, RSS, ICY, JSON | **Untrusted** |
| Sync peers | Change sets | **Authenticated but not trusted** |
| IPC / CLI | Command strings, paths | Same-user, still validated |
| Plugins | Native code | **Explicitly consented, not sandboxed** (§16.5) |

### 21.2 Parser hardening

`REQ-SEC-002` `[v1.0]` Every parser reading untrusted input MUST: validate lengths before allocating; reject rather than clamp implausible values; use checked arithmetic on every offset and size computation; have a hard input-size cap; and never trust a self-declared length field without bounds-checking it against the actual data available.

`REQ-SEC-003` `[v1.0]` Image decoding MUST probe dimensions before decode and enforce `REQ-LIB-068`'s limits. Artwork is the most common malicious-payload vector in music files.

`REQ-SEC-004` `[v1.0]` FFmpeg MUST be kept current, and CI MUST fail if the pinned version has a known CVE (§25.6). Decoding untrusted media is the highest-risk operation the app performs, and being three FFmpeg releases behind is a real exposure.

### 21.3 Skin and package safety

`REQ-SEC-005` `[v1.0]` The complete control set is §11.5: no code execution by construction, zip-slip prevention, decompression limits, SVG sanitisation, EFS totality with an output cap, and layout resource budgets. **The design goal is that installing a malicious skin can, at worst, produce an ugly or non-functional UI** — never code execution, never data exfiltration, never a hang.

`REQ-SEC-006` `[v1.0]` This property MUST be tested adversarially: a corpus of deliberately malicious packages (zip bombs, traversal paths, SVG with scripts, XML entity attacks, 10 MB EFS patterns, 100k-node layouts, symlinks, mismatched checksums) MUST be committed under `desktop/tests/data/malicious-skins/`, and every one MUST be rejected with a specific error.

### 21.4 IPC, CLI, and local surfaces

`REQ-SEC-007` `[v1.0]` The IPC endpoint MUST be restricted to the current OS user (Unix socket with `0600` in the user's runtime dir; named pipe with a user-only DACL). It MUST accept only the fixed command set (`REQ-OSI-013`), MUST cap message size, and MUST be fuzzed.

`REQ-SEC-008` `[v1.0]` Paths arriving from IPC, CLI, drag-and-drop, or a playlist MUST be canonicalised and checked before use. The app MUST NOT follow a path into a location it would not otherwise read, and MUST NOT write outside its data directories and the user's explicitly chosen destinations.

### 21.5 Database and query safety

`REQ-SEC-009` `[v1.0]` **Every** SQL statement MUST use bound parameters. String interpolation into SQL is FORBIDDEN, including in the smart-playlist compiler (`REQ-PLS-010`) — a smart playlist can arrive inside an imported settings bundle, which makes it untrusted input. A CI grep MUST flag string concatenation adjacent to SQL keywords.

`REQ-SEC-010` `[v1.0]` FTS5 query strings from user input MUST be escaped or built through the tokenizer-safe API, never passed raw — malformed FTS syntax is a denial-of-service and an error-message leak.

### 21.6 Fuzzing

`REQ-SEC-011` `[v1.0]` libFuzzer targets MUST exist, run in CI on every pull request as a short smoke (60 s each) and nightly for longer, with a committed, growing corpus:

| Target | Input |
|---|---|
| `fuzz_id3` | ID3v1/v2 frames |
| `fuzz_vorbiscomment` | Vorbis comment blocks |
| `fuzz_apev2` | APEv2 tags |
| `fuzz_mp4atoms` | MP4 metadata atoms, including `iTunSMPB` |
| `fuzz_xinglame` | Xing/Info + LAME tag (`REQ-AUD-037`) |
| `fuzz_cue` | Cue sheets |
| `fuzz_playlist` | M3U/PLS/XSPF/ASX |
| `fuzz_lrc` | LRC and enhanced LRC |
| `fuzz_theme` | Theme JSON |
| `fuzz_layout` | Layout DSL JSON |
| `fuzz_skinzip` | `.eclipseskin` archives |
| `fuzz_efs` | EFS patterns |
| `fuzz_smartrule` | Smart-playlist rules |
| `fuzz_icy` | ICY metadata streams |
| `fuzz_rss` | Podcast feeds |
| `fuzz_ipc` | IPC messages |
| `fuzz_syncmsg` | Sync protocol messages |

`REQ-SEC-012` `[v1.0]` Fuzz targets MUST build with ASan + UBSan. Any crash, hang, or sanitizer finding is a **release blocker**, and its input MUST be added to the regression corpus.

### 21.7 Supply chain

`REQ-SEC-013` `[v1.0]` All dependencies pinned: vcpkg `builtin-baseline` commit, `qt-version.txt`, Gradle version catalog with exact versions, and a Gradle dependency-verification file with checksums. No floating versions, no `latest`, no dynamic ranges.

`REQ-SEC-014` `[v1.0]` A **CycloneDX SBOM** per release artifact (§25.6), plus a licence audit and a CVE scan that fail the build on a new high-severity finding.

`REQ-SEC-015` `[v1.0]` CI MUST run **CodeQL** on C++ and Kotlin, plus `clang-tidy` with the `bugprone-*`, `cert-*`, and `cppcoreguidelines-*` checks enabled.

`REQ-SEC-016` `[v1.0]` Release artifacts MUST be signed: Windows Authenticode (certificate held in CI secrets or a cloud signing service), Android upload key with Play App Signing. Signing keys MUST NOT be in the repository, and the signing step MUST run only on tagged builds from the protected branch.

`REQ-SEC-017` `[v1.0]` Every release MUST publish **SHA-256 checksums** for all artifacts, and the release workflow MUST generate them rather than a human.

### 21.8 Hardening flags

`REQ-SEC-018` `[v1.0]` Release builds MUST enable: `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, `-fPIE`/`-pie`, `-Wl,-z,relro,-z,now`, `-Wl,-z,noexecstack` on Linux; `/GS`, `/guard:cf`, `/DYNAMICBASE`, `/HIGHENTROPYVA`, `/NXCOMPAT`, `/CETCOMPAT` on Windows. CI MUST verify the flags are present in the produced binaries, not merely in the build files.

---

## 22 · Error Handling, Logging & Observability

### 22.1 Error taxonomy

`REQ-GEN-060` `[v1.0]` Errors are modelled as values, not exceptions, across module boundaries: `Result<T, Error>` on desktop, sealed classes on Android. Exceptions MAY be used within a module for genuinely exceptional conditions, but MUST NOT cross a port boundary and MUST NEVER cross the RT thread (`REQ-AUD-015`).

`REQ-GEN-061` `[v1.0]` Every error carries: a stable **code** (namespaced, e.g. `AUD_DEVICE_LOST`), a **user-facing message** (translated, actionable, free of jargon and codes), a **technical detail** string (for logs, not the user), a **severity**, and an optional **recovery action** the UI can offer as a button.

`REQ-GEN-062` `[v1.0]` Severity ladder and required behaviour:

| Severity | Meaning | Behaviour |
|---|---|---|
| `Trace`/`Debug` | Developer detail | Log only |
| `Info` | Normal notable event | Log; no UI |
| `Notice` | User might care | Non-blocking, auto-dismissing inline notice |
| `Warning` | Degraded but working | Persistent, dismissible inline banner naming the degradation |
| `Error` | An operation failed | Inline error with a retry or fix action; **never a modal dialog for a background failure** |
| `Critical` | Core function unavailable | Modal, with a clear next step and a link to the log |

`REQ-GEN-063` `[v1.0]` **Forbidden error patterns:** silent catch-and-ignore; a message that shows only a numeric code; a modal dialog for a failure the user did not initiate; a message that blames the user; a message with no next step; a toast for something the user must act on; and repeating the same message once per file during a batch (batch failures MUST aggregate into one summary with a detail list).

### 22.2 Logging

`REQ-GEN-064` `[v1.0]` One logging facade, six levels (`trace`, `debug`, `info`, `warn`, `error`, `critical`), configurable at runtime without a restart and per-subsystem (`audio`, `library`, `theme`, `net`, `sync`, `plugin`, `ui`).

`REQ-GEN-065` `[v1.0]` Format: ISO-8601 timestamp with milliseconds, level, subsystem, thread name, message, and structured key–value context. Machine-parseable, and greppable by a human without tooling.

`REQ-GEN-066` `[v1.0]` **The RT thread MUST NOT log.** It writes to a pre-allocated lock-free event ring; a normal-priority thread drains that ring and logs. This is the only permitted mechanism (`REQ-AUD-015`).

`REQ-GEN-067` `[v1.0]` Rotation: 5 files × 10 MB maximum, oldest deleted. Logs MUST never grow unbounded, and MUST be capped even at `trace`.

`REQ-GEN-068` `[v1.0]` Redaction per `REQ-SET-013`. A built-in log viewer MUST provide: level filtering, subsystem filtering, text search, "copy for bug report" (which strips paths and warns about `debug` content), and a button to open the containing folder.

### 22.3 Diagnostics

`REQ-GEN-069` `[v1.0]` A diagnostics panel MUST show live: current audio backend, device, negotiated format, share mode, buffer size, reported latency, underrun count, RT callback timing p50/p99, DSP stages active, decoder in use, ring-buffer fill, library counts, DB size, cache size, active theme/skin, loaded plugins, and the build's version and commit hash.

`REQ-GEN-070` `[v1.0]` A **"Copy diagnostics"** action produces a redacted, paste-ready report for bug filing. Every good bug report starts here, and its absence costs maintainers more time than any feature saves.

---

## 23 · Testing Strategy

### 23.1 Shape and gates

`REQ-TST-010` `[v1.0]` Distribution target: **70 %** unit, **20 %** integration, **10 %** UI/end-to-end. Coverage gates enforced in CI:

| Scope | Line coverage gate |
|---|---|
| Domain layer (layer 3), both platforms | **≥ 90 %** |
| Audio DSP and gapless logic | **≥ 90 %** |
| EFS engine, smart-playlist compiler, theme validator | **≥ 95 %** |
| Tag, cue, playlist, LRC parsers | **≥ 90 %** |
| Library index and migrations | ≥ 85 % |
| Adapters (layer 1) | ≥ 60 % |
| Overall | ≥ 75 % |

`REQ-TST-011` `[v1.0]` Coverage MUST NOT be gamed: tests asserting nothing, or asserting only that a call did not throw, MUST be rejected in review. A test's name MUST state the behaviour it pins.

### 23.2 Unit tests

`REQ-TST-012` `[v1.0]` GoogleTest on desktop, JUnit 5 + Turbine on Android. Mandatory unit coverage for: every tag parser per format; cue parsing including all the malformed cases in `REQ-LIB-043`; every playlist format round-trip; EFS (all 150+ conformance cases); smart-playlist compilation to SQL with parameter binding asserted; biquad coefficient computation against analytically-derived values; ReplayGain/BS.1770 loudness against the reference test vectors; gapless offset computation per format; theme validation against the conformance fixtures; sort-key normalisation including the Unicode cases in `REQ-LIB-033`; and the sync conflict-resolution rules per field.

### 23.3 Property-based tests

`REQ-TST-013` `[v1.0]` Use property-based testing (RapidCheck or equivalent; Kotest property on Android) for:

- **EFS**: any pattern parses or fails cleanly, never crashes, never exceeds the output cap.
- **Smart playlists**: any generated rule compiles to valid SQL with all literals bound.
- **Sync**: any permutation of a change-log set converges to the same state (`REQ-SYN-011`).
- **Ring buffer**: any interleaving of producer/consumer operations preserves FIFO order and never reads uninitialised memory.
- **Path handling**: any path string is either rejected or canonicalised inside the allowed root (`REQ-SEC-008`).

### 23.4 Integration tests

`REQ-TST-014` `[v1.0]` Desktop integration tests MUST cover: decode correctness for every format against the golden corpus; all fifteen audio verifications in §8.11; database migrations across every version pair; a full scan of a synthetic 10,000-file tree including pathological names; skin package installation for every valid and every malicious fixture; and MPRIS2 driven from an external D-Bus client (`REQ-OSI-024`).

### 23.5 Concurrency tests

`REQ-TST-015` `[v1.0]` ThreadSanitizer builds MUST run: the RT-safety soak (`REQ-AUD-018`), concurrent library writes with concurrent reads, and parameter publication under contention. Zero findings is the gate — TSan findings are not flaky, they are races.

`REQ-TST-016` `[v1.0]` A custom allocator hook MUST assert **zero allocations** inside the audio callback across a 60-second run (§8.11 test 10).

### 23.6 UI tests

`REQ-TST-017` `[v1.0]` Desktop: QTest for widget and QML interaction covering play a track, build a playlist, edit tags, install a skin, and change output device. Android: Compose UI tests plus Espresso for the same flows, plus the Auto browse tree (`REQ-AUT-021`).

`REQ-TST-018` `[v1.0]` **Screenshot tests** for every built-in theme × every built-in skin × 3 window sizes × 2 DPI scales × {LTR, RTL} × {normal, pseudo-localised}, with a tolerance-based image diff. This is how truncation, clipping, and contrast regressions are caught before a user sees them.

### 23.7 Accessibility tests

`REQ-TST-019` `[v1.0]` Automated: contrast computation over every theme (`REQ-THM-041`); every interactive element has a non-empty accessible name and role; tab order matches visual order; no focus traps; touch-target sizes; and text-scale layout at 200 % without clipping.

`REQ-TST-020` `[v1.0]` Manual, per release, recorded in `docs/TESTING.md`: complete a full playback and playlist-building session using **only** the keyboard; then again using **only** a screen reader (NVDA on Windows, Orca on Linux, TalkBack on Android). Automated checks cannot substitute for this.

### 23.8 Conformance tests

`REQ-TST-021` `[v1.0]` Both platforms MUST run `shared-spec/conformance/**` and produce identical results (`REQ-GEN-031`). `spec-ci.yml` MUST fail if the two platforms disagree on any fixture, and MUST fail if a schema changes without its fixtures being updated.

### 23.9 Privacy tests

`REQ-TST-022` `[v1.0]` **Firewall test:** run the app with all network access denied at the OS level and exercise every offline feature — scan, play, tag, skin, playlist, sync-disabled. There MUST be no hang, no error dialog, and no functional loss.

`REQ-TST-023` `[v1.0]` **Zero-connection test:** with the global network switch off (default), capture network activity for a 10-minute session covering startup, scan, playback, and settings browsing. Assert **zero** outbound connections. This test is the proof behind `REQ-SET-010`.

`REQ-TST-024` `[v1.0]` **Dependency denylist scan** in CI over the resolved graph, blocking any analytics, attribution, advertising, or crash-reporting SDK.

### 23.10 Soak and stress

`REQ-TST-025` `[v1.0]` Nightly: 24-hour continuous playback asserting zero underruns and ≤5 % memory growth; a 500,000-track scan; 10,000 rapid track changes; 1,000 device switches; and 100 skin switches. Each asserts no crash, no leak, and no state corruption.

`REQ-TST-026` `[v1.0]` **Chaos tests**: randomly delete, truncate, and corrupt files mid-scan; randomly remove the audio device mid-playback; randomly `SIGKILL` and restart, asserting recoverable state each time (`REQ-NFR-014`).

### 23.11 Golden corpus

`REQ-TST-027` `[v1.0]` `desktop/tests/data/` MUST hold a manifest, and `tools/corpus-fetch` MUST generate or fetch the corpus (never committing large binaries). Contents in §29.4. Every file MUST be either generated by the tool or licensed for redistribution — no copyrighted music in the repository, ever.

---

## 24 · Build & Toolchain

### 24.1 Desktop

`REQ-BLD-001` `[v1.0]` Exact, copy-pasteable steps MUST be in `docs/BUILDING.md` and MUST work on a clean machine. Canonical flow:

```bash
# 1. Qt (pinned; NOT via vcpkg — see §6.2)
pip install "aqtinstall==3.*"
aqt install-qt linux desktop "$(cat desktop/qt-version.txt)" gcc_64 \
    -m qtdeclarative qtsvg qttools qtshadertools qtwayland -O "$HOME/Qt"

# 2. vcpkg (manifest mode; baseline pinned in vcpkg.json)
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"

# 3. Configure + build + test via presets
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

`REQ-BLD-002` `[v1.0]` `CMakePresets.json` MUST define at minimum: `linux-debug`, `linux-release`, `linux-asan`, `linux-tsan`, `windows-debug`, `windows-release`, plus matching build and test presets. A contributor MUST never need to remember a flag.

`REQ-BLD-003` `[v1.0]` Warnings-as-errors in CI: `-Wall -Wextra -Wpedantic -Werror` plus `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual -Wdouble-promotion -Wformat=2` on GCC/Clang; `/W4 /WX /permissive-` on MSVC. Suppressions MUST be local, narrow, and commented with a reason.

`REQ-BLD-004` `[v1.0]` `.clang-format` and `.clang-tidy` MUST be checked in and enforced in CI. Formatting MUST NOT be a review topic.

`REQ-BLD-005` `[v1.0]` Sanitizer presets MUST work and MUST be part of CI: ASan+UBSan for the general suites, TSan for concurrency, LSan for leaks.

`REQ-BLD-006` `[v1.0]` The build MUST be reproducible enough that two builds of the same commit on the same image produce byte-identical binaries where the toolchain allows: no `__DATE__`/`__TIME__`, `SOURCE_DATE_EPOCH` honoured, and paths normalised via `-ffile-prefix-map`.

`REQ-BLD-007` `[v1.0]` Version information MUST be generated from git (tag + commit + dirty flag) into `version.hpp` at configure time, and MUST be visible in About and in the diagnostics report.

### 24.2 Android

`REQ-BLD-010` `[v1.0]` `./gradlew build` MUST succeed on a clean checkout with only a JDK 21 and the Android SDK present. All versions come from `gradle/libs.versions.toml`; inline dependency versions are forbidden.

`REQ-BLD-011` `[v1.0]` Kotlin **explicit API mode** enabled; `allWarningsAsErrors` in CI; `ktlint` and `detekt` clean; R8 full mode for release with checked-in keep rules and a mapping file retained per release.

`REQ-BLD-012` `[v1.0]` Gradle **configuration cache** and **build cache** enabled; dependency verification with checksums committed.

### 24.3 Packaging

`REQ-BLD-013` `[v1.0]` **Windows:** `windeployqt` to gather Qt, then a **WiX MSI** as the primary installer (per-user by default, per-machine optional) plus an NSIS `.exe` for the portable-friendly path. The installer MUST offer, never assume, file associations (`REQ-OSI-008`), and MUST cleanly uninstall (`REQ-OSI-010`).

`REQ-BLD-014` `[v1.0]` **Linux:** a CPack `.deb` with correct `Depends` on system Qt where feasible, plus an **AppImage** built with `linuxdeploy` for distro-independent use. The `.deb` MUST install the `.desktop` file, MIME XML, icons at every size, and a man page. `lintian` MUST pass with no errors.

`REQ-BLD-015` `[v1.0]` **Android:** an AAB for Play plus per-ABI APKs for direct download. Both signed (`REQ-SEC-016`).

`REQ-BLD-016` `[v1.0]` Every artifact MUST be smoke-tested by CI after packaging: install it in a clean container or VM, launch it, play a bundled test tone, verify no crash, and verify the version string — then uninstall and verify cleanliness. Shipping an installer nobody installed is how release-day disasters happen.

---

## 25 · CI/CD

### 25.1 `android-ci.yml`

`REQ-BLD-020` `[v1.0]` Triggers on push and pull request touching `android/**`, `shared-spec/**`, or the workflow itself.

Jobs: set up JDK 21 + Android SDK → restore Gradle cache → `ktlint` → `detekt` → assemble debug → unit tests with coverage gate → instrumented tests on an API 26 and an API 34 emulator → macrobenchmark (startup, scroll jank) with budget assertions (§20) → conformance fixtures (`REQ-TST-021`) → dependency denylist (`REQ-TST-024`) → upload the debug APK as an artifact.

On a version tag: build the signed release AAB and per-ABI APKs, generate the SBOM, and upload to the release.

### 25.2 `desktop-ci.yml`

`REQ-BLD-021` `[v1.0]` Matrix: `[windows-latest, ubuntu-22.04, ubuntu-24.04]`, with `arm64` cross-builds on tags.

Jobs per platform: checkout → install Qt via `aqtinstall` (cached by version) → set up vcpkg with **binary caching** (GitHub Actions cache or NuGet feed) → `clang-format` check → CMake configure via preset → build → `ctest` → `clang-tidy` → `cppcheck` → coverage gate (Linux) → ASan/UBSan suite (Linux) → TSan concurrency suite (Linux) → fuzz smoke, 60 s per target → conformance fixtures → `tools/theme-validate` over every bundled skin → licence assertion (`REQ-GEN-015`) → package → artifact smoke test (`REQ-BLD-016`) → size report (`REQ-NFR-010`).

`REQ-BLD-022` `[v1.0]` vcpkg **binary caching is mandatory**, not optional. Without it every job rebuilds every dependency and the pipeline becomes unusable. Qt install MUST likewise be cached by version key.

### 25.3 `spec-ci.yml`

`REQ-BLD-023` `[v1.0]` Triggers on `shared-spec/**`. Validates every JSON Schema is itself valid; validates every bundled theme, skin, and fixture against its schema; asserts every schema change is accompanied by a fixture change; and asserts the desktop and Android conformance results agree.

### 25.4 `security.yml`

`REQ-BLD-024` `[v1.0]` Scheduled daily plus on pull request: CodeQL (C++, Kotlin) → CVE scan against pinned dependency versions, failing on new high severity (`REQ-SEC-004`) → licence audit against §4.2, failing on any dependency not in the register (`REQ-GEN-012`) → SBOM generation and diff against the previous release → extended fuzzing (15 minutes per target) with corpus persistence → hardening-flag verification on the produced binaries (`REQ-SEC-018`).

### 25.5 `release.yml`

`REQ-BLD-025` `[v1.0]` Triggered by a `vX.Y.Z` tag on the protected branch. It MUST:

1. Validate the tag is semver and matches the version in the build files.
2. Refuse to proceed if any `[v1.0]` requirement is marked unmet in the release checklist (for the 1.0.0 tag specifically).
3. Run the full desktop and Android pipelines.
4. Build every artifact: Windows `x64` + `arm64` MSI and EXE; Linux `x86_64` + `arm64` `.deb` and AppImage; Android AAB and per-ABI APKs.
5. Sign everything (`REQ-SEC-016`).
6. Generate SBOMs and SHA-256 checksums.
7. Generate the changelog from conventional commits since the previous tag.
8. Regenerate `docs/THIRD-PARTY.md` and the LGPL source-offer entries for this tag (`REQ-GEN-020`) and fail if they are stale.
9. Publish **one unified GitHub Release** with Android, Windows, and Ubuntu artifacts side by side.
10. Publish the release only after every artifact smoke test passes.

`REQ-BLD-026` `[v1.0]` Release artifact naming MUST be consistent and machine-parseable:
```
eclipse-player-<version>-<os>-<arch>.<ext>
eclipse-player-1.0.0-windows-x64.msi
eclipse-player-1.0.0-linux-x86_64.AppImage
eclipse-player-1.0.0-android-arm64-v8a.apk
```

### 25.6 Compliance automation

`REQ-BLD-027` `[v1.0]` These are **mechanical gates**, not review checklists, because a policy nobody can enforce is a policy nobody follows:

| Gate | Fails the build when |
|---|---|
| FFmpeg licence assertion | `avutil_license()` contains `GPL` or `nonfree` (`REQ-GEN-015`) |
| Qt linkage assertion | Qt is statically linked (`REQ-GEN-013`) |
| Dependency register | A linked dependency is absent from §4.2 |
| Denylist | An analytics/attribution/ads/crash SDK appears in the graph |
| CVE scan | A new high-severity CVE affects a pinned version |
| Layer rules | A forbidden include or import crosses a layer boundary (`REQ-GEN-051`) |
| SQL safety | String concatenation appears adjacent to SQL keywords (`REQ-SEC-009`) |
| RT safety | A call from the RT path targets a function lacking `/// RT-SAFE:` (`REQ-AUD-017`) |
| i18n | A UI string literal appears outside a translation call (`REQ-UIX-070`) |
| Public API docs | A public symbol lacks a doc-comment (`REQ-GEN-054`) |
| Artifact size | Growth > 10 % without an override label (`REQ-NFR-010`) |
| Schema/fixture sync | A schema changed without fixtures (`REQ-BLD-023`) |

---

## 26 · Versioning & Release Policy

`REQ-BLD-030` `[v1.0]` **Semantic versioning from commit #1.** MAJOR for breaking changes to the plugin ABI, the theme/layout schema, the sync protocol, or the settings-export format. MINOR for features. PATCH for fixes.

`REQ-BLD-031` `[v1.0]` **Conventional commits**, enforced by `commitlint` in CI. Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `ci`, `chore`, `revert`. Scopes mirror the requirement areas from §0.2. Every commit touching behaviour MUST reference its requirement ID in the body.

`REQ-BLD-032` `[v1.0]` **Branching:** trunk-based on `main`, which is protected and always releasable. Short-lived feature branches. Release branches (`release/1.x`) only for backporting fixes to a shipped minor.

`REQ-BLD-033` `[v1.0]` **Channels:** `stable` (tagged releases), `beta` (pre-release tags `vX.Y.Z-beta.N`), `nightly` (built from `main`, clearly marked, never auto-suggested to stable users). Users MUST be able to choose their channel, and the app MUST never silently move a user between channels.

`REQ-BLD-034` `[v1.0]` **Deprecation policy:** a deprecated plugin-ABI function, schema token, or settings key MUST continue to work for at least **two minor versions**, MUST log a deprecation warning in dev/nightly builds, and MUST be listed in `CHANGELOG.md` under a `Deprecated` heading at the time of deprecation, with the removal version stated.

`REQ-BLD-035` `[v1.0]` **Schema versioning.** `theme-schema.json`, `layout.schema.json`, `settings.schema.json`, and the sync protocol each carry an independent integer version. Additive changes keep the version and MUST be ignored gracefully by older readers (`REQ-THM-052`). Removals or semantic changes increment it and require a documented migration on the reading side.

`REQ-BLD-036` `[v1.0]` **`CHANGELOG.md`** is generated but MUST be human-curated before a release: a short "Highlights" section in plain language above the generated detail. A changelog that is only a commit dump is not a changelog.

`REQ-BLD-037` `[v1.0]` **Release checklist** in `docs/` MUST be completed and archived per release, covering: all CI green; the Android Auto manual checklist (`REQ-AUT-020`); the manual accessibility passes (`REQ-TST-020`); the artifact smoke tests; the privacy tests; the licence and SBOM regeneration; and the changelog curation.

---

## 27 · Documentation Deliverables

`REQ-GEN-075` `[v1.0]` Each document below MUST exist with at least the stated content. Documentation is a deliverable, not an afterthought, and CI MUST fail on broken internal links.

| File | Required content |
|---|---|
| `README.md` | What it is, screenshots, the three differentiators, feature summary, install instructions per platform, build quick-start, licence, how to contribute |
| `docs/ARCHITECTURE.md` | The five layers and dependency rules (§7), the thread inventory, the data flows, and the stack rationale from `REQ-GEN-040` — including *why the sink layer is ours* |
| `docs/AUDIO-ENGINE.md` | The signal chain, RT-safety rules, gapless algorithms per format with the exact formulas, the bit-perfect contract, and how each claim is verified |
| `docs/API.md` | Every public module API, generated from doc-comments, with thread- and RT-safety noted per function |
| `docs/SKIN-AUTHORING.md` | The two-tier model **and the security reasoning behind it** (`REQ-THM-002`), every token, the complete layout component/binding/action reference, EFS reference, packaging steps, validation errors explained, and the trust-badge meaning (`REQ-THM-043`) |
| `docs/PLUGIN-AUTHORING.md` | `[v1.x]` The ABI, all seven categories with compilable examples, capability model, RT-safety rules, stability promise |
| `docs/BUILDING.md` | Clean-machine steps per platform, the Qt-vs-vcpkg split and why, optional components (ASIO), troubleshooting |
| `docs/TESTING.md` | How to run each suite, the reference hardware, the golden corpus, the Android Auto manual checklist, the manual accessibility checklist |
| `docs/THIRD-PARTY.md` | Generated; the §4.2 register with SPDX ids, versions, licence texts, and source URLs |
| `docs/LGPL-SOURCE-OFFER.md` | Per-tag source links for every LGPL component (`REQ-GEN-020`) |
| `docs/PRIVACY.md` | What is stored, where, what leaves the device under which action, what never leaves, and how to verify it |
| `docs/PARITY.md` | Generated from §29.2 — the honest per-platform feature matrix |
| `docs/ROADMAP.md` | `[v1.x]` and `[v2]` items with rationale, plus the non-goals from §2.4 |
| `docs/OPEN-QUESTIONS.md` | Anything the implementation had to assume, with the assumption made |
| `docs/adr/` | One file per decision, minimum: project licence, audio output, no-code-in-skins, C ABI for plugins, Qt acquisition, sync wire format |
| `CONTRIBUTING.md` | Setup, style, commit format, review expectations, Definition of Done (§1.3), how to add a requirement |
| `SECURITY.md` | Disclosure process, supported versions, response expectations |
| `CODE_OF_CONDUCT.md` | Contributor Covenant or equivalent, with a named contact |

---

## 28 · Build Phases & Exit Gates

> Work strictly in order. A phase is complete only when **every** exit gate is green in CI on **every** platform. Do not begin the next phase before then — this ordering exists because each phase's gates protect the next phase's assumptions.

### Phase 0 — Foundation

**Build:** repository skeleton per §5; MPL-2.0 licence; `.editorconfig`, `.clang-format`, `.clang-tidy`, commitlint; CMake + presets producing a Qt "hello window" on Windows and Ubuntu; Gradle producing an empty Compose app; `shared-spec/schemas/theme-schema.json` v1; all five CI workflows running; `docs/ARCHITECTURE.md` and `docs/adr/0001-project-license.md`.

**Exit gates:**
1. `desktop-ci.yml` green on `windows-latest`, `ubuntu-22.04`, `ubuntu-24.04` — window opens, `ctest` runs (even with one trivial test).
2. `android-ci.yml` green — APK builds and installs on an emulator.
3. `spec-ci.yml` green — `theme-schema.json` is a valid JSON Schema.
4. Warnings-as-errors active; `clang-format` and `ktlint` enforced.
5. vcpkg binary caching and Qt caching demonstrably working (second run substantially faster).
6. Layer-rule enforcement script exists and passes (`REQ-GEN-051`).
7. Version string generated from git and shown in About.

### Phase 1 — Playback core

**Build:** `IDecoder` + FFmpeg adapter; `IAudioSink` + WASAPI (shared) + ALSA + PulseAudio; lock-free ring buffers; the RT thread; the gapless scheduler; transport fades; Media3 pipeline on Android; the FFmpeg licence assertion.

**Exit gates:**
1. Plays MP3, FLAC, WAV, AAC, Ogg Vorbis, Opus, ALAC, WavPack on all three platforms.
2. §8.11 tests **1, 2, 3, 9, 10, 13, 15** pass — decode correctness, **null test**, **gapless sample-exactness**, RT safety under TSan, zero allocations in the callback, pause losslessness, seek exactness.
3. Gapless verified sample-exactly for MP3 (LAME), AAC (`iTunSMPB`), FLAC, Opus, Vorbis.
4. Zero underruns in a 1-hour playback test.
5. FFmpeg licence assertion green (`REQ-GEN-015`).
6. Device loss and recovery test passes (§8.11 test 12).

### Phase 2 — Library

**Build:** the full §9.4 schema on both platforms with migrations; scanner with incremental mode and watchers; TagLib layer; cue-sheet support; artwork pipeline; FTS5 search; playlist import/export; the queue; smart-playlist compiler; EFS engine.

**Exit gates:**
1. 100,000-track scan meets the throughput budget (`REQ-NFR-004`); incremental re-scan ≤ 8 s.
2. Search meets the 80 ms budget.
3. All EFS conformance cases pass **identically on both platforms** (`REQ-TST-021`).
4. All smart-playlist conformance cases pass on both platforms; every literal proven bound (`REQ-SEC-009`).
5. Every migration test passes in both directions of the version matrix.
6. Cue-sheet tracks play gaplessly and sample-accurately (`REQ-LIB-044`).
7. Unicode corpus round-trips (`REQ-LIB-033`), including Windows long paths.
8. Fuzz targets for tags, cue, playlist, EFS, smart-rule all clean at 60 s.
9. Missing-file policy verified: unmount a source, confirm zero data loss.

### Phase 3 — UI/UX v1

**Build:** design tokens consumed from `shared-spec`; the theme engine (Tier 1) on both platforms; the four built-in themes; library views; Now Playing; mini-player; the command registry and shortcut map; preferences; i18n pipeline with `en` and `id`.

**Exit gates:**
1. All four themes pass contrast validation (`REQ-THM-041`); High Contrast meets AAA.
2. Screenshot tests pass across the full matrix in `REQ-TST-018`.
3. Every command reachable by keyboard; command palette functional; zero focus traps.
4. Automated accessibility checks pass (`REQ-TST-019`).
5. Startup and scroll budgets met (§20.2, §20.3).
6. Zero hard-coded UI strings (`REQ-UIX-070` gate green); `id` locale complete.
7. RTL pseudo-locale screenshots show no clipping.

### Phase 4 — OS integration & Android Auto

**Build:** SMTC, tray, thumbbar, file associations, single-instance IPC, CLI, safe mode, portable mode; MPRIS2, `.desktop`, MIME, XDG, inhibit; global hotkeys with the Wayland fallback; Media3 `MediaLibraryService`, notification, audio focus, widgets; the Android Auto browse tree, search, and custom actions.

**Exit gates:**
1. SMTC verified manually in the Windows volume flyout, including artwork and the scrubber.
2. MPRIS2 integration test green from an external D-Bus client (`REQ-OSI-024`); verified manually in GNOME and KDE.
3. Media keys work on Windows, X11, and Wayland (via MPRIS), with the Wayland limitation documented in §29.2.
4. IPC fuzz target clean; IPC permissions verified.
5. Audio focus matrix verified on-device for every case in `REQ-OSI-041`; becoming-noisy pauses.
6. **The full Android Auto DHU checklist (`REQ-AUT-020`) passes**, including the 20,000-track pagination case and offline voice search.
7. Auto instrumentation tests green, including a rejected caller (`REQ-AUT-003`).

### Phase 5 — Skin engine

**Build:** `.eclipseskin` package format; the layout DSL and interpreter; the full validation pipeline; the malicious-package corpus; skin browser and installer; hot-reload dev mode; the three built-in skins; `tools/theme-validate`; `docs/SKIN-AUTHORING.md`.

**Exit gates:**
1. All three built-in skins render correctly and produce **visibly different layouts**, proving the layout tier is real.
2. **Every** malicious fixture is rejected with a specific error (`REQ-SEC-006`); `fuzz_skinzip`, `fuzz_theme`, `fuzz_layout` clean.
3. Zip-slip, zip-bomb, SVG-script, and XML-entity cases each individually verified.
4. Skin switching does not interrupt playback by one sample, and completes within `motion.duration.normal`.
5. Hot-reload works within 300 ms; validation errors shown in-app.
6. A skin that omits essential controls still yields a usable, closable player (`REQ-THM-032`).
7. Layout resource budgets enforced with precise error messages.

### Phase 6 — Advanced playback

**Build:** the full DSP chain (EQ 10/18/parametric, effects, channel matrix, limiter); ReplayGain reader **and scanner**; tempo/pitch/speed; resampler tiers and dither; WASAPI exclusive and ALSA `hw:`; bit-perfect mode; visualizers including projectM; lyrics with enhanced LRC; bookmarks, A-B repeat, sleep timer, auto-resume; Android DSP parity.

**Exit gates:**
1. §8.11 tests **4, 5, 6, 7, 8, 11, 14** pass — equal-power crossfade, **bit-perfect loopback**, EQ transfer function within ±0.25 dB, THD+N below the thresholds, no clipping, latency accuracy, **desktop/Android DSP parity within −90 dBFS**.
2. Bit-perfect mode engages on real hardware and every condition in `REQ-AUD-075` is individually verified; the indicator reports the precise blocking reason when it cannot.
3. ReplayGain scanner output matches reference BS.1770 vectors.
4. CPU and memory budgets met with the full chain engaged (§20.4, §20.5).
5. projectM loads and cycles MilkDrop presets; visualizer auto-degrades under frame pressure and stops when hidden.
6. Enhanced LRC karaoke highlighting is frame-accurate; `fuzz_lrc` clean.

### Phase 7 — Ecosystem & network `[v1.x]`

**Build:** plugin SDK and host; internet radio with ICY and recording; podcasts; metadata lookup; scrobbling; converter; skin editor; localization editor; icon packs; windowshade and detachable panels.

**Exit gates:**
1. A working example plugin exists for **each** of the seven categories and loads, initialises, and unloads cleanly.
2. Crash quarantine and the RT watchdog verified with deliberately broken plugins.
3. Every network feature is off by default; the **zero-connection test (`REQ-TST-023`) still passes**.
4. `fuzz_icy`, `fuzz_rss`, `fuzz_syncmsg` clean.
5. Credentials stored only in the OS secret store, verified by inspection.
6. Converter round-trip preserves tags and artwork; ReplayGain written on output.

### Phase 8 — Sync `[v1.x]`

**Build:** mDNS discovery; PAKE pairing; TLS 1.3 mutual auth; the change-log merge engine; `shared-spec/sync-protocol.md` with the threat model; the optional relay server.

**Exit gates:**
1. Two devices pair and converge; the property-based convergence test passes over random change-log permutations (`REQ-SYN-011`).
2. Play counts **sum** rather than overwrite; every per-field rule in `REQ-SYN-009` has a passing test.
3. Tombstones prevent resurrection across a 90-day-offline simulation.
4. Every threat in `REQ-SYN-014` has a verified mitigation; a replay attack and an unpaired-peer connection are both refused.
5. Sync remains fully disableable, and the app is unaffected with it off.

### Phase 9 — Hardening & 1.0.0

**Build:** the accessibility and i18n passes; all remaining fuzz targets; the soak and chaos suites; signing; SBOM; installers; the release pipeline; all documentation.

**Exit gates:**
1. **Every `[v1.0]` requirement in this document is met**, verified against a checklist that maps each requirement ID to its test or manual verification.
2. Manual keyboard-only and screen-reader passes complete on all three platforms (`REQ-TST-020`).
3. 24-hour soak: zero underruns, ≤5 % memory growth; chaos tests pass.
4. All fuzz targets clean at 15 minutes each.
5. Every compliance gate in §25.6 green.
6. Every artifact installs, launches, plays, and uninstalls cleanly in a clean VM (`REQ-BLD-016`).
7. `docs/PARITY.md` accurate and honest; `docs/THIRD-PARTY.md` and the LGPL source offer regenerated for the tag.
8. Release checklist archived (`REQ-BLD-037`).
9. Tag `v1.0.0`.

---

## 29 · Appendices

### 29.1 Appendix A — Format support matrix

Legend: **✔** implemented · **○** planned in the noted tier · **—** not supported · `[NG]` non-goal.

| Format | Decode | Tag read | Tag write | Gapless | Bit-perfect eligible |
|---|---|---|---|---|---|
| MP3 | ✔ v1.0 | ✔ | ✔ | ✔ Xing/LAME | ✔ |
| FLAC | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| WAV / RF64 | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| AIFF | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| AAC (LC/HE) | ✔ v1.0 | ✔ | ✔ | ✔ `iTunSMPB` | ✔ |
| ALAC | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| Ogg Vorbis | ✔ v1.0 | ✔ | ✔ | ✔ granule | ✔ |
| Opus | ✔ v1.0 | ✔ | ✔ | ✔ pre-skip | ✔ |
| WavPack | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| Monkey's Audio | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| Musepack | ✔ v1.0 | ✔ | ✔ | ✔ native | ✔ |
| WMA / WMA Pro | ✔ v1.0 | ✔ | ✔ | best-effort | ✔ |
| WMA Lossless | ○ v1.x | ✔ | ✔ | ✔ native | ✔ |
| AC-3 / E-AC-3 | ○ v1.x | ✔ | — | best-effort | ✔ |
| TAK / TTA / Shorten | ○ v1.x | ✔ | ✔ | ✔ native | ✔ |
| DSD → PCM | ○ v1.x | ✔ | — | ✔ native | — (PCM output) |
| DSD native / DoP | ○ v2 | — | — | — | ○ v2 |
| Audio CD | ○ v2 | CD-Text | — | — | ✔ |
| Tracker modules | ○ v2 | ✔ | — | n/a | — |
| MIDI / KAR | ○ v2 | ✔ | — | n/a | — |
| Video of any kind | `[NG]` | — | — | — | — |

**Playlists:** M3U ✔ · M3U8 ✔ (default export) · PLS ✔ · XSPF ✔ · CUE ✔ (import, as library) · ASX/WAX/WVX ✔ (read) · `.ecpl` ✔ (native)

**Tags:** ID3v1.1 ✔ · ID3v2.2/2.3/2.4 ✔ · APEv2 ✔ · Vorbis comments ✔ · MP4/iTunes atoms ✔ · ASF attributes ✔ · WAVE `INFO`/`bext` ✔ · Lyrics3v2 ✔ (read) · embedded cue ✔ · CD-Text ○ v2 · custom/flexible tags ✔

### 29.2 Appendix B — Platform parity matrix

`REQ-GEN-080` `[v1.0]` This table MUST be kept accurate, MUST be published as `docs/PARITY.md`, and MUST be updated in the same commit as any feature that changes it. AIMP publishes an equivalent document for Windows vs. Linux; honest parity disclosure prevents bug reports and builds trust.

| Feature | Windows | Ubuntu | Android | Note |
|---|---|---|---|---|
| Gapless, sample-exact | ✔ | ✔ | ✔ | |
| Crossfade | ✔ | ✔ | ✔ | Android needs dual players — ExoPlayer has no native crossfade |
| Bit-perfect / exclusive | ✔ WASAPI excl. | ✔ ALSA `hw:` | — | Android has no exclusive-mode API |
| Full DSP chain, identical coefficients | ✔ | ✔ | ✔ | Verified by `REQ-AUD-108` |
| 18-band EQ | ✔ | ✔ | ✔ | Custom `AudioProcessor`, not `audiofx` |
| ASIO | ○ v1.x, opt-in build | n/a | n/a | Steinberg SDK user-supplied (§4.6) |
| JACK | n/a | ○ v1.x | n/a | |
| Native PipeWire | n/a | ○ v1.x | n/a | Until then, via ALSA/Pulse compat — **not** via RtAudio, which has no PipeWire backend |
| OS media controls | ✔ SMTC | ✔ MPRIS2 | ✔ MediaSession | |
| Global hotkeys | ✔ `RegisterHotKey` | ✔ X11; **portal-dependent on Wayland** | n/a | `REQ-KEY-013` — Wayland has no grab protocol; MPRIS carries media keys |
| System tray | ✔ | ✔ where the DE has one | n/a | GNOME has no tray by default (`REQ-OSI-028`) |
| Recursive filesystem watching | ✔ `ReadDirectoryChangesW` | ✔ `inotify` | **Periodic scan only** | Android has no recursive watch API |
| Skin Tier 1 (Theme) | ✔ | ✔ | ✔ | |
| Skin Tier 2 (layout DSL) | ✔ v1.0 | ✔ v1.0 | ○ v1.x | |
| projectM / MilkDrop presets | ✔ | ✔ | ✔ | |
| Plugin SDK | ○ v1.x | ○ v1.x | — | Native plugins are desktop-only by design |
| Windowshade / detachable panels | ○ v1.x | ○ v1.x | n/a | |
| Android Auto | n/a | n/a | ✔ | |
| Tag writing | ✔ | ✔ | ✔ SAF-permitted locations only | |
| Portable mode | ✔ | ✔ | n/a | |
| Audio converter | ○ v1.x | ○ v1.x | ○ v1.x (+ audio cutter) | |
| Sync | ○ v1.x | ○ v1.x | ○ v1.x | |
| `arm64` release artifact | ✔ | ✔ | ✔ | `arm64` desktop is built but not CI-*tested* in 1.0.0 |

### 29.3 Appendix C — Terminology

See §0.4. Additional terms introduced later in this document:

| Term | Definition |
|---|---|
| **Tier 1 / Tier 2** | Theme (tokens only) vs. Skin (tokens + declarative layout). §11.1. |
| **EFS** | Eclipse Format Strings. §10. |
| **Sink** | An `IAudioSink` implementation. §8.7. |
| **Null test** | Verifying that a bypassed DSP chain returns bit-identical audio. §8.11 test 2. |
| **Quarantine** | Automatic disabling of a plugin that crashed or exceeded its budget. §16.5. |
| **Lamport clock** | Logical counter used to order sync changes without trusting wall-clock time. §18.3. |
| **Change log** | The append-only table of field-level changes that sync merges. §9.4, §18.3. |
| **Golden corpus** | The generated/licensed audio test-file set used for decode and gapless verification. §29.4. |

### 29.4 Appendix D — Golden test corpus

`REQ-TST-030` `[v1.0]` `tools/corpus-fetch` MUST generate or fetch every item below. **No copyrighted audio may be committed.** Generated tones and noise cover most needs; anything fetched MUST be explicitly redistributable.

**Generated signals** (per format in §29.1, at 44.1/48/96/192 kHz and 16/24-bit where the format allows):

1. 1 kHz full-scale sine — THD+N and level reference.
2. Multitone sweep 20 Hz–20 kHz — EQ transfer-function verification.
3. White and pink noise — spectral verification.
4. Digital silence — dither and noise-floor verification.
5. Full-scale square wave — inter-sample-peak and limiter verification.
6. An impulse — DSP parity verification (`REQ-AUD-108`).
7. A known continuous 60 s signal, **split into two files by each encoder** — the gapless corpus (`REQ-AUD-035`).

**Metadata fixtures:**

8. ID3v2.3 with CP1251 Cyrillic mislabelled as Latin-1; ID3v2.4 UTF-8; ID3v2.2; ID3v1-only; APEv2; Vorbis comments; MP4 atoms; ASF attributes.
9. Files with `iTunSMPB`, with a valid LAME tag, with a CRC-invalid LAME tag, and with no gapless tag at all.
10. Filenames containing emoji, RTL text, combining marks, a 255-byte name, and a >260-character Windows path.
11. Embedded artwork: valid JPEG, valid PNG, 8192×8192, a malformed JPEG, and a 20 MB image.
12. Custom `TXXX`/Vorbis/APEv2 tags; multi-valued artists and genres; every ReplayGain and R128 tag variant.

**Structural fixtures:**

13. Cue sheets: valid, missing `FILE`, wrong-case `FILE`, overlapping indexes, non-monotonic times, BOM-prefixed, CRLF and CR, non-UTF-8; plus embedded cue in FLAC and in Vorbis comments.
14. Playlists in every supported format, with absolute, relative, unresolvable, and mixed paths.
15. LRC: plain, standard timestamps, multi-timestamp lines, enhanced word-level, malformed timestamps, 1 MB file.
16. Corrupt audio: truncated mid-frame, bit-flipped payload, valid header with garbage body, zero-byte file, a `.mp3` that is actually FLAC.
17. Malicious skins per `REQ-SEC-006`.

**Scale fixtures:**

18. A synthetic tree of 10,000 files for scan integration tests, and a generator for 100,000 and 500,000 for benchmarks and soak.

### 29.5 Appendix E — ADR index

`REQ-GEN-081` `[v1.0]` These ADRs MUST exist, each stating context, decision, consequences, and rejected alternatives:

| ADR | Decision |
|---|---|
| `0001-project-license.md` | MPL-2.0 core, LGPL-only dependencies (§4.1) |
| `0002-audio-output.md` | `IAudioSink` with native backends; why not RtAudio (§6.4) |
| `0003-no-code-in-skins.md` | Declarative layout DSL instead of QML in packages (§11.1) |
| `0004-plugin-c-abi.md` | C ABI rather than C++ for the plugin SDK (§16) |
| `0005-qt-acquisition.md` | `aqtinstall` for Qt, vcpkg for the rest (§6.2) |
| `0006-ffmpeg-lgpl.md` | FFmpeg decode-only, LGPL configuration, CI assertion (§4.4) |
| `0007-shared-db-schema.md` | One schema for SQLite and Room (§6.8) |
| `0008-sync-wire-format.md` | JSON vs. CBOR for the sync protocol (§18.4) |
| `0009-visualizer-projectm.md` | projectM for MilkDrop preset compatibility (§6.7) |
| `0010-dsp-parity-audioprocessor.md` | Custom `AudioProcessor` rather than `android.media.audiofx` (§8.9.7) |

### 29.6 Appendix F — Summary of corrections to specification v1.x

Recorded so the reasoning is not lost, and so these mistakes are not reintroduced:

| # | v1.x claim | Problem | Resolution |
|---|---|---|---|
| 1 | Bit-perfect/exclusive output "via RtAudio's low-level device access" | RtAudio exposes no share-mode control and converts formats internally; bit-perfect is unreachable | `IAudioSink` with native WASAPI/ALSA backends (§6.4, §8.7) |
| 2 | "ALSA/PipeWire/JACK on Ubuntu" via RtAudio | **RtAudio has no PipeWire backend** | Native PipeWire backend `[v1.x]`; compat layers until then, disclosed in §29.2 |
| 3 | Skins with "optional custom QML layout regions" | Arbitrary code execution from downloaded packages | Two-tier model with a non-Turing-complete layout DSL (§11.1) |
| 4 | `theme-schema.json` as "single source of truth" | Never defined | Fully specified (§11.2), with conformance fixtures |
| 5 | Dependencies "pinned via vcpkg (Qt, FFmpeg, …)" | Building Qt in vcpkg is hours-long and brittle; Ubuntu 22.04 ships only Qt 6.2.4 | `aqtinstall` for Qt, vcpkg for the rest (§6.2) |
| 6 | `minSdk 26` "required for reliable MediaSession/Auto behavior" | Inaccurate — Media3 supports API 21 | Correct rationale stated (`REQ-GEN-004`); modern permissions added (`REQ-GEN-005`) |
| 7 | "10+ band graphic equalizer" | No frequencies, Q, gain range, or topology | Exact ISO frequencies, RBJ biquads, derived Q (§8.9.1) |
| 8 | "Duplicate detection by audio fingerprint" | No algorithm named | Chromaprint, offline, with a specified matching method (§9.9) |
| 9 | "DSD where the decode library permits" | Conflated decode with DoP passthrough | Split into `[v1.x]` decode and `[v2]` passthrough (`REQ-AUD-030`) |
| 10 | "contrast ratios enforced by the design tokens themselves" | No mechanism | Loader-side WCAG computation with reject/warn/auto-correct (`REQ-THM-041`) |
| 11 | Six Android Auto root categories | Auto shows at most four tabs | Four roots, others one level deeper (`REQ-AUT-004`) |
| 12 | "Zero telemetry by default" | Policy only | Dependency-denylist CI gate plus a zero-connection test (`REQ-SET-010`, `REQ-TST-023`) |
| 13 | `sync-protocol.md` referenced | Never specified | Full protocol, conflict rules, and threat model (§18) |
| 14 | No licensing analysis | Release blocker for an OSS player | §4 in full, with mechanical CI gates |
| 15 | No plugin/extension story | The reason Winamp outlived its owner | Plugin SDK with a stable C ABI (§16) |
| 16 | No non-goals | Unbounded scope | §2.4 |
| 17 | No acceptance criteria | Phases could not be judged complete | Exit gates per phase (§28) |

---

## Final instruction

Begin with **Phase 0**. Do not skip a phase, do not begin a phase before the previous phase's exit gates are green in CI on every platform, and do not weaken a requirement to make a gate pass — if a requirement is wrong, write an ADR and change the specification deliberately.

When you are uncertain, prefer: **correctness over speed, honesty over polish, and the user's control over their own data and their own audio over every other consideration.**


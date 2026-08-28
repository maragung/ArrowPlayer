# Platform Parity Matrix

`REQ-GEN-080`. Generated from `arrow-player.md` §29.2 and extended with an
**implementation status** column, because a parity matrix that describes intent
without saying what exists yet is a wish list.

AIMP publishes an equivalent Windows-versus-Linux document. Honest parity
disclosure prevents bug reports, and it is cheaper than the support load created
by a feature list that quietly means "on one platform".

**This file MUST be updated in the same commit as any feature that changes it.**

## Legend

| Symbol | Meaning |
|---|---|
| ✔ | Specified for 1.0.0 on this platform |
| ○ | Planned for a later tier — `v1.x` or `v2` as noted |
| — | Not offered on this platform, by design |
| n/a | Not applicable to this platform |

| Status | Meaning |
|---|---|
| **shipped** | Implemented, tested, and verified in CI |
| **partial** | Some layers exist; the feature does not work end to end |
| **planned** | Specified, nothing built |

## Current implementation reality

Read this before the matrix below, because it applies to almost every row:

- **No audio output exists yet.** No decoder adapter, no sink, no ring buffer, no
  real-time thread. The DSP kernels and the gapless metadata parsers are
  implemented and unit-tested; nothing plays.
- **No user interface exists yet.** No `main.cpp`, no window.
- **`android/` is a Phase 0 scaffold.** [ADR 0012](adr/0012-restore-android.md)
  restored the target [ADR 0011](adr/0011-desktop-first-sequencing.md) had
  deferred: one Gradle `:app` module, an About screen, a debug APK build. Every
  Android column below is still **planned**, because the scaffold implements
  none of the feature rows.
- **What "partial" rests on:** the 210-test suite passes from a clean build under
  Release, ASan+UBSan and TSan on a Linux development machine. Windows and
  `arm64` rows carry no local evidence at all — those are CI-only
  (`OQ-022`, `OQ-023` in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)).

## Matrix

| Feature | Windows | Ubuntu | Android | Status | Note |
|---|---|---|---|---|---|
| Gapless, sample-exact | ✔ | ✔ | ✔ | partial | Metadata extraction (Xing/LAME, `iTunSMPB`, `OpusHead`, Vorbis granule) implemented and tested; the scheduler that uses it does not exist |
| Crossfade | ✔ | ✔ | ✔ | planned | Android needs dual players — ExoPlayer has no native crossfade |
| Bit-perfect / exclusive | ✔ WASAPI excl. | ✔ ALSA `hw:` | — | planned | Android has no exclusive-mode API |
| Full DSP chain, identical coefficients | ✔ | ✔ | ✔ | partial | Desktop biquad kernels and EQ implemented and tested against published coefficients; `REQ-AUD-108` cross-platform verification impossible until Android exists |
| 18-band EQ | ✔ | ✔ | ✔ | partial | Desktop implemented; custom `AudioProcessor` on Android, not `audiofx` ([ADR 0010](adr/0010-dsp-parity-audioprocessor.md)) |
| ASIO | ○ v1.x, opt-in build | n/a | n/a | planned | Steinberg SDK user-supplied (§4.6); never vendored |
| JACK | n/a | ○ v1.x | n/a | planned | |
| Native PipeWire | n/a | ○ v1.x | n/a | planned | Until then via ALSA/Pulse compat — **not** via RtAudio, which has no PipeWire backend at all (§29.6 item 2) |
| OS media controls | ✔ SMTC | ✔ MPRIS2 | ✔ MediaSession | planned | |
| Global hotkeys | ✔ `RegisterHotKey` | ✔ X11; **portal-dependent on Wayland** | n/a | planned | `REQ-KEY-013` — Wayland has no grab protocol; MPRIS carries media keys instead |
| System tray | ✔ | ✔ where the DE has one | n/a | planned | GNOME has no tray by default (`REQ-OSI-028`); absence must degrade gracefully (`REQ-GEN-003`) |
| Recursive filesystem watching | ✔ `ReadDirectoryChangesW` | ✔ `inotify` | **Periodic scan only** | planned | Android has no recursive watch API |
| Skin Tier 1 (Theme) | ✔ | ✔ | ✔ | partial | Schema, tokens and 122 validation fixtures exist and are verified; no loader, no renderer |
| Skin Tier 2 (layout DSL) | ✔ v1.0 | ✔ v1.0 | ○ v1.x | partial | Schema and fixtures exist; no interpreter |
| projectM / MilkDrop presets | ✔ | ✔ | ✔ | planned | [ADR 0009](adr/0009-visualizer-projectm.md) |
| Plugin SDK | ○ v1.x | ○ v1.x | — | planned | Native plugins are desktop-only by design; ABI decided in [ADR 0004](adr/0004-plugin-c-abi.md) |
| Windowshade / detachable panels | ○ v1.x | ○ v1.x | n/a | planned | |
| Android Auto | n/a | n/a | ✔ | planned | §15 |
| Tag writing | ✔ | ✔ | ✔ SAF-permitted locations only | planned | |
| Portable mode | ✔ | ✔ | n/a | planned | §19.3 |
| Audio converter | ○ v1.x | ○ v1.x | ○ v1.x (+ audio cutter) | planned | §9.10 |
| Sync | ○ v1.x | ○ v1.x | ○ v1.x | partial | Protocol fully specified in [`shared-spec/sync-protocol.md`](../shared-spec/sync-protocol.md) with threat model; no implementation |
| `arm64` release artifact | ✔ | ✔ | ✔ | planned | `arm64` desktop is built but **not CI-tested** in 1.0.0 (`REQ-GEN-001`) |

## Deviations that are permanent, not temporary

These are not gaps waiting to be closed. They are platform facts, and a future
release will not change them:

| Deviation | Platform | Why it cannot be fixed |
|---|---|---|
| No bit-perfect / exclusive output | Android | The platform exposes no exclusive-mode API. AAudio's low-latency path is not the same thing: the mixer is still in the path. |
| No recursive filesystem watching | Android | No recursive watch API exists. `FileObserver` watches one directory; watching a library tree would mean thousands of watches and is not what the API is for. Periodic scan is the honest alternative, not a workaround. |
| No global hotkeys | Android | No system-wide key grab for an app. Media keys arrive through `MediaSession`, which is a different and narrower thing. |
| No system tray | Android | No such surface. The notification and `MediaSession` are the equivalent. |
| Global hotkeys are portal-dependent | Ubuntu / Wayland | Wayland deliberately has no key-grab protocol. Where the desktop portal implements the global-shortcuts interface, hotkeys work; where it does not, they do not, and the app must say so rather than fail silently (`REQ-KEY-013`). |
| System tray depends on the desktop environment | Ubuntu | GNOME ships no tray. The app must run correctly without one (`REQ-OSI-028`, `REQ-GEN-003`). |
| No native plugins | Android | Deliberate. The plugin model is native in-process code with explicit consent (§16.5), which does not fit Android's app model or its security expectations. |
| No portable mode | Android | The platform has no equivalent of a self-contained directory the user controls. |
| `arm64` desktop not CI-tested | Windows, Ubuntu | `REQ-GEN-001` permits cross-compilation without CI testing for 1.0.0. Artifacts are produced and signed; they are not exercised on `arm64` hardware in CI. Stated because "we ship it" and "we test it" are different claims. `OQ-022`. |

## How this document stays true

- `REQ-GEN-080` makes updating it part of any feature commit that changes a row.
- The **Status** column is the part most likely to rot. It is checked at release
  time against the release checklist (`REQ-BLD-037`), which is the only moment
  where every claim in the repository gets re-read at once.
- Anything in this matrix marked ✔ for 1.0.0 that is still **planned** when 1.0.0
  is tagged is a release blocker by `REQ-GEN-080` plus §0.3 — a `[v1.0]`
  requirement cannot be unmet at the 1.0.0 tag.

# Architecture — Eclipse Player

The document `eclipse-player.md` §27 requires: *five layers, threads, data flows,
`REQ-GEN-040` rationale*. This is that document. It describes the architecture as
the specification defines it **and** as the repository currently enforces it, and
it is explicit about the difference wherever one exists.

Scope: the desktop implementation. The Android half of the same five layers is
deferred — see [ADR 0011](adr/0011-desktop-first-sequencing.md).

- [Why Qt, FFmpeg, and our own sink layer](#why-qt-ffmpeg-and-our-own-sink-layer)
- [The five layers](#the-five-layers)
- [Dependency rules, and how they are enforced](#dependency-rules-and-how-they-are-enforced)
- [Threading model](#threading-model)
- [Data flow — playback](#data-flow--playback)
- [Data flow — library scan](#data-flow--library-scan)
- [Module boundary contract](#module-boundary-contract)
- [What exists today](#what-exists-today)

## Why Qt, FFmpeg, and our own sink layer

`REQ-GEN-040` requires this document to state the following, and it is stated here
first because every other decision in the tree follows from it:

> Qt + FFmpeg + our own native sink layer is the combination that gives full
> control of the audio buffer for gapless and DSP work, first-class native OS
> integration (SMTC, MPRIS2, tray, hotkeys), and a QML-based skin engine with
> genuine per-skin layout control rather than palette swapping. The sink layer is
> ours rather than a third party's specifically because bit-perfect output and
> sample-exact gapless require share-mode, device-period, and clock control that
> general-purpose wrappers do not expose.

In longer form, for a contributor meeting the tree for the first time.

**Qt 6.8 LTS, in two halves.** Qt Widgets draws the window chrome, the menus, and
the preference dialogs; Qt Quick/QML draws every surface a skin can restyle — Now
Playing, the library grid, the mini-player, the visualizer host. The split is not
indecision. Chrome should feel native and is never skinned, whereas skinned
surfaces need a GPU scene graph and property bindings to do layout that a theme
author actually controls. Qt is picked because it is the only mature natively
compiled cross-platform C++ UI framework that also brings first-class Windows
*and* Linux packaging, an accessibility bridge to both UI Automation and AT-SPI2,
and a translation pipeline. `qtmultimedia` is deliberately **not** used: it offers
no exclusive mode and no latency guarantee, and we own the audio path. See
§6.1–6.2 of the specification and [ADR 0005](adr/0005-qt-acquisition.md).

**FFmpeg 7.1, decode only, LGPL configuration.** One dependency covers
practically every format a user will own, and — built the way §4.4 prescribes,
with the network, filter, muxer, and encoder subsystems disabled — it is legally
clean for a project that ships under MPL-2.0 with LGPL-only dependencies. It sits
behind our own `IDecoder` port, which is what makes the fallback plan (per-format
libraries: libFLAC, libmpg123, libopus, …) a local change rather than a rewrite.
See [ADR 0006](adr/0006-ffmpeg-lgpl.md).

### Why the sink layer is ours

This is the part most likely to look like reinvention, so the reasoning is worth
following precisely. Playing audio through a general-purpose wrapper is easy. The
two guarantees this product is *for* — bit-perfect output and sample-exact
gapless — are not reachable through one, because they need three things wrappers
hide:

1. **Share mode.** Bit-perfect output means the operating system's mixer must be
   out of the path: `AUDCLNT_SHAREMODE_EXCLUSIVE` on WASAPI, a direct `hw:`
   device on ALSA. A wrapper that only opens shared streams cannot express this.
2. **Device period.** Gapless splicing and the DSP chain both need to know, and
   to choose, how many frames the device asks for per callback. Latency budgets
   in §20 are stated in device periods, not in wrapper abstractions.
3. **Clock control.** Sample-exact concatenation across a track boundary is
   arithmetic on the device's own sample clock. A layer that resamples,
   de-interleaves, or channel-compensates internally — silently, as a
   convenience — destroys the very thing being guaranteed.

The specification records this as a correction of an earlier revision that had
specified RtAudio and claimed exclusive-mode output through it. Verified against
RtAudio's own documentation, that claim was false in four separate ways: there is
no PipeWire backend; the public API exposes no share-mode selection, so WASAPI
exclusive mode is unreachable; internal format conversion, channel compensation
and de-interleaving are documented behaviour, which is incompatible with
bit-perfect by construction; and its ASIO support needs Steinberg SDK sources
this project cannot redistribute. So `IAudioSink` (§8.7) is our port, with native
backends per platform — WASAPI, ALSA, PulseAudio, later PipeWire and JACK. See
[ADR 0002](adr/0002-audio-output.md).

Owning the port has a second payoff: because every backend is behind one
interface, a file-backed sink is just another implementation, and the audio
engine becomes testable without a sound card at all. §8.11's test 5 depends on
exactly that.

## The five layers

Both platforms implement the same five layers. The names differ per language; the
responsibilities do not.

```text
┌──────────────────────────────────────────────────────────────────────┐
│ 5  PRESENTATION    QML surfaces + Widgets shell  |  Compose UI       │
│                    Skin-driven. Zero business logic. Zero file I/O.  │
├──────────────────────────────────────────────────────────────────────┤
│ 4  APPLICATION     Use cases, view-models, command dispatch,         │
│                    playback session, queue orchestration             │
├──────────────────────────────────────────────────────────────────────┤
│ 3  DOMAIN          Pure entities + rules: Track, Album, Playlist,    │
│                    SmartRule, EFS AST, ThemeTokens, DspGraphSpec.    │
│                    No framework imports. Fully unit-testable.        │
├──────────────────────────────────────────────────────────────────────┤
│ 2  PORTS           Interfaces only: IDecoder, IAudioSink, ITagReader,│
│                    ITagWriter, ILibraryIndex, IHttpClient, IClock,   │
│                    IFileSystem, IMediaSession, IPluginHost           │
├──────────────────────────────────────────────────────────────────────┤
│ 1  ADAPTERS        FFmpeg, WASAPI/ALSA/AAudio, TagLib, SQLite/Room,  │
│                    Qt, Media3, SMTC/MPRIS, projectM, Chromaprint     │
└──────────────────────────────────────────────────────────────────────┘
```

Layers 2 and 1 sit *below* the domain in the numbering because dependency
direction, not call direction, is what the numbers order. The domain defines what
a decoder must be able to do; the FFmpeg adapter obeys. Calls at run time go
downward and outward; compile-time dependencies only ever point down.

How the layers map onto the desktop tree:

| Layer | Lives in | CMake target | Status |
|---|---|---|---|
| 5 Presentation | `desktop/ui/` | `eclipse-ui` (Qt) | not built yet — Stage 3 |
| 4 Application | `desktop/src/app/` | `eclipse-app` | not built yet — Stage 3 |
| 3 Domain | `desktop/src/core/`, `src/audio/dsp/`, `src/audio/analysis/`, `src/audio/graph/`, `src/theme/` | `eclipse-domain` | partly built |
| 2 Ports | headers alongside their domain area (`audio/decode/decoder.hpp`, `audio/sink/sink.hpp`, …) | header-only, part of `eclipse-domain` | not written yet |
| 1 Adapters | `desktop/src/audio/sink/`, `src/audio/decode/ffmpeg_decoder.cpp`, `src/library/`, `src/platform/` | `eclipse-adapters` | interface target only |

Ports are headers, not a directory. Putting `IDecoder` in `audio/decode/` next to
the pure parsers it serves keeps the port with its subject; what makes it a
layer-2 artefact is that it names no third party, and the layer gate enforces
that (`audio/decode/` is a domain directory, with the FFmpeg adapter explicitly
excluded from it).

## Dependency rules, and how they are enforced

`REQ-GEN-050` states five rules and requires that they hold **by CI check, not by
reviewer discipline**. `REQ-GEN-051` names the mechanism: a CMake-time
include-path partition plus a `tools/` script that greps for forbidden includes
per directory, wired into `desktop-ci.yml`.

The script is [`tools/check-layers.py`](../tools/check-layers.py). What each rule
costs to break, today:

| Rule | Enforcement | State |
|---|---|---|
| 1 · Layer *N* may depend only on layers *< N* | `check_layer_order()` maps directories to layers and refuses an upward include; layer 1 is reachable only from the composition root | enforced — [OQ-031](OPEN-QUESTIONS.md) closed, [OQ-055](OPEN-QUESTIONS.md) records the numbering inversion it found |
| 2 · Domain imports no Qt/FFmpeg/SQLite/adapter | `check_domain_purity()` + the CMake include-path partition | enforced |
| 3 · Adapters reachable only through their port | `check_adapter_confinement()` | enforced |
| 4 · `android/**` ⇄ `desktop/**` never cross; `shared-spec/**` holds no code | `check_android_isolation()`, `check_shared_spec_has_no_code()` | enforced |
| 5 · Android feature modules depend only on `core-*` | Gradle assertion test | not applicable — no `android/` (ADR 0011) |

### Rule 1 — direction

`check_layer_order()` holds one table, `LAYER_PREFIXES`, mapping directory
prefixes under `desktop/` to layers: `ui` → 5, `src/app` → 4, `src/ports` → 2,
`src/audio/sink`, `src/library` and `src/platform` → 1, and **everything else
under `src/` → 3**. Longest prefix wins. The default is the useful part: pure
code needs no entry, an adapter directory does, and an unmapped adapter fails the
moment it includes its port rather than being silently exempted.

The same table classifies both sides of an include, so the file's layer and the
included header's layer are read off one source of truth. `<eclipse/ui/…>` is
layer 5 by its include prefix; standard and third-party headers are not
classified at all, because rules 2 and 3 own those.

Two facts about the port/adapter boundary are stated rather than computed, because
§7.1's numbering puts adapters *below* ports and so inverts rule 1's arithmetic
there ([OQ-055](OPEN-QUESTIONS.md)): layer 1 **may** include layer 2, and nothing
but the composition root may include a layer-1 header.

`desktop/src/main.cpp` is exempt by name. It constructs layer 4 and hands control
to layer 5, which is what a composition root is for; the exemption is a named
constant so it reads as a decision rather than as an oversight.

### Rule 2 — domain purity

The domain directories are listed explicitly in the script rather than globbed, so
that adding a file to the domain library is a deliberate act visible in review:
`src/core`, `src/audio/dsp`, `src/audio/analysis`, `src/audio/graph`,
`src/audio/decode`, `src/theme`. One file inside them is excluded, because it is
compiled into `eclipse-adapters` instead:
`src/audio/decode/ffmpeg_decoder.cpp`.

Inside those directories, an `#include` matching any of these is a build failure:
Qt (`Q[A-Z]…`, `qt/`), FFmpeg (`libavcodec/`, `libavformat/`, `libavutil/`,
`libswresample/`, `libswscale/`), SQLite, TagLib, ALSA, PulseAudio, PipeWire,
JACK, WASAPI/COM, libsamplerate, SoundTouch, Chromaprint, projectM, and POSIX or
Linux platform headers (`sys/`, `linux/`, `unistd.h`, `dlfcn.h`).

The platform headers are on that list for the same reason as the libraries. A
domain module that reaches for `unistd.h` has acquired an operating system, which
is exactly the dependency the layer exists to refuse.

The CMake half is `eclipse-domain`'s include path: `include`, `src`, and the
generated-header directory — no third-party include directories are attached to
it at all, so a forbidden header is usually a compile error before the gate ever
runs. The one library `eclipse-domain` links is `Threads::Threads`, which is how
CMake spells the standard library's threading support (`-pthread`); it is not a
third party, and `std::atomic`/`std::thread` are standard-library facilities the
rule permits.

### Rule 3 — adapter confinement

Each adapter header is legal in exactly one directory:

| Header pattern | Only under |
|---|---|
| WASAPI (`audioclient.h`, `mmdeviceapi.h`, `avrt.h`), ALSA, PulseAudio, PipeWire, JACK | `desktop/src/audio/sink/` |
| FFmpeg (`libavcodec/`, `libavformat/`, `libavutil/`, `libswresample/`) | `desktop/src/audio/decode/` |
| TagLib, SQLite | `desktop/src/library/` |

This is the rule that keeps a port a port. The moment a view-model includes
`sqlite3.h` "just for this one query", layer 2 has stopped being a boundary and
the SQLite dependency has become unremovable.

### Two more gates on the same path

`tools/check-layers.py` is one of three architecture gates that run in
`desktop-ci.yml`, and the other two guard rules that live in this architecture
even though §7 does not phrase them as layering:

- [`tools/check-rt-safety.py`](../tools/check-rt-safety.py) — every function
  reachable from the audio callback carries a `/// RT-SAFE:` doc-comment
  (`REQ-AUD-017`), and a function claiming it must not contain an obviously
  forbidden construct: `new`/`delete`, C heap calls, `std::mutex` and friends,
  `throw`, console or file I/O, logging, container growth, `std::string`
  construction, `std::to_string`, `std::regex`, `std::shared_ptr`, `std::sort`,
  or sleep. It is a lint, not a proof — TSan and the allocation-hook test cover
  what static inspection cannot — but it makes the annotation load-bearing rather
  than decorative. A false RT-SAFE claim is worse than none, because the next
  maintainer will trust it.
- [`tools/check-sql-safety.py`](../tools/check-sql-safety.py) — no SQL is built by
  concatenation or formatting anywhere, including the smart-playlist compiler
  (`REQ-SEC-009`). That compiler matters more than it looks: a smart-playlist rule
  can arrive inside an imported settings bundle, so rule text is untrusted input.

## Threading model

§8.2 has the detail. The process-wide inventory:

| Thread | Owner | Priority | May block? | May allocate? |
|---|---|---|---|---|
| UI / main | Qt / Android main looper | normal | no (never >16 ms) | yes |
| **Audio RT callback** | the active `IAudioSink` | time-critical / RT | **NO** | **NO** |
| Decoder (×2: current + prefetch) | audio engine | above normal | yes | yes, off the hot path |
| Analysis worker pool | library / analysis | below normal | yes | yes |
| Scanner pool (N = min(4, cores)) | library | low / background | yes | yes |
| Network | `net/` | normal | yes | yes |
| DB writer (serialised) | library index | normal | yes | yes |

`REQ-GEN-052`: there is **exactly one** writer thread for the library database,
and every write is funnelled through it as a serialised command. This is not a
performance tactic. It eliminates SQLite write contention as a *class* of bug —
`SQLITE_BUSY` handling, retry loops, and interleaved-transaction corner cases stop
being possible rather than being handled.

Today none of these threads exist. Every module in the tree is synchronous and
pure, and the two-decoder prefetch, the scanner pool, and the writer thread arrive
with the modules that need them (Stage 4 and Phase 2). The table is the target the
RT-safety annotations are already written against, not a description of running
code — see [What exists today](#what-exists-today).

## Data flow — playback

```text
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

`REQ-GEN-053`: **every** arrow crossing into or out of the RT callback must be a
lock-free, wait-free, pre-allocated channel. No mutex, no allocation, no logging
call, and no syscall may appear on that path. Violations are correctness bugs, not
performance nits.

There are four such crossings, and each is one-directional by design:

| Crossing | Direction | Carries |
|---|---|---|
| `DecodeBuffer` | decoder → RT | audio frames (SPSC ring, allocated once at open) |
| spectrum tap | RT → visualizer | a fixed-size snapshot the visualizer may drop |
| boundary events | RT → L4 | "track *n* ended at sample *s*" |
| underrun / device-loss | RT → L4 | recovery triggers |

"Wait-free" is stricter than "lock-free" and is the level required here: the
callback must complete in a bounded number of steps regardless of what any other
thread is doing. That is why the reverse direction of each channel does not exist.
A request-response across the callback boundary would need the callback to wait
for an answer, and there is no deadline under which that is safe.

The consequence worth internalising: anything the callback needs must already be
there. Buffers are allocated at stream open, DSP coefficients are computed on the
UI thread and published atomically, format changes tear down and rebuild the
stream rather than reconfiguring it in place. [`docs/AUDIO-ENGINE.md`](AUDIO-ENGINE.md)
carries the signal chain and the per-format gapless arithmetic.

## Data flow — library scan

```text
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

Three details in that diagram are load-bearing:

- **`UNCHANGED` skips the tag read entirely.** A rescan of an unchanged library
  must cost a `stat()` per file and nothing more. Reading tags to discover that
  nothing changed is the difference between a rescan that takes seconds and one
  that takes minutes.
- **Transactions batch at 500 rows**, and all of them cross the single writer
  thread of `REQ-GEN-052`. Progress events are throttled to 10 Hz because a
  progress bar updated per file is a UI thread spending its budget on repaints.
- **`GONE` marks missing, never deletes.** An unmounted network share is
  indistinguishable from a deleted directory at scan time, and play counts,
  ratings, and playlist membership must survive the difference.

## Module boundary contract

`REQ-GEN-054`: every module exposes **exactly one** public header that defines its
surface; everything else is internal. [`docs/API.md`](API.md) documents every
public surface, with the thread- and RT-safety of each function.

The convention in the tree is that the public header is the one named after its
directory or its subject — `core/error.hpp`, `core/text.hpp`, `core/json/json.hpp`,
`audio/dsp/equalizer.hpp` — and that a header includes only what its own
declarations need. `equalizer.hpp` includes `biquad.hpp` because an `Equalizer`
*contains* biquads; nothing outside `audio/dsp/` needs to know that.

`REQ-GEN-054` also requires a CI check that every public symbol carries a
doc-comment. That check does not exist yet, and its absence is recorded as
[OQ-032](OPEN-QUESTIONS.md) rather than left for a reader to notice that this
paragraph describes something nothing verifies.

## What exists today

Six modules, all layer 3, all pure C++20 with no dependency beyond the standard
library:

| Module | Public header | Covers | Tests |
|---|---|---|---|
| Error model | `core/error.hpp` | `Result<T>`, `ErrorCode`, no-exceptions discipline | `test_error.cpp` |
| Text | `core/text.hpp` | UTF-8 validation, normalisation, case folding | `test_text.cpp` |
| JSON | `core/json/json.hpp` | hardened parser: bounded depth, size, member counts; duplicate keys reported | `test_json.cpp` |
| Biquad | `audio/dsp/biquad.hpp` | RBJ cookbook coefficients, double-precision state | `test_equalizer.cpp` |
| Equalizer | `audio/dsp/equalizer.hpp` | 10- and 18-band graphic EQ | `test_equalizer.cpp` |
| Gapless metadata | `audio/decode/gapless_info.hpp` | Xing/LAME, iTunSMPB, OpusHead, Vorbis granule | `test_gapless.cpp` |

Everything above layer 3 — the application objects, the Qt shell, the ports, and
every adapter — is scaffolding that has not been written yet. The layer gates pass
because the rules hold, not because there is nothing to check: the domain library
is real, it compiles, and it is where every rule in this document has teeth first.

For the sequencing, see [ADR 0011](adr/0011-desktop-first-sequencing.md) and
[`docs/ROADMAP.md`](ROADMAP.md). For what is proven where, and by what,
[`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) §5 is the honest register.

# Testing

§27 requires this document to cover *how to run each suite, the reference
hardware, the golden corpus, the Android Auto manual checklist, the manual
accessibility checklist* (`REQ-GEN-075`).

One clause of that row does not apply to this build. **The Android Auto manual
checklist is out of scope**, because the project is desktop-only for now — the
scope decision and its consequences are recorded in
[ADR 0011](adr/0011-desktop-first-sequencing.md). It is named and explained
below rather than dropped, because silently omitting it would be the exact
substitution ADR 0011 exists to refuse. The manual **accessibility** checklist
is *not* Android-specific and is fully in scope; it is here in full.

Everything stated as a number or a "passes" in this document comes from a
command run on the development machine and pasted below, not from recall. Where
a suite the specification mandates does not exist yet, or cannot run here, the
sentence describing it says so in place. The honest register of what is proven
where is [`OPEN-QUESTIONS.md` §5](OPEN-QUESTIONS.md#5--verification-status--what-is-proven-where);
this document is consistent with it and links to it rather than restating it.

- [How to run every suite](#how-to-run-every-suite)
- [The reference hardware](#the-reference-hardware)
- [The golden corpus](#the-golden-corpus)
- [Sanitizers and fuzzing](#sanitizers-and-fuzzing)
- [The fifteen audio verifications](#the-fifteen-audio-verifications)
- [Android Auto — out of scope](#android-auto--out-of-scope)
- [The manual accessibility checklist](#the-manual-accessibility-checklist)
- [Continuous integration](#continuous-integration)
- [What is not tested here](#what-is-not-tested-here)

## How to run every suite

The suite inventory today is small and honest: it is the domain layer (layer 3),
under GoogleTest, and the gate scripts. Everything above layer 3 — the adapters,
the RT engine, the Qt UI — is not written yet ([ADR 0011](adr/0011-desktop-first-sequencing.md)),
so the integration, UI, fuzz, soak, and chaos suites the specification mandates
have nothing to exercise and are not built. What runs, runs completely.

| CTest binary | Source | Cases | Domain area |
|---|---|---|---|
| `test_core` | `unit/test_text.cpp`, `unit/test_error.cpp`, `unit/test_json.cpp` | 79 | UTF-8/sort-key/path text, `Result<T>` error model, hardened JSON parser |
| `test_dsp` | `unit/test_equalizer.cpp` | 56 | biquad coefficients (26) + graphic/parametric EQ (30) |
| `test_gapless` | `unit/test_gapless.cpp` | 51 | MP3 Xing/LAME, `iTunSMPB`, `OpusHead`, granule, native trim (48 + 3 randomised) |
| **Total** | | **186** | |

The counts are `--gtest_list_tests` output, and their sum is the CTest total.

### The gate scripts

Four Python scripts enforce rules that reviewer discipline does not reliably
catch. They need no compiler and run in about a second. From the repository
root:

```bash
python3 tools/check-layers.py        # layer dependency direction (REQ-GEN-051)
python3 tools/check-sql-safety.py    # no string-built SQL        (REQ-SEC-009)
python3 tools/check-rt-safety.py     # /// RT-SAFE: claims hold   (REQ-AUD-017)
python3 tools/check-doc-links.py     # §27 docs exist, links resolve (REQ-GEN-075)
```

Real output on the current tree:

```text
  domain layer purity      ok
  adapter confinement      ok
  shared-spec has no code  ok
  platform isolation       ok
layer rules: all checks passed

sql safety: 17 file(s) scanned, no interpolated SQL found

rt safety: 4 file(s), 7 RT-SAFE annotation(s), no violations
```

What each gate does **not** catch is as important as what it does:

- `check-layers.py` verifies rule 2 (domain purity) and rule 3 (adapter
  confinement) by grepping includes against an explicit directory list; the
  general downward-only include check of `REQ-GEN-050(1)` is not written
  ([OQ-031](OPEN-QUESTIONS.md)). It reads text, not the compiled graph, so a
  forbidden dependency introduced by a macro or a transitive header it does not
  model would pass it — the CMake include-path partition is the backstop, not
  this script.
- `check-rt-safety.py` is a **lint, not a proof**. It fails a function that
  *claims* `/// RT-SAFE:` while containing `new`, a lock, `throw`, container
  growth, `std::to_string`, `std::shared_ptr`, or a log call. It cannot prove a
  function is RT-safe, only catch the obvious lies; TSan and the allocation-hook
  test (both unwritten — no callback exists yet) are what cover the rest. It
  reports 7 annotations because the DSP maths carries them ahead of the RT graph
  that will call it.
- `check-sql-safety.py` greps for string concatenation adjacent to SQL keywords.
  There is no SQL in the tree yet, so "17 file(s) scanned, no interpolated SQL
  found" means the gate is armed and idle, not that a query was proven safe.
- `check-doc-links.py` checks that every §27 document exists and that internal
  links and `#fragment` anchors resolve (GitHub's slug algorithm). It does
  **not** fetch external `http(s)` URLs — a gate must pass offline. It is
  currently red for a real reason; see [Continuous integration](#continuous-integration).

### Configure, build, and the 186 unit tests

Every external library is optional at configure time, so the tree configures and
the domain tests build with nothing but a C++20 compiler — see
[`BUILDING.md`](BUILDING.md#dependency-model). From `desktop/`:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

The configure summary names what was found; on this machine Qt and libsamplerate
are absent (see [What is not tested here](#what-is-not-tested-here)):

```text
-- ======== Eclipse Player 0.1.0 (d03fc8cd5e84) ========
--   build type    : Release
--   compiler      : GNU 14.2.0
--   deps from     : system / pkg-config
--   --- adapters (dependency-gated) ---
--   SQLite3       : ON
--   FFmpeg        : ON
--   TagLib        : ON
--   ALSA          : ON
--   libsamplerate : OFF
--   Qt 6          : OFF
```

`ctest --preset linux-release`:

```text
100% tests passed, 0 tests failed out of 186

Label Time Summary:
unit    =   0.60 sec*proc (186 tests)

Total Test time (real) =   0.69 sec
```

That is the "186 tests, all passing" figure that `README.md` and
[`AUDIO-ENGINE.md`](AUDIO-ENGINE.md#what-exists-today) quote: a local run,
reproduced here, not a number asserted by CI alone.

### The sanitizer presets

The same 186 tests run instrumented. Both presets use GCC's sanitizers, so no
Clang is needed to run them (it is needed for the fuzzers — see below). From
`desktop/`:

```bash
cmake --preset linux-asan && cmake --build --preset linux-asan && ctest --preset linux-asan
cmake --preset linux-tsan && cmake --build --preset linux-tsan && ctest --preset linux-tsan
```

Both are green here:

```text
linux-asan (ASan + UBSan):    100% tests passed, 0 tests failed out of 186
linux-tsan (ThreadSanitizer): 100% tests passed, 0 tests failed out of 186
```

TSan passing is worth reading precisely. `linux-tsan` exists for the RT-safety
soak of `REQ-AUD-018`, and there is no audio callback yet for it to drive; a
green run today means the domain code is clean under the tool, not that the
real-time path has been exercised. The register says the same, and this document
does not upgrade it.

### The shared-spec validation

`shared-spec/**` carries the cross-platform contract (schemas, conformance
fixtures, grammars, tokens). The offline validator uses the standard library
only:

```bash
python3 tools/validate-shared-spec.py
```

```text
shared-spec validation
  · 5 schemas, 102 JSON documents parsed
  · keyword allowlist: 44 keywords implemented by jsonschema_mini
  ✓ all checks passed
```

A deeper draft-2020-12 pass runs in `spec-ci.yml` and also runs here, because
the distribution ships `jsonschema` as a system package:

```bash
python3 .github/scripts/spec_full_validate.py --check-schemas --check-fixtures
python3 .github/scripts/compare_verdicts.py --self-test
```

```text
  91 fixture(s) validated against their schema with jsonschema  ·  ok
  compare_verdicts: 10 scenarios, all as expected.
```

The two conformance halves of `REQ-TST-021` (desktop and Android producing
*identical* verdicts) cannot both be exercised: there is one engine, not two.
`compare_verdicts.py` self-tests its own logic, but the comparison it exists to
make waits on the second platform ([OQ-018](OPEN-QUESTIONS.md)).

### Presets that cannot run here

`windows-debug`, `windows-release` and `windows-arm64` exist in
`CMakePresets.json` and are exercised only by `desktop-ci.yml`: the development
machine is Linux, so WASAPI, the installer, and everything Windows are CI-only
([OQ-023](OPEN-QUESTIONS.md)). `arm64` has no runner at all
([OQ-022](OPEN-QUESTIONS.md)). The Qt UI (`ECLIPSE_BUILD_UI`) never configures
because Qt is not installed ([OQ-017](OPEN-QUESTIONS.md)), so there is no QTest,
QML, or screenshot suite to run locally.

## The reference hardware

`REQ-NFR-001` opens by requiring these baselines to live here:

> All budgets are stated against these baselines, which MUST be recorded in
> `docs/TESTING.md`:

| Class | Specification |
|---|---|
| **Desktop reference** | 4-core / 8-thread x86-64 @ 2.5 GHz, 8 GB RAM, SATA SSD, integrated GPU, 1920×1080 |
| **Desktop floor** | 2-core x86-64 @ 1.6 GHz, 4 GB RAM, HDD — must remain *usable*, budgets ×2 permitted |
| **Android reference** | Snapdragon 7-series equivalent, 6 GB RAM, Android 13 |
| **Android floor** | Snapdragon 4-series equivalent, 3 GB RAM, Android 8 — budgets ×2 permitted |
| **Reference library** | 100,000 tracks / 8,000 albums / 12,000 artists, mixed formats, on local SSD |

The table is here because a performance claim without the machine it was measured
on is not a claim. Every budget in §20 is quoted against these rows — desktop
cold start ≤ 1200 ms (`REQ-NFR-002`), scan throughput ≥ 500 tracks/sec
(`REQ-NFR-004`), RT callback worst case ≤ 50 % of the period with the full chain
at 192 kHz (`REQ-NFR-006`, `REQ-AUD-021`), idle RSS ≤ 220 MB (`REQ-NFR-007`) —
and a number like "≤ 50 % of the period" is meaningless until the period and the
CPU are pinned. The **Desktop reference** row is the machine those figures assume;
the **floor** row is where the product must stay *usable* with the budgets
doubled.

None of these budgets is measured yet. The benchmark harness (`§20`, wired into
CI per `REQ-BLD-021`) needs the RT engine, the scanner, and the Qt frontend, none
of which exist. The Android rows are recorded verbatim from the specification for
completeness; they belong to a platform this build does not include
([ADR 0011](adr/0011-desktop-first-sequencing.md)). Until the harness exists, the
figures in §20 are targets the code is written against, not results.

## The golden corpus

`REQ-TST-027` and `REQ-TST-030` (§29.4) define the corpus and forbid committing
audio:

> `desktop/tests/data/` MUST hold a manifest, and `tools/corpus-fetch` MUST
> generate or fetch the corpus (never committing large binaries) […] Every file
> MUST be either generated by the tool or licensed for redistribution — no
> copyrighted music in the repository, ever.

The corpus is how the integration tests get their inputs without shipping a
music library. §29.4 lists what it must contain: generated signals per format at
44.1/48/96/192 kHz — a 1 kHz full-scale sine, a 20 Hz–20 kHz multitone sweep,
white/pink noise, digital silence, a full-scale square wave, an impulse, and a
known 60 s signal **split into two files by each encoder** for the gapless test;
metadata fixtures (every ID3/APEv2/Vorbis/MP4 tag variant, `iTunSMPB`, valid and
CRC-invalid LAME tags, emoji and RTL and 255-byte filenames, malicious artwork);
structural fixtures (cue sheets, playlists, LRC, deliberately corrupt audio); the
malicious-skin corpus of `REQ-SEC-006`; and synthetic trees of 10k/100k/500k
files for scan and soak.

**None of it exists yet.** `tools/corpus-fetch` is not written, and
`desktop/tests/data/` is an empty directory with no manifest:

```text
$ ls -A desktop/tests/data/         # empty — no manifest, no fixtures
$ ls -d tools/corpus-fetch
ls: cannot access 'tools/corpus-fetch': No such file or directory
```

Consequently the gapless suite (`test_gapless`, 51 cases) tests the metadata
parsers against **in-code synthetic byte buffers** — hand-built Xing/LAME frames,
`iTunSMPB` strings, `OpusHead` packets — not against real encoded files. That
proves the arithmetic of `REQ-AUD-036`–`REQ-AUD-045`; it does not prove
end-to-end sample-exactness, which is §8.11 test 3 and needs both the corpus and
the decoder adapter. When the corpus is built, the MP3-LAME half needs an
encoder, which is why the development FFmpeg in [`BUILDING.md`](BUILDING.md) is
configured `--enable-libmp3lame` for corpus generation only
([OQ-024](OPEN-QUESTIONS.md)).

**What a golden-file failure means.** A golden test compares this run's output
against a committed reference file. When it goes red, exactly one of two things
happened: the code regressed, or the reference is legitimately stale because the
correct output changed. Telling them apart is the entire discipline. Regenerating
the reference to make the test green — without first proving the new output is
the *correct* output by an independent route — does not fix the test. It deletes
it, and leaves a green check that asserts only that the code agrees with itself.
A stale golden is updated *after* the new output is verified, never *to* make the
suite pass.

## Sanitizers and fuzzing

### Which sanitizer covers what

| Preset | Sanitizers | Requirement | Covers |
|---|---|---|---|
| `linux-asan` | AddressSanitizer + UndefinedBehaviorSanitizer | `REQ-NFR-008`, §23.5 | memory errors and UB across the unit and (eventually) integration suites; `-fno-sanitize-recover=all` makes any finding fatal |
| `linux-tsan` | ThreadSanitizer | `REQ-AUD-018`, `REQ-TST-015` | data races on the RT path, concurrent library writes, parameter publication under contention |

`REQ-TST-015` states the gate plainly, and it is worth keeping verbatim:

> Zero findings is the gate — TSan findings are not flaky, they are races.

`REQ-NFR-008` additionally requires the suites to run under **ASan + LSan** with
zero leaks, and the audio soak under Valgrind or ASan nightly. Both preset runs
are green today (186/186), but against domain code only: the concurrency the
TSan preset is *for* — the mock sink at 5 ms periods driving the callback while
another thread changes volume, EQ, and seeks — is §8.11 test 9, and it is not
written because the callback is not written.

`REQ-TST-016` (§8.11 test 10) adds the allocation gate:

> A custom allocator hook MUST assert **zero allocations** inside the audio
> callback across a 60-second run.

That hook does not exist yet either; it lands with the RT thread.

### The fuzz targets

`REQ-SEC-011` mandates seventeen libFuzzer targets over every untrusted parser,
run as a 60-second smoke on each pull request and longer nightly, with a
committed, growing corpus:

| Target | Input | Target | Input |
|---|---|---|---|
| `fuzz_id3` | ID3v1/v2 frames | `fuzz_theme` | Theme JSON |
| `fuzz_vorbiscomment` | Vorbis comment blocks | `fuzz_layout` | Layout DSL JSON |
| `fuzz_apev2` | APEv2 tags | `fuzz_skinzip` | `.eclipseskin` archives |
| `fuzz_mp4atoms` | MP4 atoms incl. `iTunSMPB` | `fuzz_efs` | EFS patterns |
| `fuzz_xinglame` | Xing/Info + LAME (`REQ-AUD-037`) | `fuzz_smartrule` | Smart-playlist rules |
| `fuzz_cue` | Cue sheets | `fuzz_icy` | ICY metadata streams |
| `fuzz_playlist` | M3U/PLS/XSPF/ASX | `fuzz_rss` | Podcast feeds |
| `fuzz_lrc` | LRC and enhanced LRC | `fuzz_ipc` | IPC messages |
| | | `fuzz_syncmsg` | Sync protocol messages |

`REQ-SEC-012` requires every target to build with ASan + UBSan and makes any
crash, hang, or sanitizer finding a **release blocker** whose input must be added
to the regression corpus. `security.yml` runs the extended 15-minute-per-target
budget nightly with corpus persistence; §28's Phase 9 gate 4 restates it as "all
fuzz targets clean at 15 minutes each". The corpus-minimisation discipline is the
counterpart to that growth: a crash input is added, then the corpus is minimised
(`-merge`) so it stays a small set of behaviour-distinct inputs rather than an
ever-growing pile of near-duplicates that slows every future run.

**No fuzz target exists yet.** The CMake option is present and off by default,
and it wires in a directory that is empty:

```cmake
option(ECLIPSE_BUILD_FUZZERS "Build libFuzzer targets (§21.6)" OFF)
if(ECLIPSE_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)     # desktop/tests/fuzz/ is an empty directory
endif()
```

So `-DECLIPSE_BUILD_FUZZERS=ON` would fail at configure for want of a
`CMakeLists.txt` there. Two things gate the work: the parsers being fuzzed
(tags, cue, playlist, skin) mostly live above the three domain modules written so
far, and libFuzzer needs Clang, which is **not installed** on this machine
(`clang++` is absent; the build uses GCC 14.2.0). The fuzz story is therefore
specified and CI-shaped but unproven locally.

## The fifteen audio verifications

`REQ-TST-001` requires each of the fifteen §8.11 claims — decode correctness, the
null test, gapless sample-exactness, equal-power crossfade, bit-perfect loopback,
EQ transfer function, THD+N, no-clipping, RT safety, RT allocation freedom,
latency accuracy, recovery, pause losslessness, DSP parity, seek exactness — to
exist as an automated test.

That table is maintained, with a per-claim **Status** column, in
[`AUDIO-ENGINE.md`](AUDIO-ENGINE.md#how-each-claim-is-proven), and is not
duplicated here so the two cannot drift. The summary it records: thirteen of
fifteen are not written because the engine they test (decoder adapter, ring
buffer, RT thread, sink) does not exist; test 6 (EQ transfer function) is half —
the analytic side is implemented and unit-tested, the measured/FFT side is not;
test 14 (desktop/Android DSP parity) is out of scope while there is no `android/`
to be the other side of the comparison; and test 3's metadata half is unit-tested
as described under [The golden corpus](#the-golden-corpus). Thirteen, one half and
one out of scope is the whole fifteen.

Phase 1 (§28) is the gate that turns tests 1, 2, 3, 9, 10, 13 and 15 green as its
exit gate 2, and test 12 — device loss and recovery — as its exit gate 6, listed
separately there rather than in the same clause. Phase 6 covers 4, 5, 6, 7, 8, 11
and 14. Between them the two phases account for all fifteen, which is worth
stating because a mapping that quietly drops one is how a verification goes
unowned.

## Android Auto — out of scope

The §27 row asks this document for "the Android Auto manual checklist"
(`REQ-AUT-020`, whose DHU run is Phase 4 exit gate 6). **It is out of scope for
this build and is not provided.** There is no `android/` tree, no Media3
`MediaLibraryService`, and no browse tree to walk with the Desktop Head Unit — so
a checklist would be a checklist for software that does not exist, which is the
kind of green-looking placeholder [ADR 0011](adr/0011-desktop-first-sequencing.md)
was written to refuse.

This is recorded, not silently dropped, because `REQ-BLD-037` names the Android
Auto manual checklist as part of the per-release checklist, and a reader
comparing this document to §27 must find the clause accounted for. When the
Android platform is built ([OQ-018](OPEN-QUESTIONS.md)), the DHU checklist —
including the 20,000-track pagination case and offline voice search — belongs
here.

The **accessibility** checklist that shares the §27 row is a different matter: it
is not Android-specific, it governs the desktop UI directly, and it is in scope
and provided in full next.

## The manual accessibility checklist

`REQ-TST-020` requires manual passes that automation cannot stand in for, and
requires them to be recorded in this file:

> Manual, per release, recorded in `docs/TESTING.md`: complete a full playback
> and playlist-building session using **only** the keyboard; then again using
> **only** a screen reader […]. Automated checks cannot substitute for this.

`REQ-BLD-037` folds the completed passes into the archived per-release checklist.
The desktop screen readers are **NVDA** and **Narrator** on Windows and **Orca**
on Linux (Qt's AT-SPI2 bridge), per `REQ-UIX-056`; TalkBack/Android is out of
scope ([ADR 0011](adr/0011-desktop-first-sequencing.md)). The target is **WCAG
2.2 level AA** (`REQ-UIX-055`).

A caveat that belongs in place: **these passes cannot be performed yet.** The Qt
UI does not build on the development machine ([OQ-017](OPEN-QUESTIONS.md)) and the
application surfaces do not exist. The checklist below is the release-time
instrument, complete and ready; it will first be executable at Phase 3, and its
first real run is a Phase 9 exit gate (`REQ-TST-020`).

### Automated pre-checks (`REQ-TST-019`)

These run in CI once the UI exists and must be green before a manual pass starts;
a manual pass is for what they cannot see, not a substitute for them.

- [ ] Contrast computed over every built-in theme; High Contrast meets AAA
      (`REQ-THM-041`, `REQ-UIX-058`).
- [ ] Every interactive element exposes a non-empty accessible name and role.
- [ ] Tab order matches visual order; no focus traps.
- [ ] Touch targets ≥ 44 px on touch surfaces, ≥ 24 px pointer-only
      (`REQ-UIX-061`).
- [ ] Layout survives 200 % text scale with no clipping (`REQ-UIX-059`).

### Keyboard-only pass (`REQ-UIX-057`)

Unplug or ignore the pointer. Complete an entire session with the keyboard alone.

- [ ] Launch the app; focus is visible on load and its position is obvious.
- [ ] Add a music source and run a scan, reaching every control by `Tab` /
      `Shift+Tab` and arrow keys.
- [ ] Start playback, adjust volume, seek, and toggle play/pause without a mouse.
- [ ] Build a playlist: create it, add tracks, reorder, and remove — all by
      keyboard.
- [ ] Edit a track's tags and save.
- [ ] Open the command palette and invoke a command that has no toolbar button.
- [ ] Open every dialog and popover; confirm each is dismissable with `Esc` and
      that focus returns to the invoking element (no focus trap).
- [ ] Confirm the focus ring is always visible and never lost behind a surface.
- [ ] Confirm playback state, selection, and errors are each conveyed by more
      than colour — an icon, glyph, or text (`REQ-UIX-058`).

### Screen-reader pass (`REQ-UIX-056`)

Repeat the same session under NVDA or Narrator (Windows) and Orca (Linux), eyes
closed where practical.

- [ ] Every control announces a name, role, value, and state.
- [ ] Icon-only controls announce the same string as their tooltip
      (`REQ-UIX-063`).
- [ ] Now-Playing changes (track, play/pause, position) are announced without
      stealing focus.
- [ ] Scan and other long operations announce progress and completion.
- [ ] Lists and grids are navigable by the reader's item navigation; item counts
      and positions are announced.
- [ ] Dialog open/close is announced and focus moves into and back out of the
      dialog correctly.
- [ ] Error and warning notices are announced when they appear.

### Motion and flashing (`REQ-UIX-060`, `REQ-UIX-062`)

- [ ] With the OS "reduced motion" setting on, non-essential animation is
      disabled and cross-fades become instant swaps; the visualizer stops unless
      explicitly re-enabled.
- [ ] No UI element and no visualizer flashes more than three times per second.

Record the outcome, the tester, the date, and the build commit against each
section, then archive the result per `REQ-BLD-037`.

## Continuous integration

The suites map onto the workflows that exist under `.github/workflows/`:

| Workflow | What it runs | Spec |
|---|---|---|
| `desktop-ci.yml` | architecture gates → configure/build/`ctest` on `ubuntu-22.04` (gcc-12), `ubuntu-24.04` (gcc-13, plus asan, tsan, clang-18), `windows-2022` (msvc) → FFmpeg licence assertion when a build links FFmpeg, and a hard failure if an adapter exists without one (OQ-042) → clang-format, clang-tidy, cppcheck | `REQ-BLD-021`, §25.2, `REQ-GEN-015` |
| `spec-ci.yml` | `validate-shared-spec.py` → draft-2020-12 schema + fixture validation → schema/fixture-sync → `theme-validate` over the corpus → desktop/Android verdict comparison | `REQ-BLD-023`, `REQ-TST-021` |
| `repo-lint.yml` | `markdownlint-cli2` (no arguments) → `commitlint` over the reviewed range → `check-doc-links.py` and `gen-third-party.py --check` for both licence documents, each preceded by its own `--self-test` | `REQ-GEN-075`, `REQ-BLD-031`, `REQ-GEN-012`, `REQ-GEN-020` |

Two notes on that mapping:

- **The documentation gates went in green, and were held back until they were.**
  `repo-lint.yml` carried a `TODO` to add `python3 tools/check-doc-links.py` "in
  the same commit that adds the […] missing §27 documents", because a gate
  introduced while red is a gate someone weakens instead of satisfying. When this
  document landed, the gate reported two missing §27 documents —
  `docs/SKIN-AUTHORING.md` and `docs/LGPL-SOURCE-OFFER.md`. Both now exist, the
  gate reports 33 documents and 224 internal links resolved with one `[v1.x]`
  note for the deferred `docs/PLUGIN-AUTHORING.md`, and the `TODO` is gone. Each
  gate runs its own `--self-test` first: the link checker's is over its
  heading-slug algorithm, which is how a false failure against a correct link in
  `docs/API.md` was found and fixed rather than worked around, and the
  generator's covers the `REQ-GEN-020` ledger rules against seven malformed
  release rows. A checker whose own rules are untested reports whatever its bugs
  allow.
- **`markdownlint-cli2` is run with no arguments, deliberately** — the file set
  and exclusions live in `.markdownlint-cli2.jsonc`, because passing a glob is
  precisely how the wrong file set once got linted ([OQ-028](OPEN-QUESTIONS.md)).

`android-ci.yml`, `security.yml` and `release.yml` are named by §25 but do not
exist yet. The first has no app to build
([ADR 0011](adr/0011-desktop-first-sequencing.md)); `security.yml`'s fuzzing, CVE
and SBOM steps wait on the fuzz targets and the packaging that produce their
inputs; and `release.yml` has nothing to release while the UI does not build.

## What is not tested here

The single most important thing this document can do is be exact about its own
gaps. The authoritative register is
[`OPEN-QUESTIONS.md` §5](OPEN-QUESTIONS.md#5--verification-status--what-is-proven-where);
this is the short form, and it defers to that section wherever they touch.

- **Only the domain layer is tested.** 186 unit tests over six modules. The
  integration tests (`REQ-TST-014`), UI/QTest and screenshot tests
  (`REQ-TST-017`, `REQ-TST-018`), property-based tests (`REQ-TST-013`), soak
  (`REQ-TST-025`), and chaos (`REQ-TST-026`) suites are specified but not
  written — the code they exercise does not exist.
- **Thirteen of the fifteen §8.11 audio verifications are unwritten**, and one is
  half-written; the RT-safety soak, the allocation-hook test, and the
  bit-perfect loopback among them. See
  [`AUDIO-ENGINE.md`](AUDIO-ENGINE.md#how-each-claim-is-proven).
- **The golden corpus and `tools/corpus-fetch` do not exist**, so no test yet
  runs against real encoded audio ([The golden corpus](#the-golden-corpus)).
- **No fuzz target is built**, and libFuzzer's Clang is not installed here
  ([Sanitizers and fuzzing](#sanitizers-and-fuzzing)).
- **The Qt UI is CI-only** — Qt is absent locally, so no window has been observed
  to open here and no accessibility or screenshot pass can run
  ([OQ-017](OPEN-QUESTIONS.md)).
- **The zero-connection test (`REQ-TST-023`) is not implemented**
  ([OQ-019](OPEN-QUESTIONS.md)); the no-telemetry property currently rests on the
  *absence* of network code, which is the strongest form of the property and
  simultaneously no test of anything.
- **`libsamplerate` is absent** ([OQ-020](OPEN-QUESTIONS.md)); resampler tests are
  Phase 6 work.
- **`arm64` and the whole Windows matrix are CI-only or untested**
  ([OQ-022](OPEN-QUESTIONS.md), [OQ-023](OPEN-QUESTIONS.md)).
- **Android is absent entirely**, which makes the parity verifications
  (`REQ-AUD-108`, test 14) and the two-engine half of conformance (`REQ-TST-021`)
  untestable rather than merely unimplemented
  ([ADR 0011](adr/0011-desktop-first-sequencing.md),
  [OQ-018](OPEN-QUESTIONS.md)).

A test that does not exist is reported as absent, never as passing. The 186 that
do exist pass under three toolchains' worth of instrumentation, and that is the
whole of what is proven on this machine today.

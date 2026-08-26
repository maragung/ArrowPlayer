# Changelog

All notable changes to Eclipse Player are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
from commit #1 (`REQ-BLD-030`). MAJOR is reserved for breaking changes to the
plugin ABI, the theme/layout schema, the sync protocol, or the settings-export
format.

**How this file is maintained** (`REQ-BLD-036`): the detail sections are
generated from conventional commits by `release.yml`, but the **Highlights**
section is written by a human before every release. A changelog that is only a
commit dump is not a changelog — it tells you what changed without telling you
what it means for you.

Deprecations appear under a `Deprecated` heading **at the time of deprecation**,
naming the version in which the item will be removed (`REQ-BLD-034`). A
deprecated plugin-ABI function, schema token, or settings key keeps working for
at least two minor versions.

## [Unreleased]

Pre-1.0.0 foundation work. Nothing here has shipped, and the version in
`desktop/version.txt` is `0.1.0` precisely so that nothing is implied to be
stable yet.

### Added

- **Specification.** `eclipse-player.md` — 4,198 lines, 525 numbered
  requirements, exit gates per phase (§28), and an appendix recording every
  correction made to the earlier draft (§29.6) so the same mistakes are not
  reintroduced.
- **Desktop domain core.** Pure C++20 with no framework dependencies: a
  `Result<T>` error type and taxonomy (§22.1), UTF-8 text handling, a hardened
  JSON parser built for untrusted input (`REQ-SEC-002`), RBJ biquad kernels, the
  10- and 18-band equalizer with derived Q (§8.9.1), and gapless metadata
  extraction for Xing/LAME, `iTunSMPB`, `OpusHead` and Vorbis granule positions
  (§8.4).
- **Architecture gates.** `tools/check-layers.py` (`REQ-GEN-050`,
  `REQ-GEN-051`), `tools/check-sql-safety.py` (`REQ-SEC-009`) and
  `tools/check-rt-safety.py` (`REQ-AUD-017`) — the dependency, SQL and
  real-time rules enforced by scripts rather than by review.
- **`shared-spec/` v1** — the cross-platform contract: five JSON Schemas, two
  EBNF grammars, the canonical design tokens, the sync protocol, and 455
  conformance fixtures (295 EFS, 38 smart-playlist, 122 theme-validation) whose
  expected verdicts are recorded in the repository. `REQ-GEN-031` requires the
  desktop and Android engines to agree on all of them.
- **`tools/validate-shared-spec.py`** — re-runs every fixture's asserted
  verdict, including the 109 that claim rejection, using a dependency-free
  draft-2020-12 subset validator with a keyword allowlist so an unimplemented
  keyword is a hard error rather than a silent pass.
- **Fuzz harnesses and a committed corpus** (`REQ-SEC-011`, `REQ-SEC-012`) —
  `fuzz_json`, `fuzz_text`, `fuzz_xinglame` and `fuzz_gapless`, each built twice:
  once as a libFuzzer target and once as a driver that replays the 49 committed
  seeds as an ordinary CTest case, so the corpus is a regression suite on every
  build rather than an artifact only the nightly job touches. The replay found
  two real defects in code added above — `normalize_relative_path()` accepted
  absolute paths, drive letters and control characters that `REQ-THM-018`
  conjoins with "normalised", and `gapless_from_granule()` executed
  signed-negation undefined behaviour on the most negative granule position.
  Both are fixed and pinned by unit tests.
- **Repository policy files.** `.gitattributes`, `.markdownlint.json`,
  `commitlint.config.js` with the §0.2 scope enum and a custom rule requiring a
  REQ id in the body of every behavioural commit, `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, `SECURITY.md`.
- **Documentation.** `docs/BUILDING.md` including a root-free user-local
  dependency build, `docs/PARITY.md` (the §29.2 matrix with an added Status
  column), `docs/PRIVACY.md`, `docs/ROADMAP.md` (the 56 `[v1.x]` requirements,
  the `[v2]` tier, and the §2.4 non-goals) and `docs/OPEN-QUESTIONS.md` (37
  registered assumptions, narrowings and gaps with stable `OQ-NNN` ids).
- **ADRs** 0001 (MPL-2.0 core, LGPL-only dependencies), 0002 (`IAudioSink` with
  native backends rather than RtAudio), 0003 (no executable code in skins), 0005
  (Qt via `aqtinstall`, everything else via vcpkg), 0006 (FFmpeg decode-only in
  an LGPL configuration with a CI assertion).

### Verified

- **193 tests pass from a clean build** on the development machine under
  `linux-release`, `linux-asan` (ASan + UBSan) and `linux-tsan`, clean under
  `-Werror`, with CMake 3.31.6 / Ninja 1.12.1 / GCC 14.2.0 — 189 GoogleTest
  cases plus the four corpus replays. The `linux-fuzz` preset builds and its
  4 replays pass. All four gate scripts pass. This is a local reproduction, not
  a CI-only assertion.
- **FFmpeg 7.1.1, TagLib 2.0.2, alsa-lib 1.2.12 and SQLite 3.46.1** are detected
  by the configure step, so the adapters they gate can be built and tested
  locally as they are written. The FFmpeg build is LGPL-configured with
  `--disable-network`.

### Not yet true

Listed because the README promises that no feature is claimed before it is built
and tested, and this section is what keeps that promise honest:

- **No audio comes out.** There is no decoder adapter, no sink, no ring buffer
  and no RT thread yet. The DSP and gapless code is unit-tested against known
  coefficients and known headers, not against playback.
- **No user interface.** No `main.cpp`, no window, no Qt UI.
- **Sixteen of `REQ-SEC-011`'s seventeen fuzz targets do not exist.** Only
  `fuzz_xinglame` is one of the named seventeen; the other three harnesses are
  supporting targets and are not counted as standing in for any of them. The
  remaining sixteen arrive with the parsers they name (Phases 2, 3, 4, 5, 7 and
  8). No placeholder harness was added to make the ledger read better. Tracked
  as `OQ-043`.
- **No fuzzing engine has ever run.** GCC 14 does not provide
  `-fsanitize=fuzzer`, and Clang is not installed here, so every finding so far
  came from replaying seeds a human wrote. The 60-second-per-target smoke in
  `desktop-ci.yml` has not executed either, because nothing has been pushed.
- **No Android application.** `android/` does not exist, so `REQ-GEN-030` (the
  repository layout) is unmet and Phase 0 exit gate 2 stays red. Recorded in
  `docs/adr/0011-desktop-first-sequencing.md`.
- **`REQ-GEN-031` is half-proven.** One implementation conforming to the
  fixtures is not two implementations agreeing on them.
- **The Qt UI is unverified anywhere but CI.** Qt is the one dependency that is
  not installed on the development machine, so Phase 0 exit gates 1 ("the window
  opens") and 7 ("the version is shown in About") are CI-only. It is not claimed
  that a window has been observed to open. Tracked as `OQ-017` in
  `docs/OPEN-QUESTIONS.md`.
- **Windows and `arm64` are CI-only.** WASAPI, the jump list and the installer
  have no local test evidence (`OQ-022`, `OQ-023`).

[Unreleased]: https://github.com/maragung/ArrowPlayer/commits/main

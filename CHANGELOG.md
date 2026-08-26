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

- **A negative test for every gate script** (`REQ-TST-024`, `OQ-045`).
  `check-layers.py`, `check-sql-safety.py`, `check-rt-safety.py` and
  `validate-shared-spec.py` each grew a `--self-test` that plants the defect the
  gate exists to catch and requires it to be caught, over synthetic input rather
  than committed fixtures — a planted violation inside this repository would be
  found by the gate itself. They run in CI *before* the gates they belong to, so
  the instruments are checked before the measurement is taken. The four differ in
  how they get their input, because the input shape follows from how each reads
  the tree: `check-layers.py` materialises throwaway trees under `/tmp` and its
  four checks now take their roots as arguments; the two source linters split a
  `scan_lines(name, lines)` core out of `scan(path)`; `validate-shared-spec.py`
  copies `shared-spec/`, plants one defect in the copy, and re-runs itself against
  it through a new `--spec-root`.
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
- **Hardening verified in the binaries, not the build files** (`REQ-SEC-018`) —
  `tools/check-hardening.py`, a standard-library ELF and PE header reader that
  requires PIE, RELRO, `BIND_NOW`, a non-executable stack and a stack protector in
  everything the release build links, and the four Windows `DllCharacteristics`
  bits plus `/GS` and `/CETCOMPAT` on PE. It exists because `REQ-SEC-018` says CI
  must check the produced binaries rather than the build files, and this tree was
  the case in point: `eclipse_set_hardening()` had been defined and called by
  nothing for several commits, so every flag was in the CMake and in no binary.
  Wiring it in exposed a second gap on the first run — the four fuzz replay
  drivers had only partial RELRO, `-z now` being nobody's default. The script
  carries a `--self-test` of 31 assertions over synthetic binaries, 22 of them
  planted defects that must be caught; `_FORTIFY_SOURCE` and `/GS` are advisory
  per binary and mandatory across a build, because a binary that makes no
  fortifiable call is evidence about the source rather than about hardening.
- **Every CI action pinned to a commit SHA, and a gate that keeps it that way**
  (`REQ-SEC-013`, `OQ-050`). All 22 `uses:` references across the three workflows
  moved from a floating major tag — `actions/checkout@v4` and five others — to a
  full 40-character SHA with the version in a trailing comment. Each tag was
  resolved to a concrete release and verified twice, by the GitHub API and by
  `git ls-remote refs/tags/<tag>^{}`, both agreeing; every SHA stays inside the
  major line the workflow already named, so no pin is a silent major upgrade.
  Found by measurement rather than review: `grype dir:.`, run while quantifying
  the SBOM's CVE coverage and expected to report nothing, flagged
  `GHSA-cxww-7g56-2vh6` against `actions/download-artifact@v4`. That match was
  probably a false positive — and beside the point, since the reason a scanner
  cannot decide is the reason the pin is wrong. `tools/check-action-pins.py`
  enforces it in a new `action-pins` job in `repo-lint.yml`, rejecting mutable
  refs, branches, abbreviated and upper-case SHAs, a missing or prose-polluted
  version comment, and an undigested `docker://` image. It reads the workflows as
  text on purpose: comments are not part of a parsed YAML document, and a control
  pair differing only in the trailing comment shows syft recording either
  `@v4.4.0` or the bare SHA as the component version — so a `safe_load` gate
  would pass a pin no CVE database can order. Post-pin `grype dir:.` reports 0
  matches.

- **`dependabot.yml`, and the commit gate made able to accept it**
  (`REQ-SEC-013`, `REQ-GEN-030`, `OQ-050`, `OQ-051`). Pinning every dependency
  buys determinism and gives up automatic security fixes; unlike a floating tag,
  a pin goes stale silently. Dependabot is what pays that back, so the pin and
  this file belong together. Three ecosystems — the actions, the two Node gates,
  and the hash-locked `jsonschema` stack in `.github/` — each grouped into one
  pull request. Two of its behaviours were read out of dependabot-core rather
  than assumed: its pip fetcher accepts the non-standard filename
  `requirements-spec.txt` because `requirements_file?` matches `/requirements/`,
  and its requirement replacer rewrites `--hash=` entries, so a bump will not
  leave `--require-hashes` pointing at the old artifact. What it does **not**
  cover is in its own comments: there is no vcpkg ecosystem, so §4.2's C and C++
  dependencies — the bulk of the attack surface — stay hand-updated.
  Landing it needed a measured change to `commitlint.config.js`. Two rules
  rejected Dependabot's own commits: `scope-enum` had no `deps-dev`, which
  dependabot-core picks by whether the dependency is a production one and offers
  no way to configure, and the 72-column header limit cannot hold a subject that
  measures 98 columns against this repository's package names. Rather than raise
  the limit for every commit or exempt the bot from every rule, the exception is
  scoped to the message shape: a subject in Dependabot's bump grammar is allowed
  100 columns, everything else keeps 72, and all other rules apply unchanged. It
  stays a limit — a 105-column bump subject still fails, naming which allowance
  it exceeded — and the existing 34-commit history re-lints with 0 problems.

- **`REQ-SEC-004`'s "new" now has a definition, and a gate that enforces it.**
  The requirement fails the build on any *new* high-severity finding and never
  said new against what. *Since the last run* would make the verdict depend on
  scheduler history; *since the last release* degenerates to "any" while no
  release exists. So new means **absent from `security/cve-baseline.json`**: one
  entry per accepted finding, naming the component, the exact version, why it
  does not apply to this build, and the date.
  `tools/check-cve-baseline.py` reads `grype -o json` and enforces it. An entry
  matching nothing in the scan is an error, so upgrading past a CVE forces the
  stale entry out instead of letting the file rot into a suppression list; an
  entry binds to an exact version, so a bump invalidates acceptances made
  against the old one; and a placeholder reason is rejected. Critical gates
  alongside High. The file ships empty because the gating scan finds nothing —
  0 matches at any severity over both the SBOM and the tree — so it was proven
  non-vacuous against real scanner output instead: a known-vulnerable component
  injected into a copy of the SBOM produced 3 High findings, which the gate
  rejected, then accepted once baselined, then rejected again as stale when the
  injection was removed.

- **The register of open questions is now checked, not counted by hand.**
  `tools/check-doc-links.py` parses `docs/OPEN-QUESTIONS.md` and fails on a
  duplicate id, a hole in the sequence, a heading whose status is not in the
  legend, or an `OQ-` citation anywhere in the tree that resolves to no entry. It
  prints the total instead of trusting one: 51 entries, contiguous. The count in
  this file had drifted twice — 39, then 48 — because six entries live as table
  rows in section 3 rather than as headings, and a heading-only count misses them.
  Six planted cases in its self-test, and three mutations of the committed files,
  each turn it red. One real defect fell out: OQ-046 was marked `**Measured**`,
  which describes evidence rather than a decision, and is now `**Open**`.

- **One dependency register, three generated documents.** `§4.2` lives as data
  in `tools/gen-third-party/register.json`; nothing derived from it is authored
  twice. `tools/gen-third-party/gen-third-party.py` emits `docs/THIRD-PARTY.md`
  (`REQ-GEN-012`) and `docs/LGPL-SOURCE-OFFER.md` with its per-tag release ledger
  (`REQ-GEN-020`); `tools/gen-sbom.py` emits
  `docs/sbom/eclipse-player.cdx.json`, a CycloneDX 1.6 document with 23
  components (`REQ-GEN-021`, `REQ-SEC-014`). `repo-lint.yml` runs `--check` on
  all three for every push and pull request, so a hand-edit or a dependency that
  reached `vcpkg.json` without a register entry is a red build rather than a
  discovery at release time.

  The SBOM identifies vcpkg ports as `pkg:vcpkg/<port>` — the type is registered,
  and `pkg:generic` was simply wrong — carrying `repository_revision` from the
  pinned registry baseline and a `triplet` only where one is actually known: a
  host-side helper port is not built for the target triplet, so stamping it with
  one would be a wrong answer rather than a missing one. `port_version` is
  emitted only when known, because an absent qualifier already asserts
  port-version 0. `--resolved-graph` consumes `vcpkg install --dry-run` output to
  supply exact versions and the transitive set, and `--diff` implements §25.4
  step 4. All eight of these invariants were mutation-tested — each deliberately
  broken, each caught — after two of them turned out to be covered by no test at
  all.

  What this does **not** yet do is recorded rather than glossed: the three common
  CVE scanners either skip `pkg:vcpkg` components or match them to nothing, so
  the scanning half of `REQ-SEC-014` would report clean over no coverage
  ([OQ-046](docs/OPEN-QUESTIONS.md)); ten of thirteen components carry a version
  series rather than a release, which `--resolved-graph` resolves for nine of
  them (OQ-047); and the document validates against the canonical
  `bom-1.6.schema.json` with zero errors, but through a throwaway draft-07
  adapter rather than a validator this repository ships (OQ-048).
- **Repository policy files.** `.gitattributes`, `.markdownlint.json`,
  `commitlint.config.js` with the §0.2 scope enum and a custom rule requiring a
  REQ id in the body of every behavioural commit, `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, `SECURITY.md`.
- **Documentation.** `docs/BUILDING.md` including a root-free user-local
  dependency build, `docs/PARITY.md` (the §29.2 matrix with an added Status
  column), `docs/PRIVACY.md`, `docs/ROADMAP.md` (the 56 `[v1.x]` requirements,
  the `[v2]` tier, and the §2.4 non-goals) and `docs/OPEN-QUESTIONS.md` (51
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
- **Every Linux clause of `REQ-SEC-018` is observable in a real binary.** In
  `build/linux-release/tests/test_core`: `DYN` with `FLAGS_1: NOW PIE`,
  `PT_GNU_RELRO`, `FLAGS: BIND_NOW`, `GNU_STACK … RW`, `__stack_chk_fail` and
  `__snprintf_chk`. `tools/check-hardening.py` verifies all 7 binaries the build
  produces and, pointed at `build/linux-asan`, exits 2 rather than passing
  vacuously on binaries the requirement does not govern.
- **The four new self-tests, run locally:** 29 synthetic trees over
  `check-layers.py`'s four checks (16 planted violations); 10 injection sites
  caught and 8 safe constructs left alone by `check-sql-safety.py`; 11 false
  `/// RT-SAFE:` claims caught and 7 legitimate constructs left alone by
  `check-rt-safety.py`, whose span finder is asserted to bound both bodies of a
  two-function source; and 14 defects planted one at a time in a copy of
  `shared-spec/`, each caught with the *specific* complaint it should provoke,
  over a control run on the unmutated copy that passes. The `validate-shared-spec`
  harness was then checked for vacuity by replacing one mutation with a no-op and
  misspelling one expected complaint: it reported both, naming them.
- **One blind spot is pinned rather than fixed.** `check-sql-safety.py` matches
  uppercase SQL keywords only, which is what keeps it silent on prose like
  `" limit=" + n`; lowercase SQL evades it. The self-test asserts the lowercase
  case is *not* flagged, so making the matcher case-insensitive fails the test and
  forces the trade-off to be re-decided rather than discovered later.
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
- **`REQ-SEC-018` is verified on one platform of two.** The PE half of
  `check-hardening.py` has only ever read the synthetic binaries in its own
  self-test; no MSVC-produced PE has been through it, here or in CI. Its Windows
  step runs non-blocking until it has passed once, with the condition for
  removing that recorded in `OQ-044` rather than left to judgement.
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

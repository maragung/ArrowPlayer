# Open questions, assumptions, and known gaps

§27 defines this file as *"anything the implementation had to assume, with the
assumption made"*. §0.1 rule 1 is where the obligation comes from:

> **Never invent a requirement.** If something is not specified here and you need
> it, add it to `docs/OPEN-QUESTIONS.md` with a proposed answer and proceed with
> the proposal, clearly marked `ASSUMPTION:` in a code comment.

So this is not a wishlist or a backlog. It is the register of places where the
specification did not decide, decided twice, or where the implementation is
narrower than the requirement — recorded because §0.1 rule 2 forbids downgrading
a requirement silently, and silence is exactly what an undocumented narrowing is.

Each entry has a stable `OQ-NNN` id so other files can cite it without quoting
it. Ids are never reused, even after an entry closes.

## Status legend

| Status | Meaning |
|---|---|
| **Open** | Needs a decision by a maintainer. The implementation is proceeding on the stated assumption. |
| **Settled** | Decided, recorded in a fixture or schema, and the reasoning is written down. Reopening it is a specification change. |
| **Gap** | Not a question — something the specification requires that does not exist yet. Tracked so it is not mistaken for done. |

---

## 1 · Specification conflicts — the specification contradicts itself

These are the valuable ones. Each is recorded as a fixture rather than resolved
unilaterally, because picking a side in the implementation would silently
downgrade whichever requirement lost.

### OQ-001 — `REQ-THM-011` vs the §11.2 theme schema · **Open**

`REQ-THM-011` promises that a theme can extend a built-in one in about five lines
and change only the accent colour. The §11.2 schema requires
`color.{background,surface,text,accent,border}` **and** `typography` on every
theme, including one with `extends`.

Both cannot be true. Either `extends` makes the inherited groups optional, or the
five-line promise is wrong.

- **Assumption in force:** the schema is authoritative. A theme with `extends` and
  only `color.accent` is **invalid** today.
- **Recorded as:** `shared-spec/conformance/theme-validation-cases/invalid/theme-extends-accent-only.json`,
  listed under `openConflicts` in that directory's `index.json`.
- **On resolution:** if the requirement wins, the schema gains a conditional that
  relaxes `required` when `extends` is present, and the fixture **moves** from
  `invalid/` to `valid/` — a fixture move, not a fixture edit, so the diff shows
  what changed.
- **Recommendation:** the requirement should win. Five-line accent overrides are a
  named differentiator, and requiring a full colour ramp to change one value
  defeats the point of inheritance.

### OQ-002 — `REQ-THM-040` step 10 vs `REQ-EFS-009` · **Open**

`REQ-THM-040` step 10 verifies an "output cap" at install time. `REQ-EFS-009`
truncates EFS output at 4096 characters at runtime, unconditionally.

If runtime truncation is unconditional, an over-long pattern is already harmless,
and install-time verification cannot fail on it. Conversely, install-time
verification is only meaningful if such a package is refused — which the runtime
rule makes unnecessary.

- **Assumption in force:** both verdicts are recorded, and neither is discarded.
  The fixture carries the accept-under-runtime-truncation verdict *and* the
  reject-under-install-verification verdict, so whichever reading is chosen has
  its evidence already written.
- **Recorded as:** `malicious/layout-efs-output-bomb.eclayout`, also under
  `openConflicts`.
- **Recommendation:** keep runtime truncation as the safety property and demote
  step 10 to a **warning** at install time. A hard refusal punishes an author for
  a pattern the runtime handles correctly, and the warning still tells them.

### OQ-003 — `REQ-THM-027` vs `REQ-THM-031` · **Settled, narrowly**

`REQ-THM-027` says the layout state model exposes no settings values.
`REQ-THM-031`'s worked example binds `settings.showMiniVisualizer`.

- **Assumption in force:** the narrowest reading that keeps both statements true —
  a short **allowlist** of presentation-affecting settings, and nothing else.
  `layout.schema.json` permits exactly five: `settings.showMiniVisualizer`,
  `settings.showVisualizer`, `settings.showLyrics`, `settings.reducedMotion`,
  `settings.accessibleContrast`.
- **Why an allowlist:** a skin must never be able to read `settings.libraryPaths`,
  a credential, or a device name. An allowlist refuses an unknown key without
  anyone having to predict its name — the same reasoning as `REQ-THM-018`'s path
  handling.
- **Why this is marked Settled rather than Open:** unlike OQ-001 and OQ-002, one
  reading here is strictly safer and satisfies both requirements' *intent*. It is
  still recorded, because the schema is narrower than `REQ-THM-031` read literally.
- **Not listed under `openConflicts`:** that array in `index.json` holds the two
  conflicts that are still genuinely open. This one is resolved in the schema, so
  it is registered here as a narrowing instead. `shared-spec/README.md` says so
  explicitly, so the two counts do not appear to disagree.

---

## 2 · Documented narrowings — the implementation is stricter than the requirement

Each of these makes the implementation refuse something the requirement, read
literally, would allow. That is a downgrade, so each one is written down here.

### OQ-004 — Contrast ratios ignore alpha · **Settled**

`REQ-THM-041` requires WCAG 2.2 contrast checks. The implementation computes the
ratio from the **RGB channels only**, ignoring the alpha channel of a foreground
colour.

- **Why:** compositing the foreground over its real backdrop would be more
  accurate but is not computable at validation time. The backdrop depends on which
  surface the text lands on and on `assets.backgroundOpacity`, neither of which is
  known when a package is validated.
- **Consequence:** a semi-transparent foreground is judged against its own RGB
  value, which is the conservative and — more importantly — **deterministic**
  choice. Determinism is load-bearing here: `REQ-GEN-031` requires both engines to
  agree to three decimal places, and a compositing model would have to agree too.
- **Recorded as:** `contrast/pairs.json` → `alphaHandling`, and one fixture whose
  `why` states that the ratio ignores alpha.

### OQ-005 — Any DOCTYPE in an SVG is refused, unconditionally · **Settled**

`REQ-THM-042` requires SVG sanitisation. The implementation refuses **any**
`<!DOCTYPE …>` declaration, including the entirely conventional
`-//W3C//DTD SVG 1.1//EN` one, without inspecting what it references.

- **Why:** a DOCTYPE is where entity declarations live. A validator that permits
  "harmless" DOCTYPEs must decide which ones are harmless, and that decision has a
  long history of being got wrong — XXE, parameter entities, billion laughs, and
  quadratic blowup all arrive through a DOCTYPE that looked fine.
- **Consequence:** legitimate exports from older editors are refused. The fix
  belongs in the authoring tool, and `docs/SKIN-AUTHORING.md` will say so with the
  one-line removal an author needs.
- **Recorded as:** `malicious/svg-doctype.svg` with the reasoning in its `note`.

### OQ-006 — The manifest allowlist pattern refuses non-ASCII filenames · **Settled**

Zip-entry path validation accepts `images/café.png`; the manifest's own filename
allowlist pattern is stricter and refuses it.

- **Assumption in force:** the stricter pattern stands. Two layers disagreeing in
  the *safe* direction is acceptable; the reverse would not be.
- **Consequence:** an author cannot name an asset with non-ASCII characters. This
  is a real limitation for non-English authors and is the narrowing here most
  likely to deserve revisiting.
- **Recorded as:** `malicious/zip-entry-paths.json`, the `images/café.png` case.

---

## 3 · Settled ambiguities — the prose left a fork, the fixture chose

These were not contradictions; the specification simply did not say, and a choice
had to be made to write any code at all. Each is settled in a normative fixture
with its reasoning, per `shared-spec/README.md` §"The fixtures are normative".
They are listed here because §27 asks for *anything the implementation had to
assume*, and every one of these silently produces wrong output if an engine
chooses the other way.

| Id | Question | Assumption in force | Why the alternative was rejected |
|---|---|---|---|
| **OQ-007** | Are EFS string indices zero- or one-based? (`$sub`, `$strchr`, `$strstr`) | **Zero-based** | A one-based reading is equally plausible and produces off-by-one output everywhere, silently |
| **OQ-008** | Do EFS lengths and cuts count bytes, code points, or grapheme clusters? | **Grapheme clusters** | The functions exist to fit text into a column; a user counts what they see |
| **OQ-009** | Is `$upper`/`$lower` locale-sensitive? | **Locale-independent** — `$lower(I)` is `i` even under `tr-TR` | If locale governed case, two users would see different strings for the same library and `REQ-GEN-031` would be unsatisfiable. Locale governs dates, numbers and durations only |
| **OQ-010** | What does a malformed function call render as? | Cannot be **applied** (unknown name, wrong arity) → renders **literally**. Applied with **invalid operands** → yields **absent** | One rule, two outcomes. Collapsing them would either hide typos or turn them into empty output |
| **OQ-011** | `$div` by zero, overflow, non-numeric operands? | **Absent** | Not zero and not an error: missing data must not become a confident wrong number |
| **OQ-012** | Does an optional block with **no** field reference collapse? | **Renders in full** | Read literally, §10.3's collapse condition is vacuously true for such a block, which would make `[ - ]` vanish. The rule exists to hide separators belonging to absent data, not separators generally |

Changing any row is a `v2` schema change under `shared-spec/README.md`'s
versioning rules, because it changes what an existing conformance case expects.

---

## 4 · Process and infrastructure gaps

Not questions. Things the specification requires that do not exist, recorded so
they are not mistaken for done.

### OQ-013 — No monitored security mailbox and no published PGP key · **Gap**

`SECURITY.md` promises a disclosure process. GitHub private vulnerability
reporting is live and is the channel we can honestly promise to read. A dedicated
mailbox and a published PGP key are **not** in place.

- **Blocking:** both are 1.0.0 release blockers. Shipping binaries with no
  out-of-band reporting channel is not acceptable for software that parses
  untrusted skin packages and untrusted audio files.
- **Owner:** maintainer. Needs a domain, a monitored inbox, and a key whose
  fingerprint is published somewhere other than the repository.

### OQ-014 — No second Code of Conduct contact · **Gap**

`CODE_OF_CONDUCT.md` names `@maragung` as the contact. With a single maintainer
there is no path to report an incident *involving that maintainer* to anyone else
inside the project. GitHub's Community Guidelines reporting is the documented
escalation, which is honest but external.

- **Resolution:** a second named contact, as soon as a second person is
  sufficiently involved to be one. Not solvable by writing text.

### OQ-015 — The dependency denylist gate is not wired into CI · **Gap**

§19.5 makes "no telemetry" a **structural** property — the analytics and
telemetry SDKs must be absent from the dependency tree, enforced by a denylist
check in `security.yml`. `security.yml` does not exist yet.

- **Assumption in force:** the property currently holds because there are no
  network dependencies at all, which is true but proves nothing about the future.
  `docs/PRIVACY.md` says so in its own words.
- **Blocking:** `security.yml` is Phase 0 scaffolding and is next in sequence.

### OQ-016 — Two commits predate `commitlint.config.js` and do not satisfy it · **Settled**

`bf91096` ("Initial commit") is not a conventional commit, and `873e5be` predates
the config. CI lints the commits **in the range under review**, not all of
history, so this is not a permanent red gate.

- **Assumption in force:** history is not rewritten to satisfy a rule added later.
  Rewriting `main` would invalidate every commit hash quoted in an ADR.
- **Note:** the rule was applied to my own work retroactively where it was still
  cheap. Commit `e151ebe` originally had an 81-character subject, over the
  72-character limit the config sets; it was amended down to 64 rather than the
  limit being raised to accommodate it.

---

## 5 · Verification status — what is proven where

The governing record for the scope decision behind this section is
[ADR 0011](adr/0011-desktop-first-sequencing.md).

### What *is* verified on the development machine

Stated first, because the rest of this section is gaps and it would be dishonest
to let them imply that nothing has been run.

| Check | Result |
|---|---|
| `cmake --preset linux-release` + `ctest` from a clean build directory | **184/184 passed** |
| `cmake --preset linux-asan` + `ctest` (ASan + UBSan) | **184/184 passed** |
| `cmake --preset linux-tsan` + `ctest` (ThreadSanitizer) | **184/184 passed** |
| `-Werror` with the strict warning set | clean |
| `tools/check-layers.py`, `check-sql-safety.py`, `check-rt-safety.py` | pass |
| `tools/validate-shared-spec.py` | pass — 5 schemas, 102 JSON documents |

Toolchain in use: CMake 3.31.6, Ninja 1.12.1, GCC 14.2.0, and — in a user-local
`~/.local` prefix, no root required — FFmpeg 7.1.1 (LGPL-configured,
decode-oriented, `--disable-network`), TagLib 2.0.2, alsa-lib 1.2.12,
SQLite 3.46.1.

So `README.md`'s "184 tests, all passing" is locally reproduced, not merely
asserted by CI.

### OQ-017 — Qt is absent; Phase 0 exit gates 1 and 7 are CI-only · **Gap**

Gate 1 ("the window opens") and gate 7 ("the version is shown in About") require
Qt 6.8+. Qt is not installed on the development machine, `aqtinstall` was not
authorised, and installing it system-wide needs a `sudo` password that is not
available. `find_package(Qt6 …)` reports `OFF`, so `desktop/ui/` is never
configured.

- **Assumption in force:** the Qt UI is reported as **CI-verified only**. It is
  never claimed that a window has been observed to open on this machine.
- **Note this is the *only* dependency that is genuinely missing.** FFmpeg,
  TagLib, ALSA and SQLite are all present and detected, so the adapters those
  gate can be built and tested locally as they are written.

### OQ-018 — Phase 0 exit gate 2 is red; `REQ-GEN-030` is unmet · **Gap**

There is no `android/` tree, so the repository layout `REQ-GEN-030` mandates is
not matched, and gate 2 (`android-ci.yml` green, APK running on an emulator) has
nothing to run.

Downstream consequences, from ADR 0011:

| Requirement | Status |
|---|---|
| `REQ-GEN-031` — both engines agree on the conformance fixtures | **Half-proven.** One engine conforming is not two engines agreeing |
| `REQ-AUD-108` — desktop/Android DSP within −90 dBFS RMS | **Untestable.** One side of the comparison is missing |
| `REQ-LIB-001` — a schema change lands on both platforms in one commit | **Unenforceable in one direction.** There is no second platform to fail the gate |

An empty `android/` skeleton was explicitly rejected as a way to turn the gate
green: a gate that passes without protecting anything is worse than a red one,
because a red gate still tells the truth.

### OQ-019 — `REQ-TST-023`, the zero-connection test, is not implemented · **Gap**

The test that proves no connection is attempted with default settings lands with
the network layer. Until then the zero-connection claim rests on the **absence**
of network code. That is the strongest possible form of the property and
simultaneously no test of anything.

Worth recording as a supporting fact rather than as proof: the local FFmpeg is
built `--disable-network` and with `--enable-protocol='file,pipe'` only, so the
decode path has no network transport even available to it.

### OQ-020 — `libsamplerate` is not installed · **Gap**

`REQ-GEN-012` requires libsamplerate ≥ 0.1.9 — earlier releases were GPL and
would be incompatible with the MPL-2.0 core. It is the one optional dependency
`EclipseDependencies.cmake` looks for that is absent locally, so
`ECLIPSE_HAVE_SAMPLERATE` is `OFF`.

- **Impact:** none yet. The resampler is Phase 6 work. This is recorded so that
  when Phase 6 starts, the missing dependency is a known item rather than a
  surprise, and so the version floor is not forgotten — a system libsamplerate
  older than 0.1.9 would be a **licence** problem, not just a feature gap.

### OQ-021 — Dependency detection needs `PKG_CONFIG_PATH` for a user-local prefix · **Settled**

`EclipseDependencies.cmake` finds FFmpeg, TagLib, ALSA and libsamplerate through
`pkg-config`. With the libraries in `~/.local`, pkg-config does not search there
by default, so a plain `cmake --preset linux-release` reports every adapter `OFF`
even though all of them are installed — a silent, misleading result.

- **Assumption in force:** the presets are **not** patched to prepend a
  user-local prefix. Hardcoding `$HOME/.local` into a committed preset would
  change dependency resolution for every contributor to suit one machine.
- **The environment is the caller's responsibility**, and `docs/BUILDING.md`
  documents it:

  ```bash
  export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
  export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
  ```

- **Open sub-question:** whether the configure summary should *warn* when a
  dependency is missing rather than printing a bare `OFF`. The current output is
  truthful but easy to misread as "this machine cannot build the adapter" when it
  actually means "pkg-config was not told where to look". Leaning yes.

### OQ-022 — `arm64` is not CI-tested · **Gap**

§3.1 lists `arm64` in the support matrix. No arm64 runner is configured, so arm64
is a supported *target* with no test evidence. Recorded in `docs/PARITY.md` under
permanent deviations, because it will not be resolved by writing code.

### OQ-023 — The Windows matrix is CI-only · **Gap**

The development machine is Linux. `windows-debug`, `windows-release` and
`windows-arm64` presets exist and are exercised only by `desktop-ci.yml`. WASAPI,
MMDevice, the jump list and the installer are all CI-or-nothing until someone
runs them on Windows hardware.

### OQ-024 — Gapless corpus generation needs an MP3 encoder · **Settled**

§8.11 test 3 compares decoded output byte-for-byte across a track boundary, which
requires an *encoder* to build the corpus in the first place. FLAC, Vorbis, Opus,
ALAC and WavPack encoders are native to FFmpeg; **MP3 is not** — it needs
`libmp3lame`.

- **Resolved:** the local FFmpeg is built `--enable-libmp3lame`, so the MP3-LAME
  gapless case can be generated and verified locally. It does not have to be
  marked CI-only.
- **Licence:** `libmp3lame` is LGPL-2.1, and `REQ-GEN-016` permits it explicitly
  provided it stays a separately-enabled optional component so the default build
  remains minimal. Enabling it for **corpus generation** does not put an MP3
  encoder in a shipped player build — `REQ-GEN-016` is about the converter, which
  is `[v1.x]` and does not exist.
- **Not permitted, for contrast:** `libfdk_aac`. Non-free, and refused regardless
  of what it would make testable.

## 6 · How an entry closes

1. Make the decision, in an ADR if it changes the specification.
2. Update the requirement text **and** the affected fixtures in the same commit,
   so the schema/fixture-sync gate (`REQ-BLD-023`) has something to verify.
3. If the entry recorded a wrong earlier claim, add a row to §29.6 — that table
   exists so mistakes are not silently reintroduced.
4. Move the entry to a `## Closed` section here with the resolution and the
   commit. **Do not delete it.** The point of a register is that it still shows
   what was once uncertain, and an entry that vanishes leaves the next reader
   unable to tell whether it was answered or forgotten.

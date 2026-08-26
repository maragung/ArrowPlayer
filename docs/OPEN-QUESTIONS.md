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
- **What adopting it costs, precisely:** the fixture's `verdict` becomes
  `accept-with-warning` and `stepTenVerdict` is deleted. That is the whole change
  — one line of corpus and both engines follow, because `verdict` is the only
  field `.github/scripts/compare_verdicts.py` holds an implementation to
  (OQ-029). Recorded here so the decision is cheap to execute rather than a
  research task later.
- **Until then the conflict is machine-visible, not just prose.** The fixture is
  in the index's `openConflicts`, and a verdict disagreement on it is reported as
  this conflict by name instead of as an engine bug.

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

### OQ-025 — `REQ-GEN-012`'s gate cannot pass against a resolved graph · **Open**

`REQ-GEN-012` says CI "MUST fail if a dependency appears in the build that is
absent from" the §4.2 register. The register lists **direct** dependencies. A
resolved dependency graph contains more than that. Resolving
`desktop/vcpkg.json` against the pinned baseline for `x64-linux-eclipse` yields
six components no row in §4.2 describes:

| Component | Pulled in by | Licence |
|---|---|---|
| `utfcpp` | taglib | Boost-1.0 |
| `glm` | projectm | MIT |
| `projectm-eval` | projectm | LGPL-2.1-or-later |
| `opengl`, `opengl-registry`, `egl-registry` | projectm | Apache-2.0 / MIT |

None of them is a licence problem. All of them would fail the gate as the
sentence is literally written, which means the gate as written can only be
satisfied by not running it.

- **Assumption in force:** the §4.2 table is the register of **direct**
  dependencies and is reproduced verbatim in `docs/THIRD-PARTY.md`;
  `docs/THIRD-PARTY.md` additionally carries the transitive components, generated
  from the resolved graph by `tools/gen-third-party`; and the CI gate compares
  the graph against `docs/THIRD-PARTY.md` rather than against §4.2 alone.
- **Why this is the narrow reading and not a weakening:** the property
  REQ-GEN-012 protects is "nothing links into the build that nobody has looked
  at". Comparing against a generated document that must be regenerated and
  committed keeps that property and makes it *stronger*, because §4.2 alone has
  never enumerated the transitive set at all.
- **Recommendation:** amend REQ-GEN-012 to name `docs/THIRD-PARTY.md` as the
  comparison target explicitly, since that is already the document §25.5 step 8
  requires the release to regenerate and fail on if stale.
- **The npm half of this problem does not exist.** `package-lock.json` is the
  resolved graph and it is committed, so the denylist gate reads the transitive
  set directly with no separate resolution step. Only the vcpkg side needs
  `vcpkg install --dry-run` to see past the manifest, which is why
  `check-dependency-denylist.py` takes `--resolved-graph` and prints, in its own
  output, that transitive ports are not covered without it.
- **Not deferred:** the two transitive components that *would* have been a
  problem were removed rather than registered. `libzip`'s default features pull
  in bzip2 and OpenSSL; `desktop/vcpkg.json` requests it with
  `"default-features": false`, both because `REQ-THM-015` allows a skin archive
  only "deflate or stored, no encryption, no other compression methods" and
  because OpenSSL has no row in §4.2.

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

### OQ-029 — What "identical results" means in `REQ-GEN-031` · **Settled**

`REQ-GEN-031` requires the desktop and Android theme validators to "produce
identical results" over `shared-spec/conformance/`, and `REQ-TST-021` makes a
disagreement a build failure. Neither says whether "result" means the verdict
alone or the verdict together with the pipeline step that produced it, and the
two readings fail differently, so `.github/scripts/compare_verdicts.py` had to
choose one.

- **Assumption in force:** the **verdict** is the result and is compared
  strictly — any difference fails the build. The `pipelineStep` is compared as
  well, but a difference there is a **warning**, not a failure.
- **Why the strict reading of `pipelineStep` was rejected:** a fixture can be
  judged at more than one step, and `REQ-THM-040` stops at the first failure, so
  two conforming engines can attribute the same verdict to different steps.
  `malicious/layout-efs-output-bomb.eclayout` is the case already in the corpus:
  it is schema-valid at step 6 and contested at step 10, and its own `reason`
  says "Step 10 is the judge, not step 6". Failing on step attribution would make
  the gate demand an implementation detail the specification never fixed, and the
  usual way such a gate gets satisfied is by weakening it later.
- **Why it is not simply dropped either:** a step divergence normally *does* mean
  one engine ordered its pipeline wrongly, which is a real defect that happens to
  be invisible in the verdict. It is printed as a GitHub warning annotation so a
  human sees it, without a red build that cannot be made green honestly.
- **The corpus is the third party in every comparison.** Two engines can agree
  and both be wrong, so each report is also checked against the verdict
  `index.json` requires. Agreement with each other is necessary; agreement with
  the corpus is the point.
- **Coverage is checked before agreement.** A report that omits a case would
  "agree" with everything, which is the cheapest way for this gate to become
  decorative — so a report missing any tracked case fails, as does a report
  carrying a case `index.json` does not list.
- **Reports also carry a `corpusFingerprint`** (sha256 of `index.json`). Two
  reports produced against different revisions of the corpus would compare as
  agreeing while proving nothing, and that failure is invisible without it.
- **A disagreement on an `openConflicts` fixture still fails.** `REQ-TST-021`
  exempts nothing, and two shipped engines that disagree are exactly what
  `REQ-GEN-031` forbids whatever the reason. What changes is the *message*: the
  comparator names the conflict, notes that the reported value is one of the
  fixture's recorded alternate readings, and says the fix is to resolve the open
  question rather than patch an engine. Without that, a maintainer spends a day
  debugging an engine over a decision nobody has made.

**A corpus defect this gate exposed, fixed here.** Writing the comparator meant
deciding which of `verdict`, `schemaVerdict`, `enforcedVerdict` and
`stepTenVerdict` an implementation is actually held to. Three bespoke keys across
122 cases, with no stated rule, is how a gate ends up guessing. Two things were
wrong and are corrected in `index.json`:

- Its `$comment` now states the invariant the comparator depends on: `verdict` is
  what an engine MUST report in its **default configuration**, for every case
  without exception, and the other `*Verdict` keys are alternate readings that are
  deliberately not the default.
- The note on `malicious/layout-efs-output-bomb.eclayout` claimed "the stricter
  one is recorded here", while the fields record the lenient reading as `verdict`
  and the strict one as `stepTenVerdict`. The note described the opposite of the
  data. It now describes the fields, and says which reading becomes `verdict` is
  OQ-002's to settle. **No verdict was changed** — changing one would have
  resolved an open specification conflict by fiat, which §0.1 rule 2 forbids.

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
- **Half of this is now done.** `tools/check-dependency-denylist.py` exists,
  passes over the real tree, and is pinned by a committed 72-name corpus
  (`--self-test`) so its matcher cannot rot. What is still missing is the wiring:
  nothing runs it automatically. `CONTRIBUTING.md` lists it under "enforced by:
  nothing yet — run it", which is the honest state and not a substitute for the
  workflow. This entry closes when `security.yml` runs it, not before.
- **It scans 198 dependencies, not 27.** `package-lock.json` is included, so the
  171 transitive npm packages behind three direct devDependencies are checked
  too. A telemetry SDK arriving as somebody else's dependency is exactly how this
  requirement gets violated without anyone editing a manifest.

### OQ-026 — CI installs dependencies with `apt`, so `REQ-BLD-022` has nothing to cache · **Gap**

§6.2 says every non-Qt dependency comes from vcpkg in manifest mode, and
`REQ-BLD-022` makes vcpkg binary caching **mandatory** rather than optional.
`desktop-ci.yml` installs FFmpeg, TagLib, ALSA and the rest with `apt-get`. That
is fast and it is why the workflow is currently green, but it means the manifest
this repository commits is not the thing CI builds against, and a mandatory cache
has nothing to cache.

- **Assumption in force:** the `apt` matrix stays as the fast pre-check, and a
  separate vcpkg job builds the manifest with binary caching. Two lanes rather
  than one, because replacing `apt` outright would add tens of minutes to the
  feedback loop that catches most breakage.
- **Consequence if it stays unfixed:** the pinned baseline in `vcpkg.json` is
  never exercised, so it can rot without anything going red — which is exactly
  the failure `REQ-SEC-013` (no floating versions) is trying to prevent.

### OQ-030 — A sixth workflow file, outside the §5 layout · **Settled**

§5 lists five workflows and `REQ-GEN-030` requires the repository to match that
layout. `.github/workflows/repo-lint.yml` is a sixth, so the deviation is recorded
here rather than left for someone to find.

- **Assumption in force:** `REQ-GEN-030` requires every listed file to exist; it
  does not forbid a file it does not list. Nothing in §5 is missing because of
  this, and no listed workflow has been merged into another.
- **Why not a job in `desktop-ci.yml`:** its `paths` filter is `['desktop/**',
  'shared-spec/**', 'tools/**', '.github/workflows/desktop-ci.yml']`. Markdown
  style and commit-message form would then be unchecked on documentation-only
  pull requests — the exact changes those gates govern. Widening the filter
  instead would run the Windows and Ubuntu build matrix on every typo fix.
- **Why not `spec-ci.yml`:** it is scoped to `shared-spec/` and has the same
  problem in the other direction.
- **What it carries:** `markdownlint-cli2` and `commitlint`, both pinned exactly
  in `package.json` and installed with `npm ci` from the committed lockfile.
  `tools/check-doc-links.py` joins it in the commit that adds the seven missing
  §27 documents; it is left out for now because it fails for a real reason, and a
  gate introduced red is a gate somebody weakens instead of satisfying.
- **The pinning is the point, not incidental.** `npx <tool>` resolves whatever the
  registry serves that day, which is how the Markdown gate came to run a version
  nobody chose (OQ-028). `REQ-SEC-013` is about tooling not moving under the
  project, and it applies to a linter as much as to a library.

### OQ-016 — One commit predates `commitlint.config.js` and fails it · **Settled**

`bf91096` ("Initial commit") is not a conventional commit. `commitlint` lints the
commits **in the range under review**, not all of history, so this is not a
permanent red gate.

- **Assumption in force:** history is not rewritten to satisfy a rule added later.
  Rewriting `main` would invalidate every commit hash quoted in an ADR.
- **Note:** the rule was applied to my own work retroactively where it was still
  cheap. Commit `e151ebe` originally had an 81-character subject, over the
  72-character limit the config sets; it was amended down to 64 rather than the
  limit being raised to accommodate it.

**Two corrections of record.** This entry previously said *two* commits fail, and
that CI lints the range under review. Both were wrong, and both were checkable at
the time:

- `873e5be` was named as the second failure because it predates the config. It
  passes. Running `@commitlint/cli` 21.2.2 over the whole history gives exactly
  one failure — `bf91096`, on `subject-empty` and `type-empty` — and eleven
  passes. "Predates the config" is not the same claim as "fails the config", and
  writing the first while asserting the second is how a count nobody rechecks
  becomes part of the record.
- CI did not lint commit messages at all. There was no `commitlint` invocation in
  any workflow, so the statement described an intention. It is now true:
  `.github/workflows/repo-lint.yml` runs it over the pull-request range. The
  same audit found `markdownlint` unenforced by CI as well — the gate repaired in
  OQ-028 was being run by hand and by nothing else.

**Verified, both directions.** Five malformed messages are rejected with the rule
that catches each — a missing `Refs:` id on a `feat`, a scope outside §0.2, a type
outside the enum, an 84-character header, and a trailing full stop — and three
well-formed ones pass, including a `revert` carrying `Reverts <sha>` in place of a
requirement id.

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
| `.github/scripts/spec_full_validate.py --check-schemas --check-fixtures` | pass — 5 schemas valid, 91 fixtures match their claimed verdict |
| the same, with defects planted (`"type": 5`; a flipped verdict; an undeclared `$id`) | fails, as it must — 3 of 3 |
| `.github/scripts/compare_verdicts.py --self-test` | pass — 10 scenarios, 6 of which must fail and do |
| `tools/check-doc-links.py` | **fails** — 7 of the §27 documents do not exist yet |
| `tools/check-dependency-denylist.py --self-test` | pass — 24 denied, 48 allowed, 0 either way |

Toolchain in use: CMake 3.31.6, Ninja 1.12.1, GCC 14.2.0, and — in a user-local
`~/.local` prefix, no root required — FFmpeg 7.1.1 (LGPL-configured,
decode-oriented, `--disable-network`), TagLib 2.0.2, alsa-lib 1.2.12,
SQLite 3.46.1.

So `README.md`'s "184 tests, all passing" is locally reproduced, not merely
asserted by CI.

**Correction of record.** An earlier version of this section, and the table in
`shared-spec/README.md`, listed full draft-2020-12 validation as CI-only on the
grounds that `jsonschema` was unavailable here. It is available: the distribution
ships it as a system package (`jsonschema` 4.19.2, with `referencing` 0.36.2), so
no virtualenv and no `pip install` is needed and the constraint that produced the
claim never applied to this check. `.github/scripts/spec_full_validate.py` runs
locally and passes, and the row has moved above. The pinned install in
`.github/requirements-spec.txt` stays, because CI must not depend on whatever a
runner image happens to carry — and the version gap is useful in itself: 4.19.2
locally against 4.26.0 in CI means the schemas are asserted by two library
versions rather than one.

`tools/check-doc-links.py` is listed as failing rather than omitted. Seven §27
documents are genuinely missing; the gate says so, and it is not wired into a
workflow until they exist, because a gate added while red is a gate somebody will
be tempted to weaken.

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

The §4.2 register pins libsamplerate at **≥ 0.2.2** and notes separately that
releases before 0.1.9 were GPL. It is the one optional dependency
`EclipseDependencies.cmake` looks for that is absent locally, so
`ECLIPSE_HAVE_SAMPLERATE` is `OFF` and now says so in the configure summary.

- **Correction of record:** `EclipseDependencies.cmake` asked for `>= 0.1.9`,
  the licence floor, where the register says `>= 0.2.2`. Those are two different
  floors and only one of them is `REQ-GEN-012`. A 0.1.9 build would have been
  licence-clean and still absent from the register, which REQ-GEN-012 makes a
  build failure rather than a footnote. The check now requires `>= 0.2.2`; the
  vcpkg baseline resolves exactly 0.2.2.
- **Impact:** none yet. The resampler is Phase 6 work. Recorded so that when
  Phase 6 starts the missing dependency is a known item rather than a surprise.

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

- **Sub-question, now answered:** whether the configure summary should say more
  than a bare `OFF`. It should. A bare `OFF` is truthful and easy to misread as
  "this machine cannot build the adapter" when it means "pkg-config was not told
  where to look". The summary now names where dependencies came from (`system /
  pkg-config` or `vcpkg (<triplet>)`), lists `libsamplerate` alongside the other
  adapters instead of omitting it, and prints a three-line note pointing at
  `docs/BUILDING.md` whenever anything is `OFF`. It is a note rather than a
  `message(WARNING)` because building without the optional adapters is a
  supported configuration, not a mistake.

### OQ-027 — The vcpkg manifest is verified by resolution, not by a build · **Gap**

What was actually run against `desktop/vcpkg.json`, on this machine, with vcpkg
2026-07-27 at the pinned baseline:

| Check | Result |
|---|---|
| `vcpkg install --dry-run --triplet x64-linux-eclipse` | resolves; `ffmpeg` pins to 7.1.2#5, no OpenSSL, no bzip2 in the graph |
| triplet evaluation for 14 ports | linkage matches the §4.2 column exactly — LGPL dynamic, permissive static |
| `cmake --preset linux-release` with `VCPKG_ROOT` unset | unchanged; 184/184 tests still pass |

What was **not** run: an actual `vcpkg install`. No port was compiled, so the
manifest is proven to *resolve*, not proven to *build*. `arm64-linux-eclipse`
cannot resolve here at all (no cross toolchain), and both Windows triplets refuse
on a non-Windows host by design — so three of the four triplets in the §3.1
matrix have no local evidence beyond their syntax. See also `OQ-022` and
`OQ-023`.

### OQ-028 — The Markdown gate had never excluded anything it claimed to · **Settled**

`9e12695` reported "0 issues in 27 files" and added a `.markdownlintignore`
listing six exclusions. Both halves were wrong in the same way.

`markdownlint-cli2` does not read `.markdownlintignore` — that file belongs to
the v1 `markdownlint` CLI. cli2 takes its file set from `globs` and its
exclusions from `ignores`, both in `.markdownlint-cli2.jsonc`. With no such file
present, every exclusion in the repository was inert:

| Invocation | Files | Result |
|---|---|---|
| `markdownlint-cli2 "**/*.md"` | 109 | 2869 issues in 73 files — it walked `build/_deps/googletest-src/` |
| `git ls-files '*.md' \| xargs markdownlint-cli2` | 28 | 718 issues in 1 file — `eclipse-player.md`, the file the ignore list exists to protect |

So the green result came from an invocation that happened to name a file set
excluding the specification, not from an exclusion being honoured. The gate was
neither green nor red; it had no fixed subject.

- **Correction of record:** "27 files" was never the tracked-document count.
  There were 24 tracked `.md` files at `9e12695` and 22 of them lintable, so the
  figure did not correspond to any set the repository defines. It is now 26 of the
  28 tracked documents — `eclipse-player.md` and `CHANGELOG.md` are the two live
  exclusions, `docs/THIRD-PARTY.md` is pre-listed against the generator that will
  produce it — and all of them are visible in the linter's own `Finding:` line.
- **Fixed here:** `.markdownlintignore` is deleted and `.markdownlint-cli2.jsonc`
  carries `globs` plus `ignores`. Rules stay in `.markdownlint.json` so that
  editors and the v1 CLI keep reading the same rule set as the gate. Dot
  directories are walked by default, so `.github/**` needs no second glob.
- **One exclusion was too broad and is narrowed:** `desktop/third_party/` skipped
  `desktop/third_party/README.md`, which is ours and hand-written. The pattern is
  now `desktop/third_party/*/**` — vendored subdirectories are skipped, the
  README is linted.
- **The invocation is now part of the gate, not a detail of it.** `CONTRIBUTING.md`
  states that the command is `npx markdownlint-cli2` with **no arguments**,
  because arguments are exactly how the wrong file set got linted.
- **Verified, both directions:** 0 issues over 26 files; and an over-long line
  appended to `docs/PRIVACY.md` is reported as
  `MD013/line-length … Expected: 88; Actual: 125`, so the clean result is the
  linter working rather than the linter idle. A scratch file at 85 columns also
  confirms `.markdownlint.json` is the rule source in effect — default
  markdownlint flags it at 80, this configuration does not.

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

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

A status may be **qualified** where the bare word would overstate it —
`**Settled, narrowly**` on OQ-003 says the decision holds for the case in front of
it and not the general question. What a status may **not** do is describe how good
the evidence is: `**Measured**` sat on OQ-046 for a while and read as a fourth
status, when the entry was in fact still Open and merely better evidenced. Evidence
belongs in the body; the marker is the decision state, and
`tools/check-doc-links.py` now rejects a first word that is not one of the three.

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
`desktop/vcpkg.json` against the pinned baseline for `x64-linux-arrow` yields
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

---

### OQ-039 — `window.*` actions have no §13.2 command · **Open**

`layout.schema.json` admits three window actions — `window.close`,
`window.minimize`, `window.maximizeRestore`. `REQ-THM-028` says the action set
"mirrors the command registry from §13.2", and §13.2 as excerpted enumerates no
window command: §13.3's default-shortcut table lists only
`view.toggleWindowshade` and `playlist.closeTab` in that space. Read literally, an
action with no registry entry is not a valid action.

- **Assumption in force:** the three are legitimate registry commands and validate
  today. `REQ-THM-032` counts `close` among the controls a skin "must never be able
  to remove", which means a skin must be able to *invoke* it; a close button bound
  to `window.close` is accepted by `layout.schema.json` as it stands, and
  `docs/SKIN-AUTHORING.md` documents them as valid because the schema is the
  artefact that decides what validates (`REQ-THM-010`).
- **Proposed answer:** add the three to the §13.2 registry explicitly. They are
  window-management commands the registry simply never enumerated, and
  `REQ-THM-032` already requires the application to keep `close` reachable, so the
  command must exist somewhere regardless. Then `REQ-THM-028`'s "mirrors §13.2"
  holds by construction rather than by this note.
- **Consequence if unfixed:** the schema and the prose disagree about whether a
  skin may bind `window.close`. Both engines follow the schema, so a skin using it
  validates and works — the discrepancy is invisible until someone reads
  `REQ-THM-028` literally and removes the actions from the enum, which would
  silently break every skin that placed a working close button. That is the exact
  "unclosable player" failure `REQ-THM-032` exists to prevent, arrived at from the
  opposite direction.
- **Correction of record.** The schema's own `action` description has asserted since
  the `shared-spec` commit that this gap "is flagged in docs/OPEN-QUESTIONS.md".
  It was not: no entry mentioned `window.*` until this one. The assertion was
  written in the same commit as the enum and pointed at an entry that was never
  added. This entry makes it true rather than editing the schema to make the claim
  disappear.

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

### OQ-037 — the `REQ-GEN-012` completeness gate is one-directional · **Settled**

`REQ-GEN-012`'s gate fails when a component in the build is absent from the §4.2
register. `tools/gen-third-party/gen-third-party.py` implements exactly that
direction and not the reverse, so the register is permitted to list more than the
build links.

- **Assumption in force:** over-listing is a documentation problem, not a licence
  one, and is therefore not a build failure. Nothing can ship under a licence the
  register omits, which is the property the requirement protects. Two rows rely on
  this today: §4.2 lists `nlohmann/json` while the tree uses its own MPL-2.0
  parser in `desktop/src/core/json/`, and it offers `libzip` **or**
  `minizip-ng` while the manifest picks one.
- **Why not make it bidirectional:** Qt is acquired with aqtinstall and is
  deliberately absent from `desktop/vcpkg.json`, so a symmetric gate would fail
  on the one dependency §4.3 cares most about. It would turn a legitimate
  superset into a red build.
- **Consequence:** a reader who takes the register as an inventory of what ships
  is misled in the harmless direction. The generated document carries a
  per-entry note where an entry is registered but not linked, so the status is
  visible rather than inferred.
- **Proposed answer:** the generator grows a non-failing lint that names any
  desktop entry which is neither in the manifest nor flagged as externally
  acquired — register staleness reported without blocking a build.

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
check in `security.yml`. `security.yml` did not exist when this entry was
written; see the last bullet for what has changed and what has not.

- **Assumption in force:** the property currently holds because there are no
  network dependencies at all, which is true but proves nothing about the future.
  `docs/PRIVACY.md` says so in its own words.
- **Blocking:** `security.yml` is Phase 0 scaffolding and is next in sequence.
  *(Since written: it is written. What blocks now is a run, not a file.)*
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
- **The wiring landed; the entry still does not close.**
  `.github/workflows/security.yml` runs `check-dependency-denylist.py` in its
  `dependencies` job — unconditionally, blocking, ahead of the licence audit — and
  again with `--resolved-graph` in the `resolved-graph` job, which is where the
  transitive vcpkg ports this gate cannot otherwise see come from. So
  `CONTRIBUTING.md`'s "enforced by: nothing yet" is no longer true of the
  repository. It is still true of every *run*: nothing has been pushed to `origin`,
  so the file exists and has never executed, and the condition above says "when
  `security.yml` runs it, not before". A workflow file is not a run. This entry
  closes with the first green `dependencies` job.

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
- **What landed.** `desktop-ci.yml` now has that second lane: a `vcpkg` job that
  clones vcpkg at the baseline **read out of the manifest** rather than a hash
  copied into the workflow, installs in manifest mode with the overlay triplet,
  and persists a binary cache. It does not run on pull requests, for the reason
  above. It also asserts two things the manifest claims: that no Qt port is
  installed (§6.2, ADR 0005) and that `ffmpeg` resolves to the overridden 7.1.x
  rather than the baseline's newer default.
- **`x-gha` is gone, and this nearly shipped using it.** The first draft
  configured `VCPKG_BINARY_SOURCES=clear;x-gha,readwrite`, which is what
  essentially every published recipe still shows. The baseline
  `9e593bb18ea69cc5095e012465dcd675a822ed0d` resolves vcpkg-tool release
  `2026-07-27`, and that release's own message catalogue contains
  `GhaBinaryCacheDeprecated`: *"The 'x-gha' binary caching backend has been
  removed."* A removed backend does not fail loudly — it leaves the job compiling
  everything from source and reporting success, which is `REQ-BLD-022` satisfied
  on paper only. The job uses a `files` provider persisted by `actions/cache`
  instead, and its accounting step **fails** when the log contains neither
  `Restored N package(s)` nor `All packages already exist in the binary cache` —
  the only two things that tool release says when a provider is active.
- **Gate 5's Qt half is now written, and still unproven.** Phase 0 exit gate 5
  reads "vcpkg binary caching **and Qt caching** demonstrably working". The Qt
  lane now exists in `desktop-ci.yml` (and in `release.yml`'s `artifacts` job):
  a `actions/cache` step keyed on `qt-version.txt` + runner, an `aqtinstall`
  step that skips on a cache hit, and an unconditional `CMAKE_PREFIX_PATH`
  export so configure finds Qt on a warm run too. What gate 5 asks for is
  **evidence** — a warm run that is substantially faster than the cold one — and
  evidence requires a pushed run. The lane is written; the proof is CI's to give.
- **Closes when:** a second run of the `vcpkg` job on an unchanged manifest
  restores packages instead of building them — the job's summary states which
  kind of run it was — **and** a second Qt install on an unchanged
  `qt-version.txt` restores from cache instead of running `aqtinstall`. Neither
  has happened: nothing in this repository has ever been pushed.

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
  `tools/check-doc-links.py` joined it in the commit that completed the last
  missing `[v1.0]` §27 document (`docs/LGPL-SOURCE-OFFER.md`), together with
  `gen-third-party.py --check` for both generated licence documents — added green,
  which is the whole reason they were held back while red.
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

### OQ-031 — `REQ-GEN-050(1)` is enforced only for the domain layer · **Settled**

Rule 1 of `REQ-GEN-050` is the general statement — layer *N* may depend on layers
*< N* and must not depend on layers *> N*. `tools/check-layers.py` enforces the
domain slice of it thoroughly (rule 2's forbidden-include list) and the platform
and adapter-confinement rules completely, but there is no check that, say, a file
in `src/core/` does not include `app/session.hpp`. Today nothing can violate it,
because layers 4 and 5 have no files. That is the reason it has not gone red, not
a reason it is enforced.

- **Proposed answer:** add a directory-to-layer map and a downward-only include
  assertion to `check-layers.py` in the same commit that creates
  `desktop/src/app/` — the first commit where the check has something to check,
  and the first where the mapping can be written from real directories rather
  than guessed at.
- **Why not now:** the map would have to invent layer assignments for
  directories that do not exist. A gate whose rule table is speculative is a gate
  that gets edited to match whatever the code does, which is the opposite of
  enforcement.
- **Interim:** `docs/ARCHITECTURE.md` states the rule's enforcement state as
  *partial* in its own table rather than claiming five-for-five.
- **Closed** by the commit that added the `layer ordering` check, exactly as
  proposed: the map was written from directories that exist, in the commit after
  the one that created `desktop/src/app/`. `LAYER_PREFIXES` maps five directory
  prefixes to layers and defaults everything else under `desktop/src/` to layer
  3, so a new *pure* directory needs no entry while a new **adapter** directory
  does — and forgetting one makes the adapter fail the moment it includes its
  port, which is a loud failure rather than a silent exemption.
- **What the closure found:** writing the map surfaced a real inversion in §7.1's
  numbering that the abstract rule had hidden. It is recorded separately as
  OQ-055, and the check states both directions of the port/adapter relationship
  outright rather than deriving them from the arithmetic.
- **What it is proven against:** 13 synthetic trees, 6 of them planted
  violations, plus a same-file flip test — one include reversed in direction must
  flip the verdict — because a rule table that classifies nothing passes every
  negative case without walking a file (the OQ-045 shape). It was also run
  against the real tree with one upward include planted in
  `desktop/src/app/application.hpp`, which it reported and located.
- **`src/main.cpp` is exempt, by name.** The composition root constructs layer 4
  and hands control to layer 5; something has to. The exemption is a named
  constant in the gate rather than a skip nobody would notice.

### OQ-032 — `REQ-GEN-054`'s doc-comment check does not exist · **Gap**

`REQ-GEN-054` requires two things beyond the one-public-header convention:
`docs/API.md` documents every public surface, and **a CI check verifies that every
public symbol has a doc-comment**. The second does not exist.

- **Assumption in force:** the convention holds by review, which is precisely the
  substitute `REQ-GEN-050` rejects for layering and there is no reason to think it
  is more reliable here.
- **Proposed answer:** the check belongs with `docs/API.md` itself — a script that
  walks the public headers named in that document, extracts declarations at
  namespace scope, and requires a `///` comment above each. It is not written in
  the same commit as the document because the document can be true today, while
  the check needs a declaration parser good enough not to produce false failures
  on templates and operator overloads; a noisy gate gets suppressed.
- **Consequence if it stays unfixed:** `docs/API.md` drifts. Every function added
  after it was written is a silent omission, and the failure mode is a document
  that looks complete.

### OQ-033 — `docs/API.md` is hand-written, not generated · **Gap**

The §27 document table describes the file as:

> `docs/API.md` | Every public module API, generated from doc-comments, with
> thread- and RT-safety noted per function

The document that exists satisfies the second half and not the first. It notes
thread- and RT-safety per function, and it was written by reading the headers
rather than by parsing them.

- **Assumption in force:** a hand-written document is acceptable as long as it
  is accurate on the day it is written. That is a weaker guarantee than the
  requirement asks for, and the deviation is stated in the document's own
  opening rather than left for a reader to discover.
- **Proposed answer:** the declaration parser proposed in OQ-032 grows an
  `--emit` mode, so the same parser both checks that every public
  symbol has a doc-comment and renders the tables from those comments. One
  parser, two outputs — a separate generator would be a second thing to keep
  in agreement with the checker. Until it exists, the doc-comments in the
  headers are the source of truth and the document is a rendering of them that
  a human maintains.
- **Consequence if it stays unfixed:** the same drift as OQ-032, one step
  further along. The check catches a symbol with no comment; nothing catches a
  comment whose wording no longer matches the table that was copied from it.

---

### OQ-034 — the RT-safety gate verifies claims but cannot find omissions · **Gap**

`REQ-AUD-017` asks for one specific check:

> Add a CI grep that flags calls from `audio/graph/rt_*` into functions lacking
> the annotation.

The §25 gate table repeats it as a failure condition: *"A call from the RT path
targets a function lacking `/// RT-SAFE:`"*. `tools/check-rt-safety.py` does not
do this. It scans function bodies that already carry the annotation and flags
forbidden constructs inside them — allocation, locking, throwing, I/O, container
growth. That verifies every claim made. It cannot detect a claim that was never
made, which is the direction the requirement names.

- **Assumption in force:** the annotated set is the RT-callable set, held true
  by review. The gate confirms that what is annotated is safe; nothing confirms
  that what is called is annotated.
- **Proposed answer:** implement it in the commit that creates
  `audio/graph/rt_*`, for the same reason
  OQ-031 defers the layer-ordering rule — the check needs call sites to check, and
  `audio/graph/` has no files today. Writing it now would mean writing a gate
  whose entire coverage is the empty set and which therefore passes for the
  wrong reason.
- **Consequence if it stays unfixed:** the annotation degrades into
  documentation. A function reached from the callback without one is exactly the
  case `REQ-AUD-017` exists to prevent, and it is the case nothing looks for.

---

### OQ-035 — `reset()` is unannotated and a seek runs on the audio thread · **Gap**

`Biquad::reset()`, `BiquadCascade::reset()` and `Equalizer::reset()` carry no
`/// RT-SAFE:` annotation. Their processing counterparts do. A seek must clear
the filter delay lines — otherwise the state from the old position rings into
the new one — and a seek is serviced on the audio thread. Under `REQ-AUD-017`
that is a function the callback must not call, and the seek path will need to
call something equivalent.

- **Assumption in force:** unresolved, because there is no seek path yet to
  resolve it against. `Equalizer::reset()` zeroes fixed-size arrays already
  owned by the object, so it would qualify for the annotation on inspection —
  but `REQ-AUD-017` rules on the annotation, not on inspection, and
  `docs/API.md` reports the absence rather than assuming the permission.
- **Proposed answer:** the commit that writes the seek path decides, and one of
  two ways. Either annotate the three functions, stating in the comment that
  they touch only preallocated state — the honest reading if the audio thread
  is to call them directly. Or route the clear through the `REQ-AUD-016`
  parameter-snapshot mechanism, so the RT thread publishes a generation counter
  and the filters observe it, which keeps the RT-callable surface smaller at the
  cost of one more piece of protocol.
- **Consequence if it stays unfixed:** either a seek leaves stale filter state
  audible at the new position, or the audio thread calls an unannotated function
  and the annotation's meaning erodes for every future reader.

---

### OQ-036 — `REQ-GEN-019` names a source that does not hold the texts · **Gap**

`REQ-GEN-019` requires the Help → Third-Party Licences screen to show each
bundled component's **full licence text**, and requires that screen to be
"generated from `docs/THIRD-PARTY.md` at build time — never hand-maintained in
two places". The generated document carries SPDX ids, canonical references and
source URLs, not verbatim texts, so the instruction cannot be followed as
written: the texts are not in the file it names.

- **Assumption in force:** the screen is generated from
  `tools/gen-third-party/register.json` together with the licence texts vcpkg
  materialises at `vcpkg_installed/<triplet>/share/<port>/copyright`, plus Qt's
  `licenses/LGPL-3.0.txt` from `REQ-GEN-013`. `docs/THIRD-PARTY.md` is the human
  rendering of the same register, not the machine input. The property the
  requirement protects — one source of truth for what ships under what licence —
  holds, because both outputs derive from the register.
- **Why the texts are not embedded:** the build machine has no network, the
  bodies run to thousands of words each, and vcpkg already writes the exact text
  that was compiled against. A copy in Markdown would be a second thing to keep
  in agreement with the artefact that actually ships.
- **Consequence if it stays unfixed:** someone implements the screen by parsing
  `docs/THIRD-PARTY.md` literally and ships a Licences screen with no licence
  texts in it, which breaks `REQ-GEN-019` and, for Qt, `REQ-GEN-013`.
- **Proposed answer:** the requirement should name the register, of which
  `docs/THIRD-PARTY.md` is one rendering, and the commit that builds the screen
  should point its generator at the register and the copyright files.

---

### OQ-038 — the transitive gate's parser has never seen real vcpkg output · **Gap**

`gen-third-party.py --resolved-graph` is the interface OQ-025 asks for: it takes
`vcpkg install --dry-run` output and fails on any resolved port that no register
entry, transitive reference or build-only entry describes. Its parser was written
against the documented line format and is exercised only against a hand-built
fixture in `tools/gen-third-party/testdata/`, because vcpkg is not installed here.

- **Assumption in force:** the documented package-line forms
  (`name[features]:triplet@version#portversion`, and the older `-> version`) match
  what the pinned vcpkg prints. The generator says so in its own `--self-test`
  output and in the document, and the committed document is generated in
  direct-only mode so that it claims nothing this machine could not check.
- **Consequence if it stays unfixed:** the dangerous direction is silent
  under-reporting. A line the pattern does not match is skipped, a real component
  goes unseen, and a gate meant to catch the dependency nobody looked at passes
  while doing nothing — which is the failure mode OQ-025 describes.
- **Proposed answer:** the first CI lane with vcpkg available (OQ-026's caching
  lane, or `security.yml` from OQ-015) pipes a real dry-run through the generator,
  confirms the classification, and commits the captured output as the fixture in
  place of the synthetic one. Until then the parser stays labelled untested
  against real output wherever it is described.
- **That lane now exists, and is deliberately non-blocking.** `security.yml`'s
  `resolved-graph` job runs `vcpkg install --triplet x64-linux-arrow --dry-run`
  against the runner's own vcpkg — the manifest pins the registry with
  `builtin-baseline` and the custom triplet comes from `vcpkg-configuration.json`'s
  overlay, so what resolves there resolves anywhere — then pipes the output through
  the generator, the denylist and `gen-sbom.py`, and uploads it as an artifact. The
  **whole job** carries `continue-on-error: true`, for the reason OQ-044 sets out:
  the likeliest cause of a red first run is a defect in my parser rather than in the
  build, and failing the workflow over that puts pressure on the wrong file. The
  artifact is what closes this entry — real output replaces the synthetic fixture,
  `continue-on-error` comes off, and both happen in the commit that records the run.
- **The audit runs in audit mode, not `--check` mode, and the difference is not
  cosmetic.** `gen-third-party.py --check --resolved-graph <graph>` reports **STALE**
  by design: the committed `docs/THIRD-PARTY.md` is generated direct-only, so
  comparing it against a resolved graph *must* differ, and a CI step written that way
  would fail on every run for a reason that is not the requirement. The step passes
  `--resolved-graph` with `--output` to a scratch path instead, where a non-zero exit
  means an unregistered port and nothing else. Verified by planting one:
  `✗ unknown-telemetry-lib` — "These ports are in the resolved graph but described by
  no register entry, no transitive reference, and no build-only entry."

---

### OQ-040 — the desktop `theme-validate` engine does not exist · **Gap**

When this entry was written, `spec-ci.yml`'s `native` job built the CMake target
`theme-validate` unconditionally and ran the resulting binary over the 122-case
corpus to produce `desktop-verdicts.json`. Neither existed. `tools/theme-validate/`
is an empty directory, `desktop/src/theme/` is an empty directory, no
`CMakeLists.txt` under `tools/` is added by `desktop/CMakeLists.txt`, and so the
target could not be built. The job's own path filters already watch
`tools/theme-validate/**`, which is how the gap survived review: the workflow read
as though the tool were there.

The job has since been rewritten and no longer misrepresents anything — see the
last three bullets. What is left is the engine itself, which is why this entry is
now a **Gap** rather than a question: it closes when Phase 5 delivers the CLI, not
before.

- **Assumption in force:** none, and that is the problem. This is not a documented
  narrowing — it is a job that fails on its build step, which `REQ-THM-060` and
  `REQ-THM-072` both depend on. It is recorded here rather than left to look like
  a passing gate, and `docs/SKIN-AUTHORING.md` says the same thing to authors.
- **Second defect in the same job — fixed.** The run step invoked
  `./desktop/build/linux-release/tools/theme-validate`, but
  `CMakePresets.json` sets `binaryDir` to `${sourceDir}/../build/${presetName}`,
  so binaries land in the repository-root `build/`, not `desktop/build/`. The path
  would have been wrong even once the target existed. It is no longer hard-coded:
  the step now searches `build/linux-release` for an executable named
  `theme-validate` and refuses to guess if it finds none or more than one. A path
  written in advance for a binary nobody has built is a guess, and guessing is
  what produced this defect.
- **Third defect, found while fixing the second.** The CMake project root is
  `desktop/`, but §27 puts `tools/theme-validate/` at the repository root —
  *outside* the source tree CMake configures. `cmake --build --preset
  linux-release --target theme-validate` therefore cannot reach the target at all
  unless `desktop/CMakeLists.txt` pulls it in with `add_subdirectory()` and an
  explicit binary directory, or `tools/theme-validate/` becomes its own top-level
  project with its own configure step. Whoever builds the CLI has to decide which;
  the error message on the zero-executables branch names this, so the discovery
  happens at the point of failure rather than after an hour of confusion.
- **Proposed answer:** build the CLI — a C++ target under `tools/theme-validate/`
  reusing `desktop/src/core/json/json.hpp` rather than introducing a second JSON
  parser, emitting one verdict per corpus case in the shape
  `.github/scripts/compare_verdicts.py` already reads, plus JSON Pointers per
  `REQ-THM-060`. Until it exists the honest alternative is to mark the job
  `continue-on-error` or remove it; leaving it as a step that cannot pass is worse
  than either, because a red gate nobody expects to be green stops being read.
- **Consequence if unfixed:** `REQ-GEN-031`'s desktop/Android verdict agreement has
  no desktop side to compare, so `compare_verdicts.py` — which is written and
  self-tested — has nothing real to consume, and the corpus's 122 pinned verdicts
  go unexercised by any engine.
- **Why it is not built now, and this is not a dodge.** §28 lists `tools/theme-validate`
  and "the full validation pipeline" under **Phase 5 — Skin engine**, and forbids
  starting a phase before the previous one's gates are green. The repository is in
  Phase 0. Building the CLI now would mean implementing `REQ-THM-040`'s ten-step
  pipeline — archive safety, contrast computation, SVG scrubbing — four phases
  early. The rule that forbids that is the same rule that keeps the rest of the
  sequencing honest, so it is followed here too.
- **What landed instead.** The `native` job asks the tree whether the engine is
  there, deciding on `tools/theme-validate/CMakeLists.txt` rather than on the
  directory — git cannot track an empty directory, so the directory is absent on a
  fresh checkout and merely empty locally, and the CMake entry point is the one
  test that is right in both states. Absent: it emits a `::warning::`, writes a job
  summary naming the 122 uncovered cases, `REQ-THM-060`, `REQ-THM-072` and this
  entry, uploads **no** artifact, and exits 0. Present: every build and run step
  becomes blocking with no edit to the workflow, so landing the CLI is what flips
  the gate. Sources under `tools/theme-validate/` with no `CMakeLists.txt` **fail**
  the job — half-landed is not absent, and would otherwise skip its own gate
  quietly.
- **Why no artifact is uploaded while absent.** A stub or empty
  `desktop-verdicts.json` would let `agreement` compare nothing and report
  agreement — the OQ-042 shape. So `native` publishes its state as a job output and
  `agreement` branches on it: present means download, compare, and honour
  `compare_verdicts.py`'s FATAL-on-zero-reports, which exists to catch broken
  artifact wiring; absent means report the gap and stop. The comparator's own
  `--self-test` runs in **both** states, since it is the only thing keeping that
  script honest across Phases 0–4, when it has no real input.
- **Closes when:** `tools/theme-validate` exists, is reachable from a preset build,
  and the `native` job runs the corpus through it — at which point the same
  workflow is already blocking and needs no change.

### OQ-041 — `REQ-GEN-020`'s website mirror has no website · **Gap**

`REQ-GEN-020` requires the source offer to be published as
`docs/LGPL-SOURCE-OFFER.md`, *“mirrored on the website”*. The document now exists
and is generated from the same register as `docs/THIRD-PARTY.md`. The website does
not: there is no domain — the same absence OQ-013 records from the security side —
and no GitHub Pages site, so the repository is the only publication point.

- **Assumption in force:** the repository copy *is* the published offer. The
  generated document states this in its own “What is not yet in place” table
  rather than implying a mirror that nobody can visit.
- **Why this is a recorded gap and not an emergency, stated carefully:** the
  substance of the obligation rests on the route the offer actually relies on —
  LGPL-2.1 §6(d) and GPL-3.0 §6(d), equivalent access to the source from the same
  place that serves the binary. That place is the release page, and a mirror adds
  redundancy, not permission. What is unmet is the specification's own MUST, which
  is reason enough to write it down.
- **Proposed answer:** publish `docs/` as a GitHub Pages site once the domain in
  OQ-013 exists, and add its URL to the `offer` block in
  `tools/gen-third-party/releases.json` so the generated page names both
  publication points. One data field, not a second copy of the text.
- **Consequence if unfixed:** the offer lives only where the code lives. If the
  repository moves or goes away, distributed binaries could outlive their offer —
  exactly the risk the three-year written-offer clause exists to cover, and
  exactly why OQ-013's missing contact channel and this entry are one problem seen
  from two directions.

### OQ-042 — The `REQ-GEN-015` licence gate had never executed · **Settled**

`desktop-ci.yml`'s FFmpeg licence step guarded its work on
`[ -f build/<preset>/tests/test_decode ]` with `working-directory: desktop`, which
resolves to `desktop/build/<preset>/…`. `CMakePresets.json` sets `binaryDir` to
`${sourceDir}/../build/${presetName}`, so binaries land in the repository-root
`build/` — the same mistake OQ-040 records in `spec-ci.yml`, from the same cause,
and now fixed in both places by discovering the binary instead of predicting it.
The condition was therefore false on every run since the step was written, and the
`else` branch printed *“gate not applicable”* and exited 0. A blocking licence gate
that had never once run, reporting a clean skip.

- **Correction of record.** `docs/TESTING.md` listed “FFmpeg licence assertion”
  among what `desktop-ci.yml` runs, and `docs/LGPL-SOURCE-OFFER.md` said
  `REQ-GEN-015` *“makes CI verify at build time … and fail the build otherwise”*.
  Both described a step that had never executed. The requirement says what CI
  *must* do; only the workflow says what it *does*, and a licence document is the
  worst place to blur the two.
- **What changed.** The build directory is no longer guessed. `ctest --preset` is
  asked whether any `FfmpegLicense.*` case is registered, and runs them if so.
  The three outcomes are kept distinct: cases registered → assert; no cases and
  no `src/audio/decode/ffmpeg_decoder.cpp` → an honest skip, because nothing in
  the tree links FFmpeg and `REQ-GEN-015` has nothing to assert about a library
  this build does not use; no cases **but** an adapter present → **fail**, because
  that is precisely the shape of the bug this step used to have.
- **Second defect in the same job.** The failure-artifact upload pointed at
  `desktop/build/<preset>/Testing/` and would have uploaded an empty artifact for
  the same reason. Fixed alongside.
- **Verified locally, all three branches.** Against `build/linux-release`: a
  control filter that does match reports 14 registered tests, so the counting is
  real rather than always-zero; `^FfmpegLicense\.` reports 0; with
  `PKG_CONFIG_PATH` set the step takes the “FFmpeg present, nothing links it yet”
  notice and exits 0; and with a planted
  `src/audio/decode/ffmpeg_decoder.cpp` it exits 1 with the error annotation.
- **What is still not proven, and cannot be yet.** No FFmpeg is linked by anything
  in the tree, so `avutil_license()` has not been called in CI or here. Phase 0's
  exit gates do not include it — it belongs to Phase 1 (§28) — but the gate is now
  armed rather than decorative, and it will fail the moment an adapter lands
  without its assertion.

### OQ-043 — One of `REQ-SEC-011`'s seventeen fuzz targets exists · **Gap**

`REQ-SEC-011` names seventeen libFuzzer targets and requires each to have a
committed, growing corpus. `desktop/tests/fuzz/` now holds four harnesses, a
corpus of 49 committed seeds, and a replay driver that makes the corpus an
ordinary CTest case. Only **one** of the four — `fuzz_xinglame` — is one of the
seventeen.

The reason is structural rather than a decision about effort: a fuzz target needs
a parser to point at, and sixteen of the seventeen read formats this tree cannot
yet parse. ID3, Vorbis comments, APEv2 and MP4 atoms arrive with the Phase 2 tag
layer; cue sheets, playlists and smart rules with Phase 3; EFS with Phase 4; the
theme, layout and skin-archive readers with Phase 5; LRC with Phase 7; ICY and RSS
with Phase 8; IPC with Phase 9; the sync wire format with Phase 10. §28 forbids
starting a phase before the previous one's gates are green, so writing those
harnesses now would mean writing the parsers now.

- **Assumption in force:** `REQ-SEC-011` is read as a requirement on the shipped
  1.0.0 product, not on every intermediate commit — the same reading §28's phase
  ordering already forces for every other requirement whose subject does not exist
  yet. What is *not* assumed is that an empty file counts: no placeholder harness
  was created to make the count look better, because a target with no parser
  behind it reports success for the same reason a skipped gate does (OQ-042).
- **Three supporting targets are not counted among the seventeen.** The test they
  had to pass is that the parser exists and untrusted bytes reach it today — a
  shipped parser with no fuzz coverage is the gap that matters, whatever the spec's
  list happens to name. `fuzz_json` and `fuzz_text` are underneath the others:
  `fuzz_theme` and `fuzz_layout` both feed bytes through `core/json` before a
  single schema keyword is consulted, and all seventeen reach `core/text` the
  moment a tag value or file name becomes a `std::string`. `fuzz_gapless` covers
  the three parsers in `audio/decode/gapless_info.hpp` that `fuzz_xinglame` does
  not reach — `parse_itunsmpb`, which `REQ-AUD-042` calls a fuzz target in as many
  words, `parse_opus_head`, and `gapless_from_granule` — all of which are written,
  shipped in the domain library, and read bytes out of a downloaded file. All three
  supporting targets are listed separately in `desktop/tests/fuzz/README.md` and
  none is described as standing in for a named target.
- **`fuzz_mp4atoms` is still absent, and `fuzz_gapless` does not make it present.**
  `parse_itunsmpb` takes the tag *value*; `fuzz_mp4atoms` is about the atom *tree*
  that produces it, and no container parser exists. Naming this target
  `fuzz_mp4atoms` would have made the ledger read one row better and told a
  reviewer that atom parsing is fuzzed, which it is not.
- **Proposed answer:** the phase that introduces a parser introduces its fuzz
  target in the same commit, and `desktop/tests/fuzz/README.md`'s table is the
  ledger — every absent target names the phase that will bring it. The table is
  the mechanism: it converts sixteen silent omissions into sixteen rows a
  reviewer can count. This entry closes when the last row does.
- **§25.4's 15-minute lane now exists, one matrix entry per target.**
  `security.yml`'s `fuzz` job runs each of the four harnesses with
  `-max_total_time=900`, persists the corpus across runs through `actions/cache`
  under a run-numbered key with `restore-keys` walking back — a fixed key would
  freeze the corpus at whatever the first run produced, because cache entries are
  immutable — and fails on any reproducer, uploading it for 90 days. A matrix entry
  per target is what makes "15 minutes per target" literal: libFuzzer has no
  mechanism for dividing one invocation's time evenly across targets, so a single
  60-minute job would satisfy the sentence only by coincidence.
- **The job refuses to run rather than replay the corpus and call it fuzzing.**
  Without clang, `arrow_add_fuzz_target` builds only `<name>_replay`, which walks
  the committed seeds and exits in seconds. A step that ran *that* for fifteen
  minutes of wall-clock would report extended fuzzing and perform none of it —
  OQ-042's shape once more — so the job installs clang, then checks the real target
  is executable and errors out naming this file if it is not.
- **Each new corpus found a real defect on its first replay,** which is the
  argument for doing this now rather than at the end. Neither needed a mutation,
  and neither needed libFuzzer.
  - `fuzz_text`: `normalize_relative_path()` returned `true` for
    `/absolute/path`, quietly relativising it, and for a name containing a NUL —
    both classes `REQ-THM-018` requires be rejected, and both already rejected by
    `is_unsafe_relative_path()`. Two functions, one requirement, two different
    answers. The normaliser now refuses what the security check refuses and
    asserts that postcondition on its own output;
    `Normalize.RefusesWhatTheSecurityCheckRefuses` and
    `Normalize.AcceptanceImpliesSafety` pin it.
  - `fuzz_gapless`: `gapless_from_granule()` computed `-initial_granule` on an
    `std::int64_t` read out of an Ogg page. UBSan reported *negation of
    -9223372036854775808 cannot be represented in type 'long int'*. The 32-bit
    bound a line below would have rejected the value — but only after the program
    had executed undefined behaviour to reach it, and an optimiser is entitled to
    assume that never happens. The negation now runs in the unsigned domain, exact
    for every input including that one;
    `GranuleGapless.RejectsMostNegativeInitialGranuleWithoutOverflowing` pins it.
- **What is not proven here.** No clang is installed on the development machine,
  so `-fsanitize=fuzzer` is unavailable and `ARROW_HAVE_LIBFUZZER` is false in
  every local configuration. The four harnesses have been compiled and run by
  GCC 14.2.0 under ASan+UBSan via the replay driver, and the CMake branch that
  builds them without GoogleTest was exercised through the `linux-fuzz` preset —
  but the libFuzzer binaries themselves, and `arrow-domain-fuzz`'s
  `-fsanitize=fuzzer-no-link` instrumentation, have never been built. Those paths
  are **CI-only** until a clang toolchain is available here, and the 60-second
  smoke job in `desktop-ci.yml` is the first thing that will exercise them.
- **Consequence if unfixed:** `REQ-SEC-012` makes any fuzz finding a release
  blocker. Sixteen formats with no target cannot produce a finding, so the
  release gate would pass on silence rather than on evidence — which is exactly
  the failure this entry exists to keep visible.

### OQ-044 — `REQ-SEC-018`'s Windows half has never been checked against a real binary · **Gap**

`tools/check-hardening.py` reads PE headers as well as ELF: `DllCharacteristics`
for `DYNAMICBASE`, `NXCOMPAT`, `HIGHENTROPYVA` and `GUARD:CF`, the load-config
directory's `SecurityCookie` for `/GS`, and the extended-DLL-characteristics debug
entry for `/CETCOMPAT`. Every one of those paths is exercised only against
synthetic binaries built inside `--self-test`. No MSVC-produced PE has ever been
read by it, here or in CI, because nothing has been pushed.

- **Assumption in force:** that `arrow_set_hardening()`'s MSVC branch produces
  binaries carrying those bits — plausible, since `/DYNAMICBASE`, `/NXCOMPAT` and
  `/HIGHENTROPYVA` are MSVC defaults for x64 and the other three are passed
  explicitly, but assumed rather than observed.
- **Why the step is non-blocking, and why that is not the usual excuse.** A gate
  whose first run is red gets weakened rather than satisfied, and the most likely
  cause of a red first run here is a defect in my PE reader — not in the build.
  Failing the Windows matrix entry over that would put pressure on the wrong file.
  So the step runs with `continue-on-error: true` on the Windows entry only.
- **The condition for removing it is specific, not "when convenient":** one green
  run of that step on `windows-2022 · msvc`. At that point `continue-on-error`
  comes off in the same commit that records the run, and this entry closes. If the
  first run is red, the finding goes here before anything is changed, so it is
  visible whether the fault was in the reader or in the flags.
- **Consequence if unfixed:** `REQ-SEC-018` names six Windows mitigations. Until
  that step blocks, they are verified on one platform out of two, and the spec
  requires both.

### OQ-045 — Four gate scripts had no negative test · **Settled**

`check-layers.py`, `check-sql-safety.py`, `check-rt-safety.py` and
`validate-shared-spec.py` each reported "no violations" over a tree that contains
none, and that sentence is indistinguishable from what a script with an inverted
condition, an unanchored pattern or an empty file list would print.
`check-sql-safety.py` was the clearest case: there is no SQL anywhere in the tree,
so its pass had never once depended on its matching logic being correct.

- **Correction of record.** `README.md` said of the three source-level gates
  "Each has a negative test proving it catches real violations." None of them had
  one. The claim is now true, but it was written before the tests existed, and
  this entry keeps that visible rather than letting the sentence quietly become
  correct.
- **Resolved:** all four grew a `--self-test`, and they run in CI *before* the
  gates they belong to — the instruments are checked, then the measurement is
  taken. `desktop-ci.yml`'s `gates` job runs the first three in one step;
  `spec-ci.yml` runs the fourth ahead of both validators. Every gate script under
  `tools/` now has one.
- **How each gets synthetic input, and why they differ.** The shape of the input
  follows from how the script reads the tree, so uniformity would have cost
  fidelity:

  | Script | Synthetic input | Assertions |
  |---|---|---|
  | `check-layers.py` | throwaway trees under `/tmp`; the four checks now take their roots as arguments | 29 trees, 16 planted violations |
  | `check-sql-safety.py` | a `scan_lines(name, lines)` core split out of `scan(path)` | 10 injection sites, 8 safe constructs, 1 pinned blind spot |
  | `check-rt-safety.py` | the same split, plus a direct assertion on the brace-matching span finder | 11 false RT-SAFE claims, 7 legitimate constructs, 2 spans |
  | `validate-shared-spec.py` | a copy of `shared-spec/` with one defect planted, re-run as a subprocess via `--spec-root` | 14 planted defects, each matched to its complaint, 1 control |

- **Both directions, deliberately.** A gate that flags everything is as useless as
  one that flags nothing, so the negatives are the cases most likely to be
  false-flagged: a header named `Queue.h` (not Qt), `" limit=" + n` in prose (not
  SQL), placement `new` (not an allocation), `androidx/` (not `android/`), ALSA
  inside `audio/sink/` where it belongs, and an exemption comment on the line
  above the one it exempts.
- **One blind spot is pinned rather than fixed.** `check-sql-safety.py` matches
  uppercase SQL keywords only, so lowercase SQL evades it. That is what buys
  silence on prose like `" limit=" + n`. The self-test asserts the lowercase case
  is *not* flagged, so making the matcher case-insensitive fails the test and
  forces the trade-off to be re-decided rather than discovered.
- **What is still not proven.** These tests exercise the checking logic, not the
  file discovery: `check-sql-safety.py` and `check-rt-safety.py` self-test their
  `scan_lines` core, so a bug in which paths `main()` walks would still pass. The
  three "N file(s) scanned" counts in their real output are the only evidence
  there, and `check-layers.py` — whose synthetic trees go through the real
  `iter_sources`/`includes_of` — is the only one of the three where discovery is
  covered.

---

### OQ-046 — A CVE scan of the SBOM reports clean and covers nothing · **Open**

`REQ-SEC-004` requires the dependency CVE scan to fail the build on any new
high-severity finding, and §25.4 step 2 is that scan. The generated SBOM carries a
purl for every component and no `cpe` for any of them, because the §4.2 register
records no CPE names and `gen-sbom.py` will not invent one.

This entry began as a reading of three scanners' source. It has since been
**measured** — which is why it carried the status `**Measured**` for a while, and
why it no longer does: measurement is evidence quality, and the status records
whether a maintainer has decided. Nobody has decided the three-part proposal at
the foot of this entry, so it is **Open**, with better evidence behind it than
most. grype 0.117.0 (release tarball, SHA-256 verified against the
published `checksums.txt`) runs as a static binary with no root, so it was
installed and run here against vulnerability database v6.1.9. Every number below
is from that run, over the committed `docs/sbom/arrow-player.cdx.json` and one
database, so the only variable is the flag.

| Run | Components read | Matches |
|---|---|---|
| `grype sbom:docs/sbom/arrow-player.cdx.json` | 23 | **0** — "No vulnerabilities found" |
| the same document with one `pkg:npm/lodash@4.17.15` component appended | 24 | **6**, all `javascript-matcher` / `exact-direct-match` |
| the same document, `--add-cpes-if-none` | 23 | **49** — 8 Critical, 27 High, 13 Medium, 1 Low; every one `stock-matcher` / `cpe-match` |
| `grype dir:desktop` | **1** | 0 |
| `grype dir:.` (excluding `node_modules/`, `build/`) | 23 | **1** High — and not a C/C++ dependency: a floating action tag, since pinned and gated (OQ-050) |

Read the first two rows together: the injected npm component is found by the same
binary, from the same file, through the same reader, so the zero on the row above
it is not grype failing to parse the document. It is `pkg:vcpkg` having no
ecosystem matcher — which is what the source said, now with a non-vacuous control
behind it rather than a citation.

- **The purl was corrected anyway, and it changed nothing.** An earlier draft of
  `gen-sbom.py` emitted `pkg:generic` for the vcpkg ports on the belief that purl
  has no `vcpkg` type. It does — a registered type with a full machine-readable
  definition — so the ports are now `pkg:vcpkg/<port>`, qualified by
  `repository_revision` (the pinned registry baseline) and `triplet`. That was done
  because it is the correct identifier, **not** because it improves coverage: the
  0-match row above is the corrected purl.
- **Why this matters more than a missing feature.** A CVE step that scans this
  document reports clean and covers nothing — the same shape as OQ-042, where a
  licence gate took the "not applicable" branch on every run from the day it was
  written. A green check here would be a claim nobody had earned. The measurement
  is what makes that concrete: 0 and 49 out of the same document.
- **One part of the earlier proposal was wrong, and the measurement is what
  showed it.** This entry previously proposed scanning "the source tree and the
  vcpkg manifest, not only the SBOM, so each tool's native detection path is used
  instead of a purl lookup that is known to miss". For grype that is **backwards**:
  `grype dir:desktop` catalogues exactly one component,
  `pkg:vcpkg/arrow-player@0.1.0` — the project itself, read out of
  `desktop/vcpkg.json`, with none of its dependencies. The native path covers
  *less* than the SBOM, not more. The scan of the whole repository catalogues 23
  components and not one of them is a registered C/C++ dependency: nine GitHub
  Actions, five pinned Python packages from `.github/requirements-spec.txt`, the
  npm tooling root, and the project. Scanning both is still worth doing — that is
  how the one real finding surfaced — but not for the reason stated here before.
- **`--add-cpes-if-none` cannot gate the build, and the numbers say why.** Of its
  49 matches, **20 are against a component with no version at all** — `libzip`,
  `pkgconf`, `taglib`, `zlib`, the four whose register entry records a series
  rather than a release (OQ-047). A CPE match with no version is every CVE ever
  filed against the name: the list includes `CVE-2005-1849` against zlib and
  `CVE-2017-12858` against libzip, both fixed years before the pinned versions.
  Synthesised CPEs are therefore advisory output, never a gate — which is what
  this entry already said, now with a count.
- **The 29 ffmpeg matches are the half worth reading, and they raise a separate
  question.** ffmpeg is the one component with an exact version (7.1.2), so its
  matches are version-aware: 21 are High or Critical and 5 name a fix version
  (8.0, 8.1, 8.1.2). Whether any applies to *this* build is unassessed — the
  configuration is decode-only with no encoders, muxers, avfilter, swscale or
  network — and the assessment, not the count, is what a usable gate needs. The
  immediate consequence for `REQ-SEC-004` is recorded as
  OQ-049.
- **Proposed answer.** (1) `security.yml` scans both the SBOM and the repository
  tree, because they catalogue disjoint sets and the tree scan is where the one
  real finding came from. (2) grype additionally runs with `--add-cpes-if-none`,
  its output labelled advisory in the job summary and unable to fail the job. (3)
  The residual gap — no ecosystem matcher for `pkg:vcpkg`, therefore no coverage
  of the 13 registered C/C++ dependencies from the SBOM alone — is printed in the
  job summary on every run rather than hidden behind a green check. Until the
  register carries CPE names, or vcpkg lands in a scanner's ecosystem map,
  `REQ-SEC-004` is **not** satisfiable from the SBOM, and the workflow says so.
- **Wired in the shape this entry describes.** `security.yml`'s `cve` job installs
  grype 0.117.0 by digest rather than through an action, so the pin is auditable the
  same way §25.2 audits actions; runs `check-cve-baseline.py --self-test` *before*
  the gate, so a green verdict is distinguishable from a matcher that never matches
  (OQ-045); gates on both scans together; runs `--add-cpes-if-none` in a step that
  `continue-on-error` makes structurally unable to fail the job; and prints the
  `pkg:vcpkg` coverage gap with `if: always()`, green run or red. What stays
  undecided is the part that needs a decision rather than a file: whether the §4.2
  register grows CPE names for its thirteen C and C++ entries. Until it does, this
  entry stays **Open** however much of the scan is built.
- **What is still not proven.** osv-scanner and trivy have not been run — the rows
  describing them in earlier drafts of this entry were read from their source and
  issue trackers, and only grype has been executed. The grype numbers are from one
  machine, one database build (v6.1.9, 2026-08-26) and one architecture; a
  database rebuild changes the 49 and possibly the 21. Nothing here has run in CI,
  because no workflow in this repository has ever executed.

---

### OQ-047 — Ten of thirteen registered versions are a series, so the SBOM omits `version` · **Open**

CycloneDX's `component.version` is a version, not prose. The §4.2 register spells
ten of its thirteen desktop entries as a series or a bound — `2.x`, `3.4x`,
`≥ 0.2.2`, `current` — and only qt6 (6.8.2), ffmpeg (7.1.2) and gtest (1.15.2) as a
release. So those ten components carry **no `version` field at all**; the raw
register string is preserved as `arrow:register-version` and
`arrow:version-precision` says `series`.

- **Why the series is not simply put in `version`.** A consumer diffing two SBOMs,
  or matching a version range, would treat `2.x` as a literal version and match
  nothing while appearing to have matched something. An absent field is a fact a
  consumer can act on; a fake one is not.
- **The gate that says it out loud.** `gen-sbom.py --require-exact-versions` fails
  today and names all ten, and for each says whether a resolved graph could supply
  the version or whether only the register can. That distinction is not cosmetic:
  nine of the ten have a vcpkg port and a resolved graph fixes them, while
  nlohmann/json has no port at all — it is registered but not linked, since
  `desktop/src/core/json/` is a bespoke hardened parser (ADR 0008) — so no graph
  will ever pin it.
- **Tied to OQ-026.** The resolved graph comes from `vcpkg install --dry-run`, which
  needs vcpkg in CI — the same missing piece OQ-026 records. Until then the
  committed baseline is generated in direct-only mode.
- **Proposed answer.** The release SBOM (§25.5 step 6) is generated with
  `--resolved-graph --require-exact-versions`, so no release artifact ever ships a
  component whose version is unknown, while the committed baseline stays
  direct-only. Pinning the register to patch releases instead would be a
  maintenance lie the moment vcpkg's baseline moves, and it would make §4.2 claim
  precision about a version nobody re-checks.

---

### OQ-048 — Nothing committed validates the SBOM against the CycloneDX schema · **Settled**

`REQ-GEN-021` asks for a CycloneDX SBOM, which is worth having only if it is valid.
`gen-sbom.py`'s own `validate_bom` checks structure — required top-level fields,
unique bom-refs, the licence array's `oneOf` shape, each purl agreeing with its
component name, every dependency reference resolving — and is explicitly not a
JSON Schema validation.

- **What has actually been checked, and how.** Both the direct-only and the
  resolved-graph documents were validated against the canonical
  `bom-1.6.schema.json`, with CycloneDX's own `valid-bom-1.6.json` as a control and
  a planted `scope: "mandatory"` to prove the harness could fail: **0 errors** for
  each document, 0 for the control, and the planted defect caught. So the document
  is known to be valid.
- **But not by anything in this repository.** That run went through a throwaway
  40-line adapter under `/tmp` that is deliberately not committed.
  `tools/jsonschema_mini.py` implements a draft-2020-12 subset; the CycloneDX
  schemas are draft-07 and need three things it does not have: three unimplemented
  keywords, cross-file `$ref` into `spdx.schema.json` and `jsf-0.82.schema.json`,
  and the tuple form of `items` with `additionalItems`. The 166 `$ref` siblings in
  those schemas are all annotations, so the dialect difference itself is inert.
- **What landed.** `security.yml`'s `dependencies` job downloads
  `CycloneDX/cyclonedx-cli` v0.33.1 by digest and runs `validate --input-file
  docs/sbom/arrow-player.cdx.json --input-format json --input-version v1_6
  --fail-on-errors`. Both flags are load-bearing, for different reasons; see the
  next two bullets. Extending `tools/jsonschema_mini.py` to draft-07 is still worth
  doing on its own account — `validate-shared-spec.py` could then run real schemas
  locally rather than a subset — but it is no longer what `REQ-GEN-021` waits on.
- **`--fail-on-errors` is not optional, and measurement is what showed it.**
  Without the flag, `cyclonedx validate` prints `BOM is not valid.` and **exits 0**.
  The planted `scope: "mandatory"` document was correctly *reported* invalid and the
  process returned success. A step written the obvious way — no flag, trust the exit
  code — would have passed over an invalid document for the life of the project:
  OQ-042's shape arriving through somebody else's tool rather than through a branch
  in mine. With the flag, both directions hold: valid → 0, invalid → 1.
- **`--input-version v1_6` is pinned even though the default also passes.** The flag
  defaults to v1.7 and a 1.6 document validates under it today. Validating a document
  against a schema it does not declare is not a validation of that document, and the
  first 1.7 keyword that changes meaning would quietly move what is being checked.
- **Two claims above were wrong, and are corrected here rather than edited away.**
  *"It is not installed and cannot be without root"* — it is a single self-contained
  ELF executable, downloaded to a scratch directory and run as an ordinary user on
  this machine. *"Some Linux images additionally need `libicu`"* — this machine has
  **no** libicu at all (`ldconfig -p` finds zero entries) and the binary links only
  libc, libstdc++, libm, libgcc, libdl, librt and libpthread; it also runs identically
  under `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`. Both stay visible in this entry
  rather than moving to §29.6, which corrects claims made by the *specification*;
  these two were mine. §6 rule 3 below is amended to say which table takes which.
- **The embedded schemas are now measured, not inferred.** Validation was re-run with
  every egress route poisoned — `HTTP_PROXY`, `HTTPS_PROXY` and `ALL_PROXY` pointed at
  `127.0.0.1:1`, `no_proxy` and `NO_PROXY` cleared — and produced byte-identical
  verdicts: valid → 0 with *BOM validated successfully*, planted defect → 1 with
  *Validation failed*. bom-1.6, spdx and jsf-0.82 therefore come out of the
  executable, and the check works on a build machine with no network, which is what
  §25.4 needs of it.
- **What is still not proven.** CycloneDX publishes no checksums file for this
  release, so the pin `bfc8b253…` is a digest measured from one download on
  2026-08-26 rather than one the project vouches for — weaker evidence than grype's
  pin, and stated as such. And the step has never executed in CI, like every other
  step in this repository.

---

### OQ-049 — `REQ-SEC-004`'s "new" is undefined, and the gate is red today · **Settled**

`REQ-SEC-004` says the CVE scan fails the build "on any **new** high severity"
finding. The word *new* is load-bearing and the specification never defines it
against anything. The measurement in OQ-046 turns that from a pedantic reading
into a blocking one: grype, given the one component whose version is exact,
returns **21 High or Critical CPE matches against the pinned ffmpeg 7.1.2 right
now**, before a line of adapter code exists. A gate reading "fail on any high
severity" is red on its first run and every run after it.

- **Why the obvious readings both fail.** *New since the last run* makes the gate
  depend on scheduler history, so a re-run of the same commit can pass where the
  first run failed. *New since the previous release* is what §25.4 step 4 already
  uses for the SBOM diff, but there is no previous release, so on day one it
  degenerates to "any". Neither can be implemented as written today.
- **What the 21 are and are not.** They are CPE matches from NVD data against
  version 7.1.2. They are not an assessment: this build is decode-only, with no
  encoders, muxers, `avfilter`, `swscale` or network protocols (§6.3,
  `REQ-GEN-014`), and a large share of FFmpeg CVEs land in exactly those
  components. Five name a fix version — 8.0, 8.1 and 8.1.2 — against a pin the
  register and §6.3 both set at 7.1.x, so at least the question "should the pin
  move" is real and separate from "is this build affected".
- **Answer, and it is now built.** `security/cve-baseline.json` holds one entry
  per accepted finding — CVE id, component, the **exact** version it was accepted
  against, why it does not apply to this build, and the date, with an optional
  `expires`. `tools/check-cve-baseline.py` reads `grype -o json` and means *fail
  on any High or Critical finding absent from that file*. The definition depends
  only on the tree and the scan, never on run history, so re-running a commit
  cannot change the verdict; and accepting a finding is a reviewed diff that has
  to state a reason.
- **Three rules keep the baseline from rotting into a suppression list.** An entry
  matching nothing in the scan is an **error**, so upgrading past a CVE forces the
  stale entry out rather than leaving it to accumulate. An entry binds to an exact
  version, so a bump invalidates acceptances made against the old one — the
  assessment was of that build, not of the name. And `reason` must say something:
  empty, `TODO`, `n/a`, or fewer than six words is rejected, because "accepted
  because it says accepted" is the fiction this register exists to prevent.
- **Critical gates as well as High.** `REQ-SEC-004` says "high severity". Reading
  it so that the findings *above* High fall outside the gate would be a downgrade
  dressed as literalism.
- **The file ships empty, and that is a measurement rather than an omission.** In
  the gating configuration — no synthesised CPEs — grype 0.117.0 against database
  v6.1.9 returns **0 matches at any severity** from both the SBOM and the whole
  repository tree, so there is no finding to accept. The 21 High-or-Critical
  ffmpeg matches above come from `--add-cpes-if-none`, which OQ-046 measured at 49
  matches with 20 of them versionless; that output is advisory, never piped into
  this gate, and printed in the job summary instead.
- **The empty baseline is why the gate had to be watched failing.** A green run
  over a zero-match scan is the OQ-042 shape exactly. So a real vulnerable
  component — `pkg:npm/lodash@4.17.15` — was appended to a copy of the committed
  SBOM and scanned by the same binary against the same database: 6 matches, 3 High
  and 3 Medium. The gate reported the three Highs as unaccepted and ignored the
  three Mediums; with the three baselined it passed; and with those three entries
  still in place against the *un*injected SBOM it failed again, once per stale
  entry. Red, green, and red for the opposite reason, all from real scanner output.
- **What is still not proven.** The 21 is one database build (v6.1.9, 2026-08-26)
  on one machine and moves with the data; nothing here has run in CI, because no
  workflow in this repository has ever executed; and no ffmpeg finding has been
  assessed against this build's decode-only configuration — that assessment is
  what an entry in the baseline would have to contain, and it has not been done.

---

### OQ-050 — Every GitHub Action was pinned to a floating major tag · **Settled**

`REQ-SEC-013` forbids floating dependency versions, and `CONTRIBUTING.md` applies
that to Node tooling in as many words: `npx <tool>` "resolves whatever the registry
serves today", which is how the Markdown gate came to run a version nobody chose
(OQ-028). The six actions used across the three workflows were pinned the same way
they are in every example anyone copies from — to a major tag:
`actions/checkout@v4`, `actions/setup-python@v5`, `actions/setup-node@v4`,
`actions/upload-artifact@v4`, `actions/download-artifact@v4`,
`ilammy/msvc-dev-cmd@v1`.

A tag is mutable. `@v4` is whatever the maintainer last moved `v4` to, which is
the property `REQ-SEC-013` exists to forbid, and for a third-party action it is
also an arbitrary-code-execution surface with no pin in front of it.

- **How this surfaced.** Not from review. `grype dir:.` — run while measuring
  OQ-046, and expected to find nothing — reported one High-severity match:
  `GHSA-cxww-7g56-2vh6`, arbitrary file write via artifact extraction, against
  `actions/download-artifact@v4` in `spec-ci.yml`, fixed in 4.1.3.
- **Whether that finding was a true positive: probably not, and it did not
  matter.** `v4` today resolves to a 4.x release well past 4.1.3, so the runner
  almost certainly got fixed code; grype flagged it because `v4` is not a version
  it can order against `4.1.3`. The finding was still the useful kind — the reason
  the scanner cannot decide is precisely the reason the pin is wrong. An
  unorderable version is one nobody can audit.

**Resolution.** All 22 action references across the three workflows are pinned to a
full 40-character commit SHA with the version in a trailing comment. Each tag was
resolved to a concrete release and then verified twice by independent routes — the
GitHub releases and commits API, and `git ls-remote … refs/tags/<tag>^{}` — which
agreed on all six:

| Action | Was | Now | SHA |
|---|---|---|---|
| `actions/checkout` | `v4` | v4.4.0 | `11d5960a326750d5838078e36cf38b85af677262` |
| `actions/setup-python` | `v5` | v5.6.0 | `a26af69be951a213d495a4c3e4e4022e16d87065` |
| `actions/setup-node` | `v4` | v4.4.0 | `49933ea5288caeca8642d1e84afbd3f7d6820020` |
| `actions/upload-artifact` | `v4` | v4.6.2 | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| `actions/download-artifact` | `v4` | v4.3.0 | `d3f86a106a0bac45b974a628896c90dbdf5c8093` |
| `ilammy/msvc-dev-cmd` | `v1` | v1.13.0 | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` |

Every SHA is inside the major line the workflow already named, so pinning is not a
silent major upgrade. `download-artifact` 4.3.0 is past 4.1.3, so the finding that
started this is moot by version and not only by argument.

- **The trailing version comment is load-bearing, and that is measured, not
  assumed.** Two one-step workflows differing in nothing but the comment, scanned
  by the same grype build: with `# v4.4.0` the component is
  `pkg:github/actions/checkout@v4.4.0`; without it, the version syft records is
  the SHA itself — `pkg:github/actions/checkout@11d5960a…`. A SHA is not orderable
  against a fixed-in constraint, so a bare SHA pin reinstates the original defect
  in a form that looks pinned. Hence the gate requires the comment, and requires
  it to hold nothing but the version: trailing prose lands inside the string syft
  reads.
- **The pin is enforced, not merely applied.** `tools/check-action-pins.py`
  rejects mutable refs, branches, abbreviated and upper-case SHAs, a missing
  version comment, a prose-polluted one, and an undigested `docker://` reference,
  while accepting local composite actions. Its `--self-test` catches 12 planted
  defects and accepts 12 valid lines, 8 of which it confirms it actually parsed as
  references — the OQ-045 shape, since a parser matching nothing would report
  every workflow as clean. It also reads the workflows as **text**: a YAML parse
  is the obvious approach and would silently defeat the comment rule, because
  comments are not part of the parsed document. It runs as its own `action-pins`
  job in `repo-lint.yml`, the workflow with no `paths` filter, so a pull request
  that only touches `.github/` cannot skip it.
- **Empirical close.** Re-running `grype dir:. --exclude './node_modules/**'
  --exclude './build/**'` after pinning: **0 matches**, with all six actions still
  catalogued at orderable versions. Reverting a single pin to `@v4` in the working
  tree makes the gate fail, naming the file, line and mutable ref; deleting a
  single version comment while keeping the SHA also fails, naming the SBOM
  consequence. Both mutations were run against the real tree, not a fixture.
- **The other half of the trade is now written.** A pin never picks up a security
  fix on its own, and unlike a floating tag it goes stale silently, so pinning
  alone swaps one failure mode for a quieter one. `.github/dependabot.yml` covers
  the actions, the two Node gates and the hash-locked `jsonschema` stack — and,
  deliberately, says in its own comments what it does not cover: Dependabot has no
  vcpkg ecosystem, so §4.2's C and C++ dependencies stay hand-updated. Making it
  work with this repository's own commit gate needed a measured change to
  `commitlint.config.js`; that is OQ-051.
- **What is still not proven.** Nothing has been pushed to `origin`, so neither
  this gate nor Dependabot has ever run here. Whether Dependabot's
  `github-actions` ecosystem keeps SHA pins current for this repository is
  unknown until it has run once — the claim that it updates both the SHA and the
  trailing comment is read off its documentation and its own commit grammar, not
  observed. The SHAs were resolved from this machine; a reader re-verifying them
  should use the `git ls-remote` command the gate prints on failure.

### OQ-051 — Dependabot cannot satisfy the 72-column header limit · **Settled**

`commitlint.config.js` capped the commit header at 72 columns so `git log
--oneline` stays readable in an 80-column terminal, and `repo-lint.yml` enforces
it over every commit in the reviewed range. Dependabot's commits go through that
same gate, and its subject is generated from package names it does not choose.
Measured against this repository's own dependencies, before any change:

| Subject Dependabot would write | Columns |
|---|---|
| `ci(deps): bump the actions group with 6 updates` | 47 |
| `ci(deps): bump actions/download-artifact from 4.3.0 to 4.4.0` | 60 |
| `ci(deps): bump actions/checkout from 4.4.0 to 4.5.0 in the actions group` | 72 |
| `ci(deps-dev): bump markdownlint-cli2 from 0.23.2 to 0.24.0 in the node-tooling group` | **84** |
| `ci(deps-dev): bump @commitlint/config-conventional from 21.2.2 to 21.3.0 in the node-tooling group` | **98** |

Two rules failed, not one. Alongside the length, `scope-enum` rejected
`deps-dev`: dependabot-core picks the scope with
`dependencies.any?(&:production?) ? "deps" : "deps-dev"` and offers no setting
that changes it, and every Node dependency here is a devDependency. So the enum
had to admit `deps-dev` or every Node update would arrive red.

Adding `deps-dev` is bookkeeping. The length is a real conflict: a gate that
cannot be satisfied is a gate somebody turns off.

- **Three ways out, and why the third.** Raising the limit for every commit gives
  up a rule that is doing real work on the 34 commits already in this history.
  Putting the bot in commitlint's `ignores` exempts a whole class of commit from
  *every* rule — type, scope, body, requirement reference — to fix one, and
  `ignores` is handed only the message, so the exemption could not even be
  narrowed to the real author. What landed instead scopes the exception to the
  **message shape**: a subject in Dependabot's own bump grammar gets 100 columns,
  everything else keeps 72, and all other rules apply unchanged to both.
- **Bounded, not open.** 100 is the same limit already used for body lines, and it
  is a limit rather than an exemption: a 105-column bump subject still fails, and
  the failure message says which of the two allowances it exceeded.
- **A human writing that shape gets the same allowance, and that is fine.** The
  grammar is `bump <name> from <a> to <b>` optionally `in the <group> group`, or
  `bump the <group> group with <n> updates`. Anything matching it is a dependency
  bump; there is no interesting message that shape excludes.
- **Measured after the change, not assumed.** All seven realistic Dependabot
  subjects pass, including the 98-column one. Three controls still fail, each
  naming the right rule: a 105-column bump subject, a 90-column ordinary subject
  (72, as before), and `ci(deps-nope):` (scope-enum). The whole existing history
  — 34 commits — re-lints with 0 problems.
- **What is not proven.** No Dependabot commit has actually been produced here;
  the seven subjects above are constructed from dependabot-core's own message
  builder and this repository's real dependency names, which is close but is not
  the same as observing one. If its grammar changes, the regex in
  `commitlint.config.js` stops matching and the effect is a *stricter* gate — the
  bump falls back to 72 and fails, which is the safe direction for a mistake to
  fall.

---

### OQ-052 — §25.4's arrows: enumeration, or execution order? · **Open**

`REQ-BLD-024` is one sentence joined by arrows: "CodeQL (C++, Kotlin) → CVE scan
… → licence audit … → SBOM generation and diff against the previous release →
extended fuzzing … → hardening-flag verification on the produced binaries". Six
steps, five arrows, and nothing that says what an arrow means.

- **Assumption in force:** the arrows enumerate the steps and fix the order they
  are *described* in, not the order they *execute* in. `security.yml` runs the
  independent ones as parallel jobs and uses `needs:` nowhere, because no step
  consumes another step's output across a job boundary.
- **Why this is the narrow reading and not a weakening.** All six steps exist and
  every one of them can fail the workflow, which is the property the requirement
  protects. What the parallel shape gives up is that a failing CodeQL run would
  prevent the CVE scan from starting — and here that property has *negative* value.
  Chained, the 60-minute fuzzing matrix lands last, so a hardening regression is
  reported an hour and a half after the push instead of ten minutes, and one CodeQL
  failure hides five verdicts a reviewer needs in the same pass. Serialising also
  makes the daily run's wall-clock the sum of its parts, which is how a scheduled
  workflow ends up switched off.
- **Where an order is real, it is written down.** Two orderings are genuine data
  dependencies and appear as sequential steps inside a job: the resolved dependency
  graph must exist before the licence audit, the denylist and the SBOM can read it;
  and each gate script's `--self-test` runs before the gate it qualifies, so a green
  gate is never the first thing a job proves (OQ-045).
- **Recommendation:** amend §25.4 to read "each of the following, any of which
  fails the workflow" and drop the arrows, or state that the sequence is normative.
  As written the sentence cannot be complied with unambiguously — and under the
  literal reading the whole cost falls on feedback latency rather than on coverage,
  which is a poor trade to make by accident.
- **One clause of the same sentence is settled elsewhere, not by this entry.**
  "(C++, Kotlin)" names two languages and this repository has one: ADR 0011 defers
  the Android target, so there is no Kotlin to analyse. The CodeQL job prints a
  two-row coverage table naming the language it analysed and the reason the other
  is absent, rather than analysing one and reporting the step green — which is the
  distinction OQ-042 exists to keep.

### OQ-053 — Phase 0 wants all five workflows running; Phase 9 owns the release pipeline · **Open**

§28's Phase 0 **Build** list includes "all five CI workflows running", which names
`release.yml`. §28's Phase 9 **Build** list includes "signing; SBOM; installers;
**the release pipeline**". Both cannot be satisfied as written: a release pipeline
that runs in Phase 0 would have to sign and package four months before the phase
that builds signing and packaging.

The ingredients bear this out. Of `REQ-BLD-025`'s ten steps, four have something to
work with today — the semver/version-file check (step 1), SBOM generation (half of
step 6), the changelog from conventional commits (step 7), and regenerating
`docs/THIRD-PARTY.md` with a `releases.json` entry for the tag (step 8). The other
six need artifacts that no `install()` or `CPack` rule in this tree produces, a
signing identity that does not exist (`REQ-SEC-016`), an Android target deferred by
ADR 0011, and a release checklist document that has not been written.

- **Assumption in force:** the two lists describe different things under one name.
  Phase 0 needs the *workflow file* to exist and be green when triggered; Phase 9
  builds the *pipeline* — signing, installers, publishing. `release.yml` is
  therefore written now, does the four steps whose inputs exist, and stops at the
  publish boundary rather than skipping past it.
- **How the boundary is expressed:** not as a comment. The workflow detects whether
  packaging rules exist and refuses to publish while they do not, the same shape
  OQ-040 now uses for the theme engine: the gate flips when the thing it needs
  lands, so nobody has to remember to come back.
- **Why not simply defer the file to Phase 9:** a release workflow first exercised
  at release time is the classic way releases fail. Its version check, changelog
  generation and staleness check are exercisable now, so they are exercised now —
  and `workflow_dispatch` makes them runnable without cutting a tag.
- **Related, and confirming:** Phase 0's own exit gates ask less than its Build list
  does. Gate 3 is "`spec-ci.yml` green — `theme-schema.json` is a valid JSON
  Schema", which the `schemas` job proves and which needs no C++ engine at all. The
  `native` job that OQ-040 records was over-reaching relative to the gate its own
  phase sets, which is some evidence that "all five workflows running" means running
  and green, not feature-complete.
- **Recommendation:** amend Phase 0's Build list to "all five CI workflow files
  present and green" and leave the release *pipeline* in Phase 9 where its
  dependencies are. The distinction is real and the specification already relies on
  it; it just is not written down.
- **Consequence if unfixed:** either Phase 0 cannot close on a literal reading of
  its own Build list, or `release.yml` gets written as ten steps of which six are
  theatre — and a release pipeline that reports success without producing a signed
  artifact is the worst possible place for the OQ-042 shape to live.
- **What landed.** `release.yml` now exists in exactly that shape: `preflight`
  (steps 1, 2 and step 8's precondition), `changelog` (step 7), `sbom` (half of
  step 6), `third-party` (step 8's checkable half), and `publish`, whose entire
  job is to refuse. The refusal is itemised — packaging, signing, Android — so the
  report shrinks by a row as each lands, and when the table empties the job
  **fails** rather than passing, because at that point the missing thing is the
  publish steps themselves. Step 2 is scoped to the `v1.0.0` tag alone, which is
  what §25.5 says; below 1.0.0 it says so instead of silently skipping.
- **One thing this entry got wrong, and how.** The first draft of the step-8
  precondition re-implemented the generator's rule in shell, and got it wrong in
  the strict direction: it required both `offer.postal_address` and
  `offer.mailbox`, while `gen-third-party.py` refuses on `postal_address` alone.
  That would have failed tags the generator accepts. The step now **asks** the
  generator — it plants a throwaway release row in a copy of the ledger, runs the
  source-offer document, and looks for `OQ-013` in the output. The probe's row is
  deliberately incomplete so the generator also complains about the row; that is
  irrelevant, because the discriminator is whether OQ-013 is named, and both
  directions were measured. A copy of a condition is a copy that drifts, and this
  one drifted before it was ever committed.
- **Closes when:** §28's Phase 0 Build list distinguishes the workflow files from
  the release pipeline, or Phase 9 lands packaging and signing and the `publish`
  job's steps are written against them.

### OQ-054 — `app/` ships two of the four things §5 lists in it · **Gap**

§5's repository layout annotates `desktop/src/app/` as *"application object,
lifecycle, DI container, CLI"*. The first two landed with the application layer.
The **DI container** and the **CLI** did not.

- **Why no container yet:** a dependency-injection container exists to hand out
  implementations of interfaces. §7.1's layer 2 lists ten of them — `IDecoder`,
  `IAudioSink`, `ITagReader`, `ITagWriter`, `ILibraryIndex`, `IHttpClient`,
  `IClock`, `IFileSystem`, `IMediaSession`, `IPluginHost` — and Phase 0 defines
  **none**. A container written now would be designed against nothing: its
  registration signature, its lifetime policy and its thread-affinity rules would
  all be guesses about objects that do not exist, and the first real port would
  either fit by luck or force the rewrite of every call site added in the
  meantime.
- **Why no CLI yet:** §28 assigns "single-instance IPC, CLI, safe mode, portable
  mode" to **Phase 4**, not Phase 0. An argument parser that accepted `--play` or
  `--enqueue` and ignored them would be worse than their absence, because it
  teaches a vocabulary the program does not have — and scripts written against it
  would break on the release that made the flags real.
- **Note on where the obligation comes from:** neither item has a `REQ-` id. §5 is
  a layout diagram, and the CLI's requirement ids live in Phase 4's area. It is
  recorded here anyway: §0.1 rule 2 forbids narrowing a requirement silently and
  does not make the ban conditional on the requirement having been numbered.
- **Consequence:** `desktop/src/app/` is smaller than §5's line for it, and
  `arrow-player` ignores `argc`/`argv` entirely rather than half-reading them.
  Nothing in the tree depends on the two absent parts, so nothing is stubbed to
  paper over them, and `src/app/application.hpp` cites this entry at the point
  where a reader would otherwise wonder where the container went.
- **Closes when:** Phase 1 defines the first layer-2 ports and a container has
  real registrations to hold, and Phase 4 lands the CLI alongside the
  single-instance check that shares its entry path.

### OQ-055 — §7.1 numbers adapters *below* ports, which inverts rule 1 · **Open**

§7.1 stacks the layers 5 down to 1 with **PORTS at 2** and **ADAPTERS at 1**.
`REQ-GEN-050` rule 1 then says layer *N* may depend on layers *< N* and never on
layers *> N*. Applied to that numbering, the arithmetic says:

- an adapter (1) may depend on **nothing** — including the port it implements;
- a port (2) **may** depend on an adapter (1).

Both conclusions are backwards, and the second directly contradicts rule 3 of the
same requirement: *"adapters are reachable only through their layer-2 port"* is
unsatisfiable if the port itself is allowed to name the adapter.

- **Assumption in force:** the diagram's bottom two rows are a listing order, not
  a dependency order. `tools/check-layers.py` therefore states both directions
  outright instead of computing them: layer 1 **may** include layer 2 (an adapter
  implements its port), and **nothing but the composition root** may include a
  layer-1 header — not a port, not the domain, not the application layer. Rule 1's
  arithmetic governs layers 2 through 5, where it is correct.
- **Why this reading and not the literal one:** the literal one forbids every
  adapter this project intends to ship. FFmpeg's decoder cannot implement
  `IDecoder` without including it. A rule whose only consistent outcome is an
  empty adapter layer is a numbering error, not a design.
- **Why it was not visible before:** rule 1 was enforced only for the domain
  layer (OQ-031), and rule 3 was enforced only against *third-party* headers.
  Neither view could see an internal include crossing the port/adapter boundary,
  so the inversion sat in the specification unremarked until the map was written.
- **Recommendation:** either renumber — PORTS 1, ADAPTERS 2, with the diagram
  redrawn so "lower" means "depended upon" — or add one sentence to §7.2 saying
  that layer 1 may depend on layer 2 and on nothing else. The second is a smaller
  edit and matches what the gate now enforces.
- **Consequence if unfixed:** the specification and the gate disagree on paper
  while agreeing in practice, and the next person to read rule 1 literally will
  either "fix" the gate or conclude the adapters are all illegal.

---

## 5 · Verification status — what is proven where

The governing record for the scope decision behind this section is
[ADR 0011](adr/0011-desktop-first-sequencing.md).

### What *is* verified on the development machine

Stated first, because the rest of this section is gaps and it would be dishonest
to let them imply that nothing has been run.

| Check | Result |
|---|---|
| `cmake --preset linux-release` + `ctest` from a clean build directory | **210/210 passed** — 206 unit, 4 fuzz-corpus |
| `cmake --preset linux-asan` + `ctest` (ASan + UBSan) | **210/210 passed** |
| `cmake --preset linux-tsan` + `ctest` (ThreadSanitizer) | **210/210 passed** |
| `cmake --preset linux-fuzz` + `ctest` — fuzzers without GoogleTest | **4/4 passed**; libFuzzer reported unavailable (GCC), corpus replay built and run |
| `desktop/tests/fuzz/make-seeds.py --check` | pass — 49 committed seeds byte-identical to the generator |
| `-Werror` with the strict warning set | clean |
| `tools/check-layers.py`, `check-sql-safety.py`, `check-rt-safety.py` | pass |
| `tools/check-layers.py --self-test` | pass — 29 synthetic trees over all four checks, 16 of them planted violations |
| `tools/check-sql-safety.py --self-test` | pass — 10 injection sites caught, 8 safe constructs left alone, 1 documented blind spot still blind |
| `tools/check-rt-safety.py --self-test` | pass — 11 false RT-SAFE claims caught, 7 legitimate constructs left alone, span finder bounds both bodies |
| `tools/validate-shared-spec.py --self-test` | pass — 14 defects planted in a copy of `shared-spec/`, each caught with the right complaint, control run clean |
| the same, with one mutation replaced by a no-op and one complaint misspelt | fails, naming both — 2 of 2, so the harness is not vacuous |
| `tools/check-hardening.py --self-test` | pass — 31 assertions over synthetic ELF and PE binaries, 22 of them planted defects that must be caught |
| `tools/check-hardening.py build/linux-release` | pass — 7 binaries: PIE, RELRO, `BIND_NOW`, non-exec stack, `__stack_chk_fail`, fortified entry points in 5 of 7 |
| the same, before `arrow_set_hardening` was wired in | **failed 4 of 7** — the fuzz replay drivers had only partial RELRO; that is why the gate exists |
| the same, pointed at `build/linux-asan` | exits 2 — every binary out of scope, so it refuses to report success |
| `tools/validate-shared-spec.py` | pass — 5 schemas, 102 JSON documents |
| `.github/scripts/spec_full_validate.py --check-schemas --check-fixtures` | pass — 5 schemas valid, 91 fixtures match their claimed verdict |
| the same, with defects planted (`"type": 5`; a flipped verdict; an undeclared `$id`) | fails, as it must — 3 of 3 |
| `.github/scripts/compare_verdicts.py --self-test` | pass — 10 scenarios, 6 of which must fail and do |
| `tools/check-doc-links.py` | pass — 34 documents, 243 internal links, 23 §27 deliverables present, and the register itself: **52** entries, contiguous 1..52, every status in the legend, every `OQ-NNN` citation in the tree resolving |
| `tools/check-dependency-denylist.py --self-test` | pass — 24 denied, 48 allowed, 0 either way |
| `tools/check-doc-links.py --self-test` | pass — 9 heading-slug cases, plus 6 register cases: 5 planted defects each rejected for the right reason, 1 fenced example correctly accepted, over a 6-entry valid control |
| the register check, mutated against the real tree | pass — three separate mutations of the committed files each turn it red: removing OQ-039's heading (reported as a hole **and** as two now-dangling citations from `docs/SKIN-AUTHORING.md` and elsewhere), citing an `OQ-0NN` nobody defines, and giving OQ-050 the invented status `**Done**`. The tree restores byte-identical afterwards |
| `tools/gen-third-party/gen-third-party.py --self-test` | pass — fixture parses to 19 ports; the unknown-component gate fires; the `REQ-GEN-020` ledger gate fires on all 7 malformed release rows |
| the same with `--check`, both documents | pass — `docs/THIRD-PARTY.md` and `docs/LGPL-SOURCE-OFFER.md` byte-identical to a fresh render |
| `tools/gen-sbom.py --self-test` | pass — 18 planted register defects caught, 10 valid registers accepted, 25 planted document defects caught over a control that validates, 10 purl shapes spelled out, and the `--resolved-graph` path run through the real vcpkg dry-run parser and classifier |
| the same, with each new invariant inverted one at a time | fails, naming the case — 8 of 8: the dropped `build_only` bucket, a graph-blind version gate, a guessed port-version, `pkg:generic` for a vcpkg port, unsorted qualifier keys, a target triplet on a host-side helper, an upper-case `serialNumber`, and the component/port-name gate removed |
| `tools/gen-sbom.py --check` | pass — `docs/sbom/arrow-player.cdx.json` byte-identical to a fresh render |
| that document, and a resolved-graph one, against the canonical `bom-1.6.schema.json` | **0 errors each**; CycloneDX's own `valid-bom-1.6.json` control 0 errors; a planted `scope: "mandatory"` caught — but through a throwaway draft-07 adapter, not committed code (OQ-048) |
| both documents' 24 purls against the purl grammar | pass — no namespace on `pkg:vcpkg`, qualifier keys sorted, no subpath, no unexpected qualifier |
| `tools/gen-sbom.py --require-exact-versions` | **fails**, naming 10 of 13 (OQ-047); over a hand-written dry-run graph it names 1 of 13 — nlohmann/json, which has no vcpkg port. Real `vcpkg install --dry-run` output has still never been through it |
| `grype 0.117.0 sbom:docs/sbom/arrow-player.cdx.json` (db v6.1.9) | **0 matches** over 23 components — and the same document with one `pkg:npm/lodash@4.17.15` appended returns **6**, so the reader works and the zero is `pkg:vcpkg` having no matcher (OQ-046) |
| the same, `--add-cpes-if-none` | **49 matches** — 8 Critical, 27 High, 13 Medium, 1 Low; all `stock-matcher`/`cpe-match`; **20 of 49** on a component with no version, so advisory only |
| `grype dir:desktop` | **1 component catalogued** — `pkg:vcpkg/arrow-player@0.1.0`, the project itself, no dependencies. The native path covers less than the SBOM, correcting this register's own earlier proposal |
| `grype dir:.` (`node_modules/`, `build/` excluded) | 23 components, **1 High** — `GHSA-cxww-7g56-2vh6` against `actions/download-artifact@v4` in `spec-ci.yml`. Not a C/C++ dependency; recorded as OQ-050 |
| the same, after every action was SHA-pinned | **0 matches**, six actions still catalogued at orderable versions. Two one-step control workflows differing only in the trailing `# v4.4.0` comment show syft recording either `@v4.4.0` or the bare SHA as the version — which is why the gate requires the comment |
| `tools/check-cve-baseline.py --self-test` | pass — 21 cases: 17 planted defects each rejected for the right reason (unbaselined High, Critical, stale entry, version drift, placeholder and too-short reason, expired acceptance, missing and mistyped field, malformed and impossible date, duplicate, and three unreadable scans) and 4 valid inputs accepted, including Medium and Negligible **not** gating |
| the CVE gate against real grype output | pass — the committed baseline is empty because the gating configuration finds nothing: **0 matches at any severity** from both `sbom:docs/sbom/arrow-player.cdx.json` and `dir:.`, grype 0.117.0 / DB v6.1.9. Proven non-vacuous by injecting `pkg:npm/lodash@4.17.15` into a copy of the SBOM: 6 matches, the 3 High unaccepted → red, baselined → green, then stale against the uninjected document → red again, once per entry |
| `tools/check-action-pins.py --self-test` | pass — 12 planted defects caught (mutable tag, branch, short and upper-case SHA, missing version comment, prose-polluted comment, undigested `docker://`), 12 valid lines accepted, 8 confirmed parsed as real references |
| the same over the real tree, one pin reverted to `@v4`, then one version comment deleted | **fails both times**, naming file, line and reason; passes over 22 references when restored |
| seven realistic Dependabot subjects against `commitlint.config.js` | all pass, including a 98-column one; three controls still fail and name the right rule — a 105-column bump subject, a 90-column ordinary subject, and an out-of-enum `deps-nope` scope (OQ-051) |
| `commitlint` over the whole history after that change | 34 commits, **0 problems** — the relaxation did not loosen anything the existing history relied on |
| `.github/dependabot.yml` | parses; three ecosystems, each grouped into one pull request. Its pip entry was checked against dependabot-core rather than assumed: `requirements_file?` accepts `requirements-spec.txt` because the name matches `/requirements/`, and the requirement replacer rewrites `--hash=` entries. **Never run** — nothing is pushed |
| `gen-sbom.py --self-test` and `--check` in `repo-lint.yml` | wired — self-test in the `Gate self-tests` step, `--check` beside the two licence documents, in a workflow with no `paths` filter. **Caveat that applies to every workflow here:** nothing has been pushed to `origin`, so no workflow in this repository has ever executed. Every result in this table is a local run |
| `security.yml` — six jobs for §25.4's six steps | written and parsed (6 jobs, `yaml.safe_load`); every `uses:` SHA-pinned with a version-only comment (`check-action-pins.py`: 4 files, 38 references). **Not executed**, per the caveat above. Two things are non-blocking by design until their first run — the `resolved-graph` job (OQ-038) and the Windows hardening step (OQ-044) — and each records the condition for removing that |
| `cyclonedx-cli validate` against the committed SBOM | pass locally — valid → exit 0, a planted `scope: "mandatory"` → exit 1, **with every egress route poisoned** and with no libicu on the machine, so the schemas are embedded in the executable. `--fail-on-errors` proven load-bearing: without it the invalid document is *reported* invalid and the process still exits 0 (OQ-048) |
| `vcpkg install --dry-run` on this machine | **not run** — vcpkg is not installed here. The graph parser is exercised against a hand-built fixture and against a planted unregistered port, which it rejects by name; the runner's own vcpkg is what OQ-038 wants a measurement from |
| `spec-ci.yml`'s `native` and `agreement` jobs | rewritten and **executed locally, step by step**, with `GITHUB_OUTPUT`/`GITHUB_STEP_SUMMARY` redirected to files. The engine-detection step gives the right answer in all four tree states — directory empty, directory absent as on a checkout, sources with no `CMakeLists.txt` (**fails**, as it must), and `CMakeLists.txt` present (`engine=present`) — and the binary-discovery step takes the right branch in five: no build directory, no executable, exactly one (flags passed through verbatim), two (**refuses to guess**), and a non-executable file. Both summary steps rendered and read as a human would read them. What is **not** proven is the engine, which does not exist: OQ-040 |
| `release.yml`'s five jobs | **executed locally, step by step**, with `GITHUB_OUTPUT`/`GITHUB_STEP_SUMMARY` redirected to files. Step 1 across seven ref states (dispatch, matching tag, mismatched tag, `v1.0`, `v1.0.0-rc1`, `v01.0.0`, `0.1.0`); step 2 across five (untagged, below 1.0.0, no checklist, unticked item, all ticked); the OQ-013 probe in all three; `publish` in four, with packaging and Android planted and removed, confirming three rows → two → one → **exit 1**. The changelog, SBOM and licence steps ran for real against the committed data. What is **not** proven is any of it on GitHub: nothing has been pushed and no tag exists, so no run of this file has ever happened: OQ-053 |

Toolchain in use: CMake 3.31.6, Ninja 1.12.1, GCC 14.2.0, and — in a user-local
`~/.local` prefix, no root required — FFmpeg 7.1.1 (LGPL-configured,
decode-oriented, `--disable-network`), TagLib 2.0.2, alsa-lib 1.2.12,
SQLite 3.46.1.

So `README.md`'s test count is locally reproduced, not merely asserted by CI. The
number moved from 186 to 193 in the commit that added the fuzz targets: three unit
cases pinning the two defects the corpora found — two for
`normalize_relative_path()`, one for `gapless_from_granule()` — plus the four
corpus-replay cases themselves.

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

`tools/check-doc-links.py` now passes and is wired into `repo-lint.yml`. It was
listed as failing rather than omitted for as long as §27 documents were missing —
seven of them at first, then one — because a gate added while red is a gate
somebody will be tempted to weaken. The one document it still names,
`docs/PLUGIN-AUTHORING.md`, is `[v1.x]` and reported as a note rather than a
failure, which is the difference between deferred and forgotten.

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

### OQ-018 — Phase 0 exit gate 2 needs its first green run; `REQ-GEN-030` is partially met · **Gap**

ADR 0012 restored the Android target ADR 0011 had deferred, so `android/` and
`android-ci.yml` now exist. What exists is a **Phase 0 scaffold** — a Gradle +
Kotlin + Compose app that builds, lints, unit-tests and assembles a debug APK,
plus an emulator job that installs and launches it. What does not exist yet:

| Requirement | Status |
|---|---|
| Phase 0 exit gate 2 — `android-ci.yml` green, APK on emulator | **First green run pending.** Nothing has been pushed to origin, so no run exists. The emulator job carries `continue-on-error` (the OQ-044 shape) and the condition for removing it is its first green run |
| `REQ-GEN-030` — repository layout | **Partially met.** `android/` exists; the §5 module list (core-*, feature-*, auto/, benchmark/) is not yet implemented — one `:app` module today |
| `REQ-GEN-031` — both engines agree on the conformance fixtures | **Half-proven.** The Android validator is unwritten; one engine conforming is not two engines agreeing |
| `REQ-AUD-108` — desktop/Android DSP within −90 dBFS RMS | **Untestable.** One side of the comparison is missing |
| `REQ-LIB-001` — a schema change lands on both platforms in one commit | **Partially enforceable.** A second platform now exists to fail the gate; the same-commit rule applies to its files |
| Android half of the §4.2 register | **Not reconciled.** The register lists NDK components (projectM, Chromaprint) the scaffold does not use; a future commit reconciles the catalogue with `libs.versions.toml` (ADR 0012 notes this) |

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
`ArrowDependencies.cmake` looks for that is absent locally, so
`ARROW_HAVE_SAMPLERATE` is `OFF` and now says so in the configure summary.

- **Correction of record:** `ArrowDependencies.cmake` asked for `>= 0.1.9`,
  the licence floor, where the register says `>= 0.2.2`. Those are two different
  floors and only one of them is `REQ-GEN-012`. A 0.1.9 build would have been
  licence-clean and still absent from the register, which REQ-GEN-012 makes a
  build failure rather than a footnote. The check now requires `>= 0.2.2`; the
  vcpkg baseline resolves exactly 0.2.2.
- **Impact:** none yet. The resampler is Phase 6 work. Recorded so that when
  Phase 6 starts the missing dependency is a known item rather than a surprise.

### OQ-021 — Dependency detection needs `PKG_CONFIG_PATH` for a user-local prefix · **Settled**

`ArrowDependencies.cmake` finds FFmpeg, TagLib, ALSA and libsamplerate through
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
| `vcpkg install --dry-run --triplet x64-linux-arrow` | resolves; `ffmpeg` pins to 7.1.2#5, no OpenSSL, no bzip2 in the graph |
| triplet evaluation for 14 ports | linkage matches the §4.2 column exactly — LGPL dynamic, permissive static |
| `cmake --preset linux-release` with `VCPKG_ROOT` unset | unchanged; the suite still passes in full (186/186 on the day of that run; 210/210 now) |

What was **not** run: an actual `vcpkg install`. No port was compiled, so the
manifest is proven to *resolve*, not proven to *build*. `arm64-linux-arrow`
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
3. If the entry recorded a wrong earlier claim, keep the correction where the
   claim was, so a reader meets both together — that is why OQ-046's "scan the
   source tree instead, the native path covers more" and OQ-048's "cannot be
   installed without root" are still legible above, each next to the measurement
   that refuted it. §29.6 of `eclipse-player.md` is **not** the place for these:
   that table corrects claims made by the *specification*, and a register entry's
   mistake is the implementation's, not the spec's. Add a row there only when the
   thing that was wrong is a requirement.
4. Move the entry to a `## Closed` section here with the resolution and the
   commit. **Do not delete it.** The point of a register is that it still shows
   what was once uncertain, and an entry that vanishes leaves the next reader
   unable to tell whether it was answered or forgotten.

# `shared-spec/` — the cross-platform contract

This directory is the single source of truth for everything the desktop (C++/Qt) and
Android (Kotlin) engines must agree on. It contains **no code**: only schemas, grammars,
token values, one protocol document, and conformance fixtures with the verdicts both
engines must reach.

`REQ-GEN-031` is the reason it exists:

> The desktop and Android engines MUST produce **identical** verdicts on the shared conformance
> corpus.

"Shared format" is a claim; a fixture corpus with recorded verdicts is evidence. Two
independent implementations agreeing on 455 cases is a fact you can re-check after every
change. Two implementations that merely read the same prose are two implementations that
will drift.

---

## Layout

| Path | Contents | Spec |
|---|---|---|
| `schemas/theme-schema.json` | Theme token document (`theme.json`) | §11.2, `REQ-THM-010…012` |
| `schemas/skin-manifest.schema.json` | `manifest.json` inside a `.eclipseskin` | §11.3, `REQ-THM-015…019` |
| `schemas/layout.schema.json` | The `.eclayout` layout DSL | §11.4, `REQ-THM-025…033` |
| `schemas/settings.schema.json` | Settings export bundle, and the **source of truth for every default** | §19.4, `REQ-SET-002/007` |
| `schemas/smart-playlist.schema.json` | Smart-playlist rule documents | §9.6, `REQ-PLS-005…013` |
| `grammars/eclipse-format-strings.ebnf` | EFS grammar | §10.2, `REQ-EFS-001` |
| `grammars/smart-playlist.ebnf` | Human-readable rule syntax | §9.6 |
| `design-system/tokens.json` | Spacing, type scale, radius, elevation, motion — the numbers | §12.1, `REQ-UIX-001` |
| `design-system/{typography,motion,iconography}.md` | How to *use* those numbers, without restating them | §12.1 |
| `sync-protocol.md` | Wire format, message types, merge rules, threat model | §18, `REQ-SYN-012/014` |
| `conformance/efs-cases.json` | 295 EFS cases | `REQ-EFS-012` (≥150) |
| `conformance/smart-playlist-cases.json` | 38 rule→SQL cases | `REQ-PLS-012` |
| `conformance/theme-validation-cases/` | 122 package-validation cases + `index.json` | §11.5, `REQ-THM-040…042` |

## The fixtures are normative

A conformance file is not a set of examples an implementation may interpret generously.
Where the prose leaves a question open, **the fixture answers it**, and the answer is
written down in the case's `note` field with the reasoning. Some of those answers were
genuine forks in the road:

- EFS string indices are **zero-based** (`$sub`, `$strchr`, `$strstr`). A one-based
  reading is equally plausible and silently produces off-by-one output everywhere.
- EFS lengths and cuts count **grapheme clusters**, not code points or bytes — the
  functions exist to fit text into a column, and a user counts what they see.
- Case conversion is **locale-independent**, so `$lower(I)` is `i` even under `tr-TR`.
  Locale governs dates, numbers and durations only; if it governed case, two users would
  see different strings for the same library and `REQ-GEN-031` would be unsatisfiable.
- A function that cannot be *applied* (unknown name, wrong arity) renders **literally**;
  a function that is applied with invalid operands yields **absent**. One rule, two
  outcomes.
- `$div` by zero, arithmetic overflow, and non-numeric operands all yield **absent**
  rather than zero or an error. Missing data must not become a confident wrong number.
- An optional block containing **no** field reference renders in full. Read literally,
  §10.3's collapse condition is vacuously true for such a block, which would make
  `[ - ]` vanish; that reading is rejected because the rule exists to hide
  separators belonging to absent data.

Three places where the specification contradicts itself are recorded rather than
resolved unilaterally. All three are in `docs/OPEN-QUESTIONS.md` as `OQ-001`, `OQ-002`
and `OQ-003`; the first two are also listed in `theme-validation-cases/index.json` under
`openConflicts`, because they are still genuinely open. The third is resolved in
`layout.schema.json` by taking the narrowest reading that satisfies both requirements,
so it is recorded as a narrowing rather than as an open conflict:

1. **`REQ-THM-011` vs the §11.2 schema.** The requirement promises a five-line theme
   that only changes the accent colour; the schema requires
   `color.{background,surface,text,accent,border}` and `typography`.
   `invalid/theme-extends-accent-only.json` records what the schema does *today*, and
   moves to `valid/` when the conflict is closed.
2. **`REQ-THM-040` step 10 vs `REQ-EFS-009`.** Runtime truncates at 4096 characters, so
   an over-long pattern is harmless; install-time "output cap verified" is only
   meaningful if such a pattern is refused. `malicious/layout-efs-output-bomb.eclayout`
   carries both verdicts.
3. **`REQ-THM-027` vs `REQ-THM-031`.** The state model is said to expose no settings
   values, yet the worked example binds `settings.showMiniVisualizer`.
   `layout.schema.json` follows the narrowest reading that keeps both true: a short
   allowlist of presentation-affecting settings.

Resolving any of these is a **specification** change, not an implementation detail. §0.1
forbids silently downgrading a requirement, and picking a side in the implementation
would do exactly that — invisibly.

## Versioning

Every schema carries `"$id": "https://arrow-player.org/schemas/<name>/v1"` and every
document it validates carries `"schemaVersion": 1` as a `const`. The two move together.

**Within `v1`** — allowed without a version bump, because a v1 reader keeps working:

- adding an **optional** property;
- adding a member to an enum **only** where the consumer already handles unknown members
  (nowhere in v1: every enum here is closed, so in practice this does not apply);
- adding a conformance case;
- correcting a `description`, or adding a `note` to a fixture.

**Requires `v2`** — a new `$id`, a new `const`, and a migration path:

- adding or removing a **required** property;
- narrowing a type, pattern, or range;
- removing a property or an enum member;
- changing what an existing conformance case expects.

That last one is the important one. **A fixture is never weakened to make an
implementation pass.** If an engine disagrees with a case, exactly one of two things is
true: the engine is wrong, or the case is wrong and the specification needs changing.
Editing the expectation to match the code destroys the only evidence that the two
platforms agree.

`schemaVersion` is a `const`, not a minimum, so a v1 loader **refuses** a v2 document
instead of reading the parts it recognises. Partial reads of a newer format are how a
"compatible" loader silently drops a user's settings.

## Checking this directory

```bash
python3 tools/validate-shared-spec.py
```

Standard library only — no `pip`, no virtualenv — so it runs in a bare container and in
a contributor's shell without setup. It uses `tools/jsonschema_mini.py`, a draft-2020-12
**subset** validator with a keyword allowlist: a schema using a keyword the subset does
not implement is a **hard error**, never a silent pass. What it enforces:

- every JSON document parses; every schema has `$schema` and a unique on-domain `$id`;
- every `$ref` resolves, and none points outside this directory;
- every JSON fixture is validated against its paired schema and the result is compared
  with the verdict the corpus claims — including the cases that claim to be
  **rejected**;
- no fixture is on disk but missing from `index.json` (an unlisted fixture is a case
  neither engine is asked to agree on);
- the numeric minimums the spec states: ≥150 EFS cases, all 46 §10.5 functions
  exercised, the eight `REQ-PLS-012` worked examples present, a documented default for
  every settings key, `privacy.telemetryEnabled` pinned to `const: false`;
- expected SQL never contains a quoted literal — every value is a bound parameter
  (`REQ-PLS-010`, `REQ-SEC-009`);
- `tokens.json` and `theme-schema.json` describe the same seven type styles.

What it deliberately does **not** check, and where that happens instead:

| Not checked by this script | Where it runs |
|---|---|
| Full draft-2020-12 semantics, `format` assertions | `.github/scripts/spec_full_validate.py` |
| SVG sanitisation, ZIP limits, image probing | not yet — `tools/theme-validate` is a Phase 5 deliverable (OQ-040) |
| WCAG contrast computation | desktop tests against `contrast/pairs.json` |
| EFS and smart-playlist **evaluation** | desktop conformance tests |
| Android verdicts (the other half of `REQ-GEN-031`) | not yet — see below |

The first row moved out of "CI-only" once it turned out `jsonschema` is already
present on the development machine as a distribution package. The full validator
therefore runs locally too:

```bash
python3 .github/scripts/spec_full_validate.py --check-schemas --check-fixtures
```

That does not make the stdlib script redundant — it is the one that runs in a bare
container, and its keyword allowlist catches a schema using a keyword no
hand-written validator implements, which the real library would happily accept and
silently pass. Run both. They deliberately disagree in one direction only: if the
subset accepts something the full validator rejects, the subset has a bug, and
that is a finding rather than a nuisance.

## Status: `REQ-GEN-031` is unproven on both sides

There is no Android validator and no desktop theme engine — `tools/theme-validate` is a
Phase 5 deliverable and the Android app is a Phase 0 scaffold (ADR 0012 restored the
target ADR 0011 had deferred), so **zero** implementations currently produce verdicts,
not one. The corpus is complete and platform-neutral so either engine can consume it
unchanged, but **agreement between two engines cannot be claimed while neither exists.**
Recorded in `docs/adr/0011-desktop-first-sequencing.md`, `docs/adr/0012-restore-android.md`
and `docs/OPEN-QUESTIONS.md` (OQ-018, OQ-040) rather than left to be inferred from an
absent directory. `spec-ci.yml`'s `agreement` job says the same thing in its own job
summary on every run, so the gap is visible where the gate is, not only here.

## Adding a case

1. Write it in the relevant `conformance/` file. Give it a stable `id` — ids appear in
   failure output and in commit messages.
2. If it settles an ambiguity, say so in a `note`, with the reasoning and the reading
   you rejected. A fixture whose *why* is undocumented gets "fixed" by the next person
   who trips over it.
3. For `theme-validation-cases/`, add the matching `index.json` entry: the pipeline step
   that judges it, the schema (or `null` when the judge is code), the verdict, and the
   `REQ` id.
4. Run `python3 tools/validate-shared-spec.py`.
5. Reference the `REQ` id in the commit body (`REQ-BLD-031`).

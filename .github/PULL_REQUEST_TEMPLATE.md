<!--
  §1.3 of eclipse-player.md is the Definition of Done, and it applies to every
  unit of work. This template is that list, not a shorter version of it: a
  checklist that omits items is a checklist that quietly redefines "done".

  Items that genuinely do not apply — a docs-only change has no benchmark — are
  marked N/A with the reason. "N/A" with no reason reads as an unticked box.
-->

## What this changes

<!-- One or two sentences. What behaviour is different afterwards? -->

## Requirement

<!--
  §1.3 rule 1: this implements a stated requirement ID, or it is refactoring
  with no behaviour change. Rule 10 puts the ID in the commit message too, and
  commitlint enforces that for feat/fix/perf/revert.
-->

- Requirement ID(s):
- Or: refactoring with no behaviour change — <!-- say what moved and why -->

## Definition of Done (§1.3)

- [ ] 1 · Implements a stated requirement ID, or is refactoring with no
      behaviour change.
- [ ] 2 · Unit tests cover the happy path, **every documented failure mode**,
      and every boundary condition.
- [ ] 3 · `clang-format` clean; `clang-tidy` produces no new findings.
- [ ] 4 · Public APIs documented, with thread-safety **and** RT-safety stated in
      the doc comment.
- [ ] 5 · CI green on **every** platform in the matrix, not just mine.
- [ ] 6 · Every user-visible string is externalised for translation (§12.7).
- [ ] 7 · Any new dependency is in §4.2 **and** in the SBOM
      (`python3 tools/gen-sbom.py` then commit the regenerated document).
- [ ] 8 · Any deviation from `eclipse-player.md` is recorded in `docs/adr/`.
- [ ] 9 · Performance-sensitive paths (§20) have a benchmark and it did not
      regress.
- [ ] 10 · The commit message references the requirement ID.

## Gates run locally

<!--
  CONTRIBUTING.md has the exact sequence, self-tests first. Paste the tail of
  the run rather than asserting it: a claim is not a result.
-->

- [ ] Every gate's `--self-test`, then every gate.
- [ ] `npx markdownlint-cli2` (no arguments — the file set lives in the config).
- [ ] `ctest` for the presets this change can affect.

## Anything assumed

<!--
  §0.1 rule 1: anything assumed goes in docs/OPEN-QUESTIONS.md with a proposed
  answer. Rule 2: never invent a requirement, and never silently downgrade one.
  If this PR weakens a gate, makes something non-blocking, or leaves a step
  unimplemented, say so here and link the OQ entry — that is the difference
  between a recorded gap and a hidden one.
-->

- OQ entries added or changed:

# ADR 0011 — Desktop-first sequencing, with the Android gap stated

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-GEN-030, REQ-GEN-031, REQ-BLD-020, REQ-AUD-108, REQ-LIB-001

## Context

§5 defines a repository layout containing both `android/` and `desktop/`, and
`REQ-GEN-030` requires the repository to match it. §28's Phase 0 exit gates
require `android-ci.yml` green with an APK building and installing on an emulator
(gate 2), alongside the desktop gates.

Implementation in this repository has proceeded on the desktop side only. That is
a deliberate scoping decision, and this ADR exists because §0.1 rule 2 forbids
silently downgrading a requirement to make a gate pass. The requirement is not
met. Recording that plainly is the alternative to pretending otherwise.

The reason is capacity, not disagreement with the requirement. Building both
platforms in parallel from Phase 0 requires two toolchains, two CI pipelines, and
two implementations of every shared behaviour to be kept in step by
`REQ-LIB-001`-style same-commit rules. Doing that with the resources available
would mean both platforms progressing shallowly, and the specification's own
priority ordering — "correctness over speed, honesty over polish" — argues for one
platform done properly over two done partly.

## Decision

1. **`android/` is not created until the desktop side has a working playback core
   (Phase 1 gates green).** An empty Gradle skeleton would turn a red gate green
   without making the Android app any closer to existing, which is the exact
   substitution this ADR is written to avoid.
2. **`android-ci.yml` is not written yet either**, for the same reason: a workflow
   with nothing to build is a green check that means nothing.
3. **`shared-spec/` is built out in full, immediately, ahead of both
   implementations.** This is the load-bearing consequence of the decision. The
   455 conformance fixtures, the five schemas, the two grammars, the design tokens
   and the sync protocol are all complete and verified, so the Android
   implementation, when it starts, *consumes* a contract rather than negotiating
   one — and its conformance verdicts are graded against recorded expectations
   rather than against whatever the desktop engine happens to do.
4. **The gap is recorded in three places that a reader will actually encounter:**
   this ADR, `docs/OPEN-QUESTIONS.md`, and the "Not yet true" section of
   `CHANGELOG.md`. `docs/PARITY.md` carries it as an unimplemented row rather than
   an omission.

## Consequences

**Unmet requirements, stated exactly:**

| Requirement / gate | Status | Why |
|---|---|---|
| `REQ-GEN-030` — repository layout | **Unmet** | `android/` absent |
| Phase 0 exit gate 2 — `android-ci.yml` green, APK on emulator | **Red** | no Android app, no workflow |
| `REQ-GEN-031` — desktop and Android agree on conformance fixtures | **Half-proven** | one engine can conform; agreement needs two |
| `REQ-AUD-108` — desktop/Android DSP within −90 dBFS | **Untestable yet** | one side of the comparison is missing |
| `REQ-LIB-001` — schema change applied to both platforms in one commit | **Unenforceable in one direction** | there is no second platform to fail the gate |
| Phase 0 exit gates 1 and 7 — window opens, version shown in About | **CI-only** | Qt is not installable on the development machine; see `docs/OPEN-QUESTIONS.md` |

**Phase 0 is therefore not complete.** It is complete for the desktop scope. Under
§28's own rule — a phase is complete only when every gate is green on every
platform — Phase 1 work proceeds with Phase 0 partially green, which is a
deviation from the sequencing rule and not merely from a gate. It is recorded here
rather than resolved, because resolving it means writing the Android app.

**Positive.** The desktop engine gets built to the specification's actual depth:
sample-exact gapless, RT-safety under TSan, zero allocations in the callback,
verified DSP coefficients. Those are the claims that are hard to retrofit and easy
to fake, and they are the ones §8.11 demands proof of.

**Positive.** `shared-spec/` being finished first inverts the usual failure mode.
The common risk in a two-platform project is that the first implementation becomes
the de facto specification and the second is judged by matching it. With the
fixtures recorded and verdicts pinned in advance, both implementations are judged
against the same external artefact — which is what `REQ-GEN-031` is actually for.

**Negative.** The desktop implementation will make assumptions that only become
visible when a second implementation disagrees. Some of those will turn out to be
wrong, and finding them late is more expensive than finding them early. Partly
mitigated by the conformance corpus, which is where such assumptions get pinned;
not mitigated at all for anything the corpus does not cover.

**Negative.** The Android app's absence makes several requirements untestable
rather than merely unimplemented — the parity ones especially. A claim that cannot
be tested is a claim that should not be made, and the parity documents say so.

**Negative.** `REQ-GEN-030` stays unmet until `android/` exists, so a strict
reading of §0.3 means 1.0.0 cannot be tagged. That is the correct reading and is
not being worked around.

## Alternatives considered

**Create an empty `android/` skeleton to satisfy the layout requirement** —
rejected, and worth naming because it is the tempting option. An empty Gradle
project with a Compose "hello" screen would turn gate 2 green and let
`REQ-GEN-030` be reported as met. It would also make the repository *look*
two-platform while `REQ-GEN-031`, `REQ-AUD-108` and `REQ-LIB-001` remained exactly
as unproven as they are now — a green check standing in for the thing it was
supposed to measure. Gates exist to protect the next phase's assumptions; one that
passes without protecting anything is worse than a red one, because a red gate
still tells the truth.

**Build both platforms in parallel from Phase 0** — the specification's intent, and
rejected on capacity grounds rather than on merit. It would produce two shallow
implementations, and the audio claims in §8.11 are not the kind that survive
shallow implementation.

**Android first, desktop second** — rejected. The desktop engine is where the
specification is most demanding (bit-perfect output, sample-exact gapless, the
sink layer that exists precisely because general-purpose wrappers cannot do this
per ADR 0002). Building the harder side first means the shared contract is shaped
by the harder constraints. Doing it the other way risks a contract that Media3's
defaults can satisfy and native sinks cannot.

**Drop Android from the specification** — rejected outright. Android Auto and
platform parity are core to the product vision (§2.1, §15), the sync module exists
to connect the two (§18), and `shared-spec/` only earns its existence if something
else consumes it. Removing Android would be a specification change requiring its
own ADR and a rewrite of §15, §29.2 and §18 — not a scoping decision.

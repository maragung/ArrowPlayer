# ADR 0012 — Android restored, superseding the desktop-first deferral

- **Status:** Accepted
- **Date:** 2026-08-27
- **Supersedes:** [ADR 0011](0011-desktop-first-sequencing.md)
- **Requirements:** REQ-GEN-030, REQ-GEN-031, REQ-BLD-020

## Context

ADR 0011 deferred the Android target: no `android/` tree and no
`android-ci.yml` until the desktop playback core existed, on capacity grounds.
That decision is now revoked, and this ADR exists because §0.1 rule 2 requires
a deliberate specification change to be recorded as one — revoking a decision
by editing the tree in silence would be the same shape of downgrade ADR 0011
was written to refuse.

What changed since ADR 0011:

- The desktop Phase 0 foundation is complete for the desktop scope: 210 tests,
  all architecture gates green, the Qt shell written. The remaining Phase 0
  gaps are CI-side (nothing has been pushed), not design-side.
- A maintainer decision was made to build both platforms in parallel after
  all. The capacity argument in ADR 0011 was about *doing both shallowly*; the
  alternative that now holds is a deliberately minimal Android app — a Gradle,
  Kotlin and Compose scaffold that builds, installs and shows its identity —
  grown toward the specification module by module, rather than two platforms
  half-implemented.

## Decision

1. **`android/` is created now** as a self-contained Gradle build producing a
   working (if minimal) Compose app: versioned via the same
   `desktop/version.txt`-style source of truth, with an About view showing the
   git-generated version, matching Phase 0's exit gate 7 on the Android side.
2. **`android-ci.yml` is written now** and runs on `android/**`,
   `shared-spec/**` and itself: JDK 21, Gradle cache, `ktlint`, `detekt`,
   `assembleDebug`, unit tests, debug-APK upload. Phase 0 exit gate 2 ("APK
   builds and installs on an emulator") is served in stages: the build +
   install-on-emulator half is a `reactivecircus/android-emulator-runner` job
   on the CI schedule; the scaffold itself is verified by `assembleDebug` on
   every push. (The emulator job is the first thing scaled back if runner
   time becomes the constraint.)
3. **The conformance agreement gate (`REQ-GEN-031`) gains its second
   implementation** incrementally: the Android app consumes the
   `shared-spec/` fixtures through its own validator rather than by calling
   into `desktop/`, exactly as §5's platform-isolation rule requires.
4. **ADR 0011 stays on the record, superseded rather than deleted.** Its
   reasoning — one platform done properly over two done partly — is the
   reasoning this ADR reverses, and a future reader needs both sides of the
   decision. Its consequence table is corrected by this ADR's existence, not
   rewritten: the two platforms are now being built in parallel again.

## Consequences

**Met requirements, stated exactly:**

| Requirement / gate | Status | Why |
|---|---|---|
| `REQ-GEN-030` — repository layout | **Partially met** | `android/` and `android-ci.yml` now exist; the full §5 module list is not yet implemented |
| Phase 0 exit gate 2 — `android-ci.yml` green, APK on emulator | **In progress** | the workflow exists; the first green run is CI's to demonstrate |
| `REQ-GEN-031` — desktop and Android agree on conformance fixtures | **Still half-proven** | the Android validator is not written yet; nothing here claims it is |
| Phase 0 exit gates 1 and 7 — window opens, version shown in About | **CI-only** | unchanged from ADR 0011; Qt is not installable on the development machine (OQ-017) |

**Positive.** `REQ-GEN-031` and `REQ-AUD-108` become testable on a real second
implementation sooner than the desktop-first sequencing promised.

**Negative.** Two toolchains and two CI pipelines must now be kept in step by
the same-commit rules (`REQ-LIB-001`), which is exactly the capacity cost ADR
0011 chose to defer. It is accepted deliberately rather than ignored.

**Negative.** The Android scaffold's dependency catalogue (Gradle
version-catalog entries) is not yet reconciled with the §4.2 register. The
register's Android half lists NDK components the scaffold does not use; a
future commit reconciles the two, and until it does the register note says so.
Recorded as part of the OQ-018 rework rather than left silent.

## Alternatives considered

**Keep ADR 0011 and skip Android** — rejected by the maintainer's explicit
decision to release an Android APK alongside the desktop binaries.

**Port the desktop Qt app to Android via Qt-for-Android** — rejected. §2.1 and
§15 specify a native Android app (Kotlin/Compose) with full Android Auto
integration; a Qt APK would be a different product that does not satisfy
`REQ-GEN-031`'s two-implementation contract.

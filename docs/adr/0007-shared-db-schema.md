# ADR 0007 — One database schema for SQLite and Room

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-LIB-001, REQ-LIB-050, REQ-GEN-031, REQ-SYN-001

## Context

Both platforms keep a local index of the user's library. Desktop uses SQLite
directly; Android uses Room, which is a compile-time-checked wrapper over SQLite.
The same database engine sits underneath both, but the two are described in
different languages: DDL in one, annotated Kotlin data classes in the other.

The tempting arrangement is to let each platform model its own storage and
translate at the seams. It is tempting because each side then gets idiomatic
code, and because nothing forces the two to be written at the same time.

It fails at the sync module (§18). Sync computes a difference between two devices'
libraries and applies it. If the two sides have different models, sync is a
translation layer: every field needs a mapping, every mapping needs a decision
about what to do when the shapes disagree, and every one of those decisions is a
place where a user's play count or rating quietly changes meaning as it crosses
between their laptop and their phone. Conflict resolution (§18.3) becomes
ambiguous, because "the same row" is no longer a well-defined phrase.

It also fails the conformance strategy. `REQ-GEN-031` makes shared fixtures the
mechanism by which "shared format" is proven rather than asserted. Smart-playlist
fixtures compile rules to SQL; that SQL has to run against a real schema. If the
schemas differ, the fixtures test two different things and prove nothing about
agreement.

## Decision

**§9.4 defines one schema, normative for both platforms.** SQLite DDL is
canonical. Room entities mirror it exactly — same table names, same column names,
same types, same nullability, same defaults, same indices, same semantics, same
migration numbers.

- **The DDL in §9.4 is the source of truth.** Room's `@Entity` annotations are
  written to match it, not the other way round. Where Room's conventions would
  produce a different name, the annotation overrides Room, not the schema.
- **Migrations are numbered once**, and migration *N* means the same thing on both
  platforms. A device at schema version 7 is at schema version 7 regardless of
  which app wrote it.
- **`REQ-LIB-001` makes this mechanical:** a schema change MUST be applied to both
  platforms in the same commit, with matching migration numbers, or CI fails. Not
  "should be kept in sync" — a gate.
- **Room's schema export is the assertion mechanism.** Room can emit its resolved
  schema as JSON; CI compares that against the canonical DDL. A drift is a build
  failure with a diff, rather than a mystery that surfaces during sync months
  later.

## Consequences

**Positive.** Sync becomes a diff over a shared model rather than a translation
layer — the change sets in §18 describe rows in a schema both ends already agree
on. Conflict resolution can talk about columns without qualifying which platform's
column it means.

**Positive.** The `shared-spec/conformance/` fixtures can validate both sides.
Smart-playlist rules compile to SQL that runs unchanged on either platform, which
is what makes `REQ-PLS-013` (identical results from identical rules) testable.

**Positive.** One schema to review means one place where a column's meaning is
decided, and one place to look when the meaning is disputed.

**Negative.** Neither side gets to be fully idiomatic. Room developers will find
some naming and nullability choices unnatural, because they were made for SQLite's
DDL first. Accepted: the cost is paid by two developers reading annotations; the
alternative is paid by every user whose data crosses devices.

**Negative.** A schema change becomes a two-platform commit, which is slower and
harder to land — especially while `android/` does not yet exist. During that
period the constraint is unenforceable in one direction, which is recorded in
ADR 0011 and `docs/OPEN-QUESTIONS.md`. The DDL is written to the shared standard
regardless, so the Android side inherits a schema rather than negotiating one.

**Negative.** Room's own migration testing utilities expect Room-authored
schemas. Mirroring hand-written DDL means the Android migration tests have to be
written against the exported schema JSON rather than generated. Accepted; it is a
one-time harness cost.

## Alternatives considered

**Independent schemas with a translation layer at sync** — rejected for the
reasons above: it moves an ambiguity that would be decided once, in the schema,
into code that must decide it on every sync, for every field, forever.

**Generate both from a single IDL** (a schema definition compiled to DDL and to
Kotlin) — genuinely attractive, and rejected as premature. It adds a code
generator and a build step to solve a problem that §9.4 plus a CI comparison
solves with no new tooling. Worth revisiting if the schema grows past the point
where a human comparison is reliable — which the CI comparison, not a person, is
already handling.

**Room as canonical, DDL generated from it** — rejected: it makes the Android
implementation the specification, which inverts the platform relationship (§5:
neither platform imports the other; anything shared is data in `shared-spec/`),
and it would make the desktop schema dependent on a Kotlin toolchain.

**A server-side database as the shared truth** — rejected outright. The product is
offline-first and standalone with no accounts (§2.1); sync is optional,
peer-to-peer, and never required (`REQ-SYN-013`). A shared server would invert the
entire privacy model to solve a schema-consistency problem.

# ADR 0003 — No executable code in skin packages

- **Status:** Accepted
- **Date:** 2026-08-23
- **Requirements:** REQ-THM-001 … REQ-THM-003, REQ-THM-025 … REQ-THM-034, REQ-SEC-005
- **Supersedes:** the "custom QML layout regions" design in specification v1.x

## Context

The skin engine is differentiator #1 (§2.2). Specification v1.x proposed
user-installable skin packages that could contain *"optional custom QML layout
regions"*, together with an in-app skin browser and installer.

Those two features together mean: **download an arbitrary file from the internet
and execute the code inside it.** QML is Turing-complete with JavaScript
semantics; a QML document can call `Qt.createQmlObject`, reach the filesystem and
the network through its imports, and in some configurations load native plugins.

For an application whose headline promise is privacy (§2.2, §19.5), shipping a
one-click installer for arbitrary code is indefensible. Winamp's modern skins
shipped a scripting language (Maki) and that ecosystem was never audited for it;
we are not repeating that.

## Decision

**No skin or theme package may contain executable code.** Customisation is split
into two tiers, neither of which is Turing-complete:

- **Tier 1 — Theme.** Design tokens only: colour, typography, spacing, radii,
  elevation, motion, opacity. Pure data, validated against `theme-schema.json`.
- **Tier 2 — Skin.** A Theme plus declarative layout documents (`.eclayout`),
  icons, images and fonts.

The layout DSL is deliberately constrained:

- a **closed component vocabulary** (REQ-THM-026) — adding a component is a
  schema-version change, not something a skin can do;
- **read-only bindings** into a whitelisted presentation-state model
  (REQ-THM-027) — a skin cannot reach settings, the library, or the filesystem;
- **enum-only actions** (REQ-THM-028) — a skin references a command by name from
  the central registry, and an unknown name fails at install time;
- **no arithmetic or expressions** beyond Arrow Format Strings (§10, itself
  total and output-capped) and a one-level `when:` predicate (REQ-THM-030);
- **resource budgets** — 500 components, depth 24, 64 bindings (REQ-THM-033).

## Why this is not merely a restriction

The overwhelming majority of what skin authors actually do is rearrange, resize,
restyle, hide and swap imagery. The declarative tier covers all of that, and
Tier 2 is what makes the three built-in skins visibly different products rather
than recolours of one layout.

Authors who genuinely want new *interactive behaviour* are served by the plugin
SDK (§16), which is a separate, explicit, informed trust decision with a
capability prompt — not something that happens because someone applied a
pretty theme.

## Consequences

**Positive.** The design goal becomes achievable and testable: *installing a
malicious skin can, at worst, produce an ugly or non-functional UI.* Never code
execution, never data exfiltration, never a hang. REQ-SEC-006 requires an
adversarial corpus (zip bombs, traversal paths, scripted SVG, XML entity attacks,
10 MB format strings, 100k-node layouts) and every entry must be rejected with a
specific error.

Because there is no code to sandbox, **skin signing is unnecessary**
(REQ-THM-043). The gallery's "Verified" badge is about provenance and taste, not
safety — and the documentation must say so explicitly, so nobody reads
"unverified" as "dangerous".

**Negative.** Some layouts expressible in QML are not expressible in the DSL.
Authors wanting custom animation curves per element, or novel interactions, will
hit the wall. This is accepted, and `docs/SKIN-AUTHORING.md` must explain the
reasoning rather than presenting the limit as arbitrary — an author who
understands why will work with the constraint instead of fighting it.

**Negative.** We now maintain a layout interpreter on each platform, and the
component vocabulary needs deliberate curation. Mitigated by keeping the
vocabulary small and versioning the schema.

## Alternatives considered

**Signed QML from a first-party channel only**, with user-installed packages
restricted to tokens. Rejected: it requires a signing key, a trust store and a
review pipeline, and it creates two classes of author — the interesting layouts
would all live behind a gate, which defeats the point of a skin ecosystem.

**QML with a best-effort sandbox.** Rejected: reliably sandboxing QML is an
unsolved problem, and "best effort" security in a privacy-first player is worse
than an honest limitation, because it invites trust the implementation cannot
justify.

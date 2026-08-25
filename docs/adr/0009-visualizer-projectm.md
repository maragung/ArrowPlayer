# ADR 0009 — projectM for MilkDrop preset compatibility

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-UIX-040, REQ-GEN-012

## Context

Visualizers are not a decorative afterthought for this product category. MilkDrop
and its predecessor Geiss were the two most-downloaded Winamp plugins ever —
roughly 4.7 million and 2.7 million downloads. §2.3 identifies the plugin
ecosystem as the reason Winamp outlived its owner, and the visualizer ecosystem
was the most visible part of it.

That ecosystem is two decades of `.milk` presets written by people who are not
going to rewrite them for a new player. The question is not "how do we draw
something that reacts to audio" — that is a weekend — but "can a user bring their
preset collection".

## Decision

**Embed projectM 4.x** (LGPL-2.1, C++/OpenGL) as the advanced visualizer engine,
alongside three lightweight native visualizers (spectrum, oscilloscope, VU) that
have no external dependency.

- projectM is an optional component. Its absence compiles out the advanced
  visualizer and leaves the three native ones, consistent with the
  no-mandatory-dependency build model in `docs/BUILDING.md`.
- LGPL-2.1 fits the §4.2 dependency register without qualification, and is
  dynamically linked like every other LGPL component (`REQ-GEN-013` establishes
  the pattern for Qt; the same rules apply here).
- Preset files are **untrusted input** (§21.1). A `.milk` preset is a program in
  a small expression language, and projectM evaluates it. It is treated with the
  same suspicion as a skin package: parsed under the resource limits of §21.2,
  and never granted filesystem or network reach.

## Consequences

**Positive.** A user's existing preset collection works. That is a feature no
amount of bespoke visualizer work can produce, because the value is in the
collection, not the renderer.

**Positive.** projectM already ships inside Qmmp, Clementine, and Poweramp on
Android, so it is proven on both of our target platforms rather than only on the
desktop one. For a project committed to platform parity (§29.2) that is decisive:
a visualizer that only worked on desktop would be a parity deviation on the most
visible surface in the app.

**Positive.** Not writing a preset language, a preset renderer and a preset
ecosystem is a large amount of work not done.

**Negative.** An OpenGL dependency and a substantial C++ library in the graph,
with its own CVE surface and its own build quirks. Mitigated by making it
optional, pinning it like every other dependency (`REQ-SEC-013`), and treating
presets as untrusted.

**Negative.** projectM's rendering is not bit-identical across platforms and
driver stacks, so "same preset, same output" is a weaker promise than the
sample-exact promises made about audio. Recorded as a parity note rather than
implied to be exact.

**Negative.** GPU-driven visualization conflicts with the battery and thermal
budgets (§20.7) on Android. The advanced visualizer is therefore off by default
on battery and frame-limited, which is a behavioural difference from desktop that
belongs in §29.2.

## Alternatives considered

**A bespoke visualizer framework only** — rejected: it produces three or four
visualizers and no ecosystem. The three native visualizers ship anyway, precisely
because they are cheap and dependency-free; they are the fallback, not the
strategy.

**Butterchurn** (MIT, a JavaScript/WebGL MilkDrop reimplementation) — rejected on
runtime grounds. It is the wrong runtime for a C++ desktop app and a Kotlin
Android app; embedding it would mean shipping a browser engine to draw a
visualizer, which is disproportionate on desktop and unacceptable against the
Android binary-size budget (§20.6).

**Writing a `.milk` interpreter ourselves** — rejected: preset compatibility means
bug-compatibility with two decades of presets that were written against
MilkDrop's actual behaviour, not against a specification. projectM has already
paid that cost.

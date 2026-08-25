# Typography

Canonical values: [`tokens.json`](tokens.json) → `typography`.
Specification: `eclipse-player.md` §12.1 (`REQ-UIX-001`).

This document explains the type system. It does not restate the numbers, because
`REQ-UIX-001` requires both platforms to read them from `tokens.json` rather than
duplicate them — and a number duplicated into prose is a number that will drift.

## The scale

Seven styles on a 14 px base with a 1.2 ratio: `display`, `headline`, `title`,
`body`, `label`, `caption`, `mono`. There is no eighth. A design that needs one
is a design that has not decided what it is saying; the layout DSL enforces this
by accepting only `typography.scale.<one of those seven>` as a `style` value
(`REQ-THM-028`), so a skin cannot invent a size.

Each style carries four numbers — size, line height, weight, letter spacing —
and they travel together. Taking a size without its line height is how a list
row ends up with text that touches its neighbour.

Letter spacing is negative at display and headline sizes and positive below
body. Large text at default tracking looks loose; small text at default tracking
looks cramped. The values compensate rather than express a preference.

## What each style is for

| Style | Use | Not for |
|---|---|---|
| `display` | The one thing a full-screen surface is about — a Now Playing title | Anything that can appear twice on screen |
| `headline` | Section and dialog headings | Emphasising a body paragraph |
| `title` | Card titles, panel headers, the playlist row's primary line | Making a row taller than it needs |
| `body` | Default text: descriptions, prose, list secondary lines | Dense tabular data |
| `label` | Buttons, tabs, form labels, column headers | Sentences |
| `caption` | Timestamps, counts, helper text, metadata chips | Anything a user must read to operate the app |
| `mono` | Durations, sample rates, bit depths, file paths, log lines | Prose that happens to be technical |

`mono` earns its place in a music player: a duration column in a proportional
font makes `1:09` and `1:41` different widths, so the digits jitter as the track
changes. The same applies to the `96000 Hz / 24-bit` badge and to log output.

## Weight

Four weights are used: 400 (body, caption, mono), 500 (label), 600 (title,
headline), 700 (display). Synthetic bold is forbidden — if a font stack lacks a
weight, the next available real weight is used, because a faux-bolded glyph at
12 px is a smear. Latin-script and CJK stacks are declared separately in the
theme's `typography.fontStack` for the same reason: a Latin font asked to render
CJK falls back per-glyph and produces visibly mixed baselines.

## Scaling

`REQ-UIX-059` requires the UI to stay usable at **200 %** text scale without
clipping essential text, and `accessibility.textScale` (0.8–2.0) multiplies the
sizes above. Two consequences bind every layout:

- Never encode a height in text-size units and never hard-code a row height that
  a scaled line height can outgrow. Layouts reflow; they do not overflow.
- Truncation is a rendering decision, never a data one. A truncated title still
  exposes its full value to the accessibility layer and to tooltips
  (`REQ-UIX-063` requires the tooltip and accessible name be the same string).

Marquee is not a substitute for reflow. It is for one line that is genuinely
longer than any reasonable width — a long track title — and it must stop when
reduced motion is set (`REQ-UIX-060`).

## Contrast

Type never carries meaning by colour alone (`REQ-UIX-058`), and every
(text, background) pair a theme can produce is checked against WCAG 2.2 AA by
the loader (`REQ-THM-041`): 4.5 : 1 for normal text, 3 : 1 for large text, where
"large" means ≥ 18.66 px regular or ≥ 14 px bold. Read against the scale, that
threshold covers `display` and `headline` at default scale and nothing else — so
`title` and below must clear the full 4.5 : 1. Theme authors routinely get this
wrong by assuming their 18 px `title` counts as large text. It does not.

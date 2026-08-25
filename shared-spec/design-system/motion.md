# Motion

Canonical values: [`tokens.json`](tokens.json) → `motion`.
Specification: `eclipse-player.md` §12.1 (`REQ-UIX-001`, `REQ-UIX-002`).

## The rule

> Animation exists to explain a change of state, never to decorate.
> — `REQ-UIX-002`

That is not a stylistic preference, it is the acceptance criterion. An animation
that cannot name the state change it explains does not ship.

## Durations

Four, and only four: `instant` (80 ms), `fast` (150 ms), `normal` (250 ms),
`slow` (400 ms). Each has one job, recorded in `tokens.json` as a `use` string
next to the number so the pairing cannot be lost:

- **instant** — press feedback. Below roughly 80 ms a transition reads as a
  glitch rather than a response; above it, the button feels late.
- **fast** — hover, small state changes, a checkbox, a chip.
- **normal** — panels and view transitions, and the skin cross-fade. This is the
  longest duration a user will sit through repeatedly without it feeling slow.
- **slow** — full-screen transitions only, which happen rarely enough to afford
  the extra 150 ms of legibility.

Anything longer than `slow` is a bug. Anything between two tokens is a bug: an
author who wants 300 ms is choosing between `normal` and `slow`, not adding a
fifth token.

## Easing

| Token | Curve | When |
|---|---|---|
| `standard` | `cubic-bezier(0.2, 0, 0, 1)` | Almost everything: elements moving or changing within the view |
| `decelerate` | `cubic-bezier(0, 0, 0.2, 1)` | Something entering — it arrives and settles |
| `accelerate` | `cubic-bezier(0.4, 0, 1, 1)` | Something leaving — it departs and is gone |
| `emphasized` | `standard`, paired with `slow` | The one transition on a surface that carries the meaning |

`emphasized` is deliberately not a fifth curve; it is `standard` plus a duration
commitment. Encoding it that way in `tokens.json` (as a `pairedDuration` field
rather than as new control points) makes it impossible to use the emphasis
without accepting the duration that makes emphasis legible.

Linear easing appears nowhere in the UI. It is correct for exactly two things,
neither of which is UI motion: a progress bar advancing against real time, and
an audio fade whose curve is fixed by `REQ-AUD-051` (equal-power or linear as
that requirement specifies — audio fades are a signal-processing concern and
take no tokens from this file).

## Forbidden outright

`REQ-UIX-002` names these, and CI review treats them as defects rather than
taste disputes:

- animated album-art parallax on scroll
- bouncing playback buttons
- animated gradients behind text
- **anything that animates continuously while idle**

The last one is the load-bearing prohibition. A player sits idle on screen for
hours; a continuously animating element is a permanent, unrequested draw on the
GPU, the battery, and the user's peripheral attention. The visualizer is the
single exception, because motion is its entire function — and even it is clamped
to at most 3 flashes per second (`REQ-UIX-062`) and stops under reduced motion.

## Interruptibility

Every animation MUST be interruptible (`REQ-UIX-002`). If a user presses *next*
twice quickly, the second press does not queue behind an animation, and it does
not restart the transition from the beginning — it retargets from wherever the
current value happens to be. An animation that must complete before the next
input is accepted has turned a decoration into latency.

## Reduced motion

`REQ-UIX-060` requires honouring the OS setting — Windows "Show animations",
GNOME `gtk-enable-animations`, Android "Remove animations" — and the app adds
its own three-state override (`accessibility.reducedMotion`: `system` /
`always` / `never`, default `system`). When reduced motion is in effect:

- all non-essential animation is disabled
- cross-fades become instant swaps, including the skin cross-fade
- continuous visualizer motion stops unless the user explicitly re-enables it

"Disabled" means the end state is applied immediately, not that the duration is
shortened to something small. A 20 ms animation is still animation, and for a
user with a vestibular disorder it is still a symptom trigger.

Skins participate in this: the layout DSL exposes `settings.reducedMotion` as a
readable state path so a skin can present a static arrangement, and a skin has
no way to opt out of the setting — there is no token, binding, or action that
re-enables motion.

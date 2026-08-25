# Iconography

Canonical values: [`tokens.json`](tokens.json) → `spacing`, and the theme
schema's `icons` object ([`../schemas/theme-schema.json`](../schemas/theme-schema.json)).
Specification: `eclipse-player.md` §12.1, §11.2, `REQ-THM-042`, `REQ-UIX-019`,
`REQ-UIX-063`.

## Vector only

`REQ-UIX-019` requires the UI to be correct at 100, 125, 150, 175, 200, and
250 % scaling, including a window dragged between mixed-DPI monitors, and states
plainly that **all iconography MUST be vector**. There is no raster icon
fallback and no `@2x` set to maintain. A bitmap icon is a decision to look wrong
on some display the user owns.

Packaged icons are therefore SVG (`REQ-THM-015` allows only SVG under a skin's
`icons/`), and every one is sanitised before it renders (below).

## The theme's icon controls

A theme does not ship geometry; it selects and adjusts a set:

| Token | Range | Meaning |
|---|---|---|
| `icons.setId` | `^[a-z0-9-]{1,64}$` | Which installed set to use |
| `icons.style` | `outline` / `filled` / `duotone` | Which variant within the set |
| `icons.strokeWidth` | 0.5 – 4 | Outline weight, in the set's own units |
| `icons.sizeScale` | 0.5 – 2 | Multiplier on the layout's declared `iconSize` |

A set is expected to provide every style it declares. A theme selecting
`duotone` from a set that only draws outlines gets outlines, not a validation
failure — a missing variant is a quality problem for the set's author, not a
reason to refuse to render the user's music player.

`icons.sizeScale` multiplies rather than replaces, so the layout keeps control
of relative sizing (a transport icon stays larger than a list-row icon) while
the theme keeps control of overall weight.

## Sizing

Icon sizes are drawn from the 4 px spacing grid: 16, 20, 24, 32, 48. 24 is the
default and the value the layout DSL's `iconSize` property uses when unset.

Optical size is not the same as box size. A 24 px box holding a glyph drawn to
20 px of visual weight sits correctly beside 24 px text; the same glyph drawn to
fill its box reads as too heavy. Sets are authored to a consistent optical size
within a nominal box, which is why mixing sets inside one surface looks wrong
even when every icon is nominally the same size.

Touch targets are a separate concern from icon size: `REQ-UIX-061` requires
≥ 44 px on touch-capable surfaces and ≥ 24 px on pointer-only surfaces. A 24 px
icon inside a 44 px hit area is correct and common; a 44 px icon is not.

## Sanitisation is not optional

Every SVG that enters the app from a package is sanitised (`REQ-THM-042`).
Stripped, and the package rejected if present:

- `<script>`, `<foreignObject>`
- `<use>` with an external reference
- `<image>` with a non-`data:` href
- any `on*` event attribute
- any `href` / `xlink:href` that is not an internal fragment
- `<style>` containing `@import`
- any external entity or DOCTYPE declaration

Plus: the element count is capped at 10,000, and parsing runs with entity
expansion disabled — the billion-laughs defence.

This is the concrete form of the promise in ADR 0003 that a skin package
contains no executable code. SVG is the one packaged format that *can* carry
script, so it is the one place where that promise has to be enforced by a parser
rather than by the absence of a feature. `shared-spec/conformance/theme-validation-cases/malicious/`
holds fixtures for each of these, and both platforms MUST reject all of them
identically (`REQ-GEN-031`).

## Meaning

- **Never colour alone.** `REQ-UIX-058`: playback state, selection, ratings, and
  error states each need a non-colour indicator. An icon is often that
  indicator, which means the icon must differ in *shape*, not merely in tint —
  a red circle and a green circle are the same icon to a large minority of
  users.
- **Every icon-only control has a tooltip and an accessible name, and they are
  the same string** (`REQ-UIX-063`). One string, one place, so the two cannot
  drift apart as the UI is edited.
- **No trademarked or copied glyphs** (`REQ-GEN-022`). Built-in sets are
  original work. A play triangle is not anyone's property; a specific player's
  logo is.

## Icon packs

`REQ-THM-063` `[v1.x]` makes an icon set installable on its own as
`.eclipseicons`, following AIMP's addon taxonomy, so a set can be reused across
themes. Until then a set ships inside a skin package. The `icons.setId` token is
already the indirection that will make the split a non-breaking change, which is
why it exists in v1.0 rather than being added later.

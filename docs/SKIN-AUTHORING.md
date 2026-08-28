<!-- SPDX-License-Identifier: MPL-2.0 -->

# Skin authoring

This is the author's reference for Arrow Player's two customisation tiers: how
to write a **theme**, how to build a **skin**, what the validator will and will
not accept, and why the format refuses to run your code.

It is a specification document as much as a tutorial. Everything here is
traceable to a requirement in [`eclipse-player.md`](../eclipse-player.md) §10–§12
and, more usefully, to a schema under [`shared-spec/`](../shared-spec/), which is
the artefact that actually decides what validates. Where this document and a
schema disagree, **the schema is right** and this document has a bug — say so in
an issue.

One thing to read before anything else: much of what is described here is
specified and validated but **not yet rendered**. There is no skin loader, no
layout interpreter, and no `tools/theme-validate` binary in this tree yet. The
[last section](#what-does-not-exist-yet) is an honest inventory of the gap. The
formats are stable enough to author against; the runtime is not there to author
*for*.

## Contents

- [The two tiers](#the-two-tiers)
- [Why no code — the whole argument](#why-no-code--the-whole-argument)
- [Tier 1 · the theme](#tier-1--the-theme)
- [Tier 2 · the skin package](#tier-2--the-skin-package)
- [The layout DSL](#the-layout-dsl)
- [Bindings — the complete state whitelist](#bindings--the-complete-state-whitelist)
- [Actions — the complete enum](#actions--the-complete-enum)
- [`when` — conditional visibility](#when--conditional-visibility)
- [EFS in a skin](#efs-in-a-skin)
- [Validation — the ten steps](#validation--the-ten-steps)
- [Reading a validation error](#reading-a-validation-error)
- [Contrast is enforced, not requested](#contrast-is-enforced-not-requested)
- [Trust, and what "Verified" does not mean](#trust-and-what-verified-does-not-mean)
- [Packaging a skin](#packaging-a-skin)
- [Compatibility and versioning](#compatibility-and-versioning)
- [What does not exist yet](#what-does-not-exist-yet)

## The two tiers

`REQ-THM-001` fixes the number of tiers at exactly two.

| | **Tier 1 — Theme** | **Tier 2 — Skin** |
|---|---|---|
| Contains | Design tokens only | A theme, **plus** layouts, icons, images, fonts |
| Format | `theme.json` | a `.arrowskin` ZIP package |
| Changes | Colour, type, spacing, radii, elevation, motion, opacity | all of Tier 1, **plus** arrangement, sizing, visibility, which surfaces exist |
| Code | none possible | **none possible** |
| Trust | safe to apply from any source | safe to apply from any source, after validation |
| Platforms | desktop and Android | desktop `[v1.0]`, Android `[v1.x]` |

The split exists so that the common case stays trivial. Changing the accent
colour should not require you to learn a layout language, and `REQ-THM-011`
guarantees a five-line theme is a legitimate theme:

```json
{
  "schemaVersion": 1,
  "id": "my-accent",
  "name": "Just The Accent",
  "version": "1.0.0",
  "mode": "dark",
  "extends": "arrow-dark",
  "color": { "accent": { "base": "#E85D75" } },
  "typography": {}
}
```

Everything unset inherits — from `extends` if present, otherwise from the
built-in Dark or Light theme according to `mode`.

`REQ-THM-003` requires the *user*-facing difference to be visible too: applying a
theme is one click with no warning, while installing a skin first shows what the
package contains. That is a promise made to the person installing your work, and
it is why `capabilities` in the manifest is load-bearing rather than decorative
(see [the manifest](#manifestjson)).

## Why no code — the whole argument

`REQ-THM-002` is blunt: **neither tier may contain executable code.** No QML, no
JavaScript, no Maki-style script, no shader source, no expression language beyond
EFS (§10) and the restricted `when:` grammar. It is recorded as
[ADR 0003](adr/0003-no-code-in-skins.md), which means it is not a default someone
can quietly relax later.

`REQ-THM-002` also requires the reasoning to appear *here*, so that authors
understand the constraint instead of fighting it. Verbatim from the
specification, because paraphrasing an argument like this is how it gets weaker:

- QML is Turing-complete and has JavaScript semantics. A `.arrowskin`
  containing QML could read the user's filesystem, open network sockets,
  exfiltrate the library database, or execute native code through imports.
  Sandboxing QML reliably is an unsolved problem, and "best effort" sandboxing in
  a privacy-first player is worse than an honest limitation.
- Winamp's modern skins shipped a scripting language (Maki). That is exactly the
  capability we are refusing, and Winamp's skin ecosystem was never audited for
  it.
- Declarative layout with a fixed component vocabulary covers the overwhelming
  majority of what skin authors actually do: rearrange, resize, restyle, hide,
  and swap imagery. It does not cover authors who want to invent new interactive
  behaviour — those authors are served by the **plugin SDK** (§16), which is an
  explicit, informed, separately-consented trust decision.

The practical consequence for you: a skin is *data*. It cannot compute, allocate
without a bound, loop, recurse, read a file it did not ship, or reach the
network. Every escape hatch you might reach for has been closed on purpose:

| You might want | Why it is refused | What to use instead |
|---|---|---|
| a literal `#RRGGBB` in a layout | it would break every theme the layout is combined with, and skip the contrast check | a `color.*` token reference |
| an arbitrary font size in a layout | same reason, one scale down | one of the seven `typography.scale.*` tokens |
| arithmetic on a bound value | expressions are a language, and a language is code | the closed EFS numeric functions |
| a shader or projectM preset | shader source is executable content | `style` on `Visualizer`, chosen from five renderers |
| an image from a URL | a network request from a downloaded package | a package-relative `images/…` reference |
| a new component | the vocabulary is closed by `REQ-THM-026` | compose the 30 that exist, or write a plugin |

## Tier 1 · the theme

Schema: [`shared-spec/schemas/theme-schema.json`](../shared-spec/schemas/theme-schema.json)
(`$id` `https://arrow-player.org/schemas/theme/v1`). It is JSON Schema draft
2020-12, and `REQ-THM-010` makes it the single source of truth for four
consumers: the desktop validator, the Android validator, the `tools/theme-validate`
CLI, and the skin editor. No implementation may accept a token the schema rejects
or reject one it accepts.

`additionalProperties` is `false` at every level, so a typo is an error rather
than a silently ignored key. Seven fields are required: `schemaVersion`, `id`,
`name`, `version`, `mode`, `color`, `typography` — and `color`/`typography` may
be as sparse as the example above, because the rest inherits.

### Identity and inheritance

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | `1` | a constant; bumping it is a `REQ-THM-052` migration |
| `id` | string | stable identity; two files with one `id` are one theme at two versions |
| `name` | string | display name |
| `author`, `license`, `homepage`, `description` | string | optional metadata; `license` is an SPDX id |
| `version` | `N.N.N` | strict three-part, no pre-release suffix |
| `minAppVersion` | `N.N.N` | refuse rather than half-load on an older app |
| `mode` | `light` \| `dark` | also chooses the inheritance base when `extends` is unset |
| `extends` | string | `id` of a built-in theme to inherit unset tokens from |

### `color` — eight groups

Every leaf is `#RRGGBB` or `#RRGGBBAA`, lower- or upper-case hex. There is no
named-colour syntax and no `rgb()` function.

| Group | Keys |
|---|---|
| `background` | `base`, `sunken`, `raised`, `overlay`, `scrim` |
| `surface` | `base`, `hover`, `pressed`, `selected`, `disabled` |
| `text` | `primary`, `secondary`, `tertiary`, `disabled`, `inverse`, `onAccent`, `link` |
| `accent` | `base`, `hover`, `pressed`, `subtle`, `muted` |
| `border` | `base`, `subtle`, `strong`, `focus` |
| `state` | `success`, `warning`, `error`, `info` |
| `playback` | `progress`, `progressTrack`, `buffered`, `waveform`, `waveformPlayed`, `peakMeter`, `peakMeterClip` |
| `visualizer` | `palette` (array of colours), `background` |

`color.playback` exists as its own group because a progress bar and a peak meter
are not "surfaces" and theming them through `accent` produced unreadable results
in every prototype. `color.text.onAccent` is the pair the contrast checker cares
about most — see [contrast](#contrast-is-enforced-not-requested).

### `typography`

| Field | Notes |
|---|---|
| `fontFamily.sans` / `.mono` / `.display` | a stack: 1–8 names, first available wins |
| `baseSize` | number; the canonical value is 14 px |
| `scale.<style>` | one of seven: `display`, `headline`, `title`, `body`, `label`, `caption`, `mono` |

Each `scale` entry requires `size` and optionally sets `lineHeight` (0.8–3, a
multiplier), `weight` (100–900), `letterSpacing` (−2–8 px) and `transform`
(`none`, `uppercase`, `lowercase`, `capitalize`). The canonical values live in
[`shared-spec/design-system/tokens.json`](../shared-spec/design-system/tokens.json)
— 14 px base, 1.2 ratio:

| Style | size | lineHeight | weight | letterSpacing |
|---|---|---|---|---|
| `display` | 34 | 1.15 | 700 | −0.5 |
| `headline` | 24 | 1.25 | 600 | −0.25 |
| `title` | 18 | 1.3 | 600 | 0 |
| `body` | 14 | 1.5 | 400 | 0 |
| `label` | 13 | 1.4 | 500 | 0.1 |
| `caption` | 12 | 1.35 | 400 | 0.2 |
| `mono` | 13 | 1.45 | 400 | 0 |

A layout may only name one of these seven. That is the whole reason the list is
short: an author who can set any size can produce an unreadable surface, and no
validator can tell the difference between "10 px on purpose" and a mistake.

### `shape`, `spacing`, `elevation`, `motion`, `opacity`

| Object | Keys | Range |
|---|---|---|
| `shape.radius` | `none` (const 0), `sm`, `md`, `lg`, `xl`, `full` (const 9999) | 0–64 for the four free ones |
| `shape.borderWidth` | `hairline`, `thin`, `thick` | 0–8 |
| `spacing` | `unit` (1–16, default 4), `scale` (4–16 numbers, each 0–256), `density` | `density`: `compact` \| `comfortable` \| `spacious` |
| `elevation` | array, ≤6 entries of `{offsetX?, offsetY, blur, spread?, color}` | offsets ±64, blur 0–128 |
| `motion.duration` | `instant`, `fast`, `normal`, `slow` | integers, ms |
| `motion.easing` | `standard`, `decelerate`, `accelerate`, `emphasized` | each a 4-number cubic Bézier, components −2–2 |
| `opacity` | `disabled`, `hover`, `pressed`, `scrim`, `ghost` | 0–1 |

Canonical spacing is `xs` 4, `sm` 8, `md` 12, `lg` 16, `xl` 24, `2xl` 32,
`3xl` 48, `4xl` 64; radii `sm` 4, `md` 8, `lg` 12, `xl` 16; durations
`instant` 80 ms, `fast` 150 ms, `normal` 250 ms, `slow` 400 ms. Note that
`motion.duration.normal` is also the skin cross-fade duration in `REQ-THM-050`,
so a theme that sets it to 0 makes skin switching instant rather than smooth —
which is legal, and is what reduced-motion does anyway.

`elevation` in a theme carries an explicit shadow `color`, unlike `tokens.json`,
which stores only `offsetY`/`blur`/`alpha` and takes the colour from the active
theme. A fixed shadow colour looks wrong in one of the two modes; a theme that
sets both is choosing to own that risk.

### `icons`, `assets`, `a11y`

| Object | Keys |
|---|---|
| `icons` | `setId` (`[a-z0-9-]{1,64}`), `style` (`outline` \| `filled` \| `duotone`), `strokeWidth` (0.5–4), `sizeScale` (0.5–2) |
| `assets` | `background`, `logo` — each an `assetRef`; plus `backgroundFit` (`cover` \| `contain` \| `tile` \| `stretch` \| `center`) and `backgroundOpacity` (0–1) |
| `a11y` | `contrastTarget` (`AA` \| `AAA`, default `AA`), `respectsReducedMotion` (default `true`), `minTouchTarget` (24–96, default 44), `focusRingWidth` (1–8, default 2) |

An `assetRef` in a theme matches `^(images|icons|fonts)/[A-Za-z0-9._-]+$`.
`REQ-THM-012` is explicit that this pattern is a **security control, not a
convenience**: it forbids absolute paths, URLs, and traversal, and it is what
makes "a theme cannot reference anything outside its own package" a property of
the format rather than a rule the loader has to remember. A bare `theme.json`
applied outside a package therefore cannot use `assets` at all — there is no
package for the reference to resolve inside.

A worked minimal theme and a worked full theme are committed as
[`theme-minimal.json`](../shared-spec/conformance/theme-validation-cases/valid/theme-minimal.json)
and
[`theme-full.json`](../shared-spec/conformance/theme-validation-cases/valid/theme-full.json).
Start from the minimal one.

## Tier 2 · the skin package

A skin is a ZIP archive with the extension `.arrowskin`, using **deflate or
stored only** — no encryption, no other compression method (`REQ-THM-015`). The
layout inside is fixed:

```text
my-skin.arrowskin  (ZIP)
├── manifest.json          REQUIRED — validated against skin-manifest.schema.json
├── theme.json             REQUIRED — validated against theme-schema.json
├── LICENSE                REQUIRED — the skin's own licence text
├── preview.png            REQUIRED — 1280×800 gallery preview
├── layout/                optional
│   ├── main-window.eclayout
│   ├── now-playing.eclayout
│   ├── mini-player.eclayout
│   └── library.eclayout
├── icons/                 optional — SVG only, sanitised
│   └── *.svg
├── images/                optional — PNG / WebP / JPEG
│   └── *
├── fonts/                 optional — TTF / OTF / WOFF2
│   ├── *.ttf
│   └── LICENSE-fonts      REQUIRED if fonts/ is non-empty
└── i18n/                  optional — translations for skin-authored strings
    └── <lang>.json
```

Four files are mandatory even for a theme-only skin. `LICENSE` is mandatory
because a package with no licence text is a package nobody may redistribute, and
the gallery in `REQ-THM-064` deduplicates and republishes. `preview.png` is
mandatory at exactly `1280×800` because the gallery lays out a grid and one
odd-sized preview ruins it.

`i18n/<lang>.json` holds **skin-authored strings only** — a label you invented for
a button you placed. It never overrides application strings; a skin that could
retranslate the application could relabel *Delete from disk* as *Save*.

### `manifest.json`

Schema:
[`shared-spec/schemas/skin-manifest.schema.json`](../shared-spec/schemas/skin-manifest.schema.json).
Eight fields are required: `schemaVersion`, `id`, `name`, `version`, `license`,
`capabilities`, `minAppVersion`, `checksums`.

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | `1` | constant |
| `id` | `^[a-z0-9]([a-z0-9-]{1,62}[a-z0-9])?$` | 2–64 chars, no leading/trailing hyphen |
| `name` | 1–64 chars | |
| `author` | ≤128 chars | optional |
| `version` | `N.N.N` | |
| `license` | ≤64 chars | SPDX id; the full text also ships in `LICENSE` |
| `homepage` | URI, ≤512 | optional |
| `description` | ≤512 | optional |
| `minAppVersion` | `N.N.N` | `REQ-THM-016` |
| `capabilities` | 1–5 of `theme`, `layout`, `icons`, `images`, `fonts` | unique |
| `targetSurfaces` | ≤16 of `main-window`, `now-playing`, `mini-player`, `library` | unique |
| `preview` | `"preview.png"` | a constant, so the manifest is self-describing |
| `i18n` | ≤64 BCP-47 tags | which `i18n/<lang>.json` files exist |
| `checksums` | 3–1999 entries | SHA-256, lower-case hex, keyed by package-relative path |

Two of these do more work than they look like they do.

**`capabilities` is enforced, not described.** A capability you do not list
**must not be honoured even if the directory is present**. Ship `fonts/` without
declaring `fonts` and the fonts are ignored. This is what makes the
`REQ-THM-003` capability disclosure meaningful: the list the user is shown before
installing is the list the loader will act on, so it cannot understate what the
package does.

**`checksums` is a completeness check, not just an integrity check.** The
installer verifies every listed file **and refuses a package containing a file
absent from the map**. An unlisted file is the entire point: a checksum map that
only covered what it happened to mention would let an attacker append a payload.
`manifest.json` is the one exclusion, because it cannot checksum itself.

The `checksums` key pattern is the format's zip-slip defence written as an
allowlist rather than a denylist:

```text
^(theme\.json|LICENSE|preview\.png
  |(layout|icons|images|fonts|i18n)/[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)?)$
```

Relative, ≤200 bytes, confined to a permitted top-level directory, depth ≤4, and
*structurally incapable* of holding `..`, an absolute prefix, a drive letter, a
NUL, or a control character. A denylist would need to anticipate every encoding
of `..`; this cannot express one.

Committed examples:
[`manifest-minimal.json`](../shared-spec/conformance/theme-validation-cases/valid/manifest-minimal.json)
(theme-only, three checksums) and
[`manifest-full.json`](../shared-spec/conformance/theme-validation-cases/valid/manifest-full.json).

### Hard limits, enforced before extraction

`REQ-THM-017`. "Before extraction" is the important half — these are read from
the ZIP central directory, so a zip bomb is refused without ever being written to
disk.

| Limit | Value | Reason |
|---|---|---|
| Total uncompressed size | 32 MiB | zip bomb |
| Entry count | 2,000 | zip bomb / inode exhaustion |
| Compression ratio, any single entry | 100 : 1 | zip bomb |
| Single file uncompressed | 8 MiB | memory |
| Path depth | 4 | sanity |
| Path length | 200 bytes | cross-platform safety |
| Image dimensions | 8192 × 8192 | decoder memory |
| Font count | 8 | memory, licence review |
| Layout documents | 16 | parse cost |

Some of these are in the manifest schema and some deliberately are not. JSON
Schema can count array items and constrain a path pattern, so entry count and
path length live there. It cannot count properties matching a pattern, so the
16-layout and 8-font caps live in installer code with their own tests — an
unenforceable comment in a schema is worse than no comment.

Every one of these limits **rejects; it does not clamp.** `REQ-SEC-002` requires
untrusted input to be refused rather than repaired, because a clamp turns "this
package is malformed" into "this package now means something the author did not
write". A 9th font is not silently dropped, and a 40 MiB package is not
truncated to 32 MiB.

### Path safety

`REQ-THM-018` requires every entry path to be relative, normalised, free of `..`
segments, free of absolute prefixes and drive letters, free of NUL and control
characters, **not a symlink, not a hard link, not a device node**, and confined to
a permitted top-level directory. Extraction resolves the final path and asserts
it is inside the destination directory — it never trusts the archive's own path.
This is a fuzz target (§21.6), not a code review item.

The link and device-node clauses matter even though the allowlist pattern already
rejects the path shapes: a ZIP entry can name a legal path and still carry a
symlink as its *content*, at which point a later entry writing "through" it
escapes the destination. Checking the mode bits is a separate check from checking
the name.

`REQ-THM-019` makes extraction atomic: extract to a temporary directory, validate
everything, then move into place. A failed or malicious package leaves no
residue — including no half-installed skin that the browser might list.

## The layout DSL

A layout document is JSON with the extension `.eclayout`, validated against
[`shared-spec/schemas/layout.schema.json`](../shared-spec/schemas/layout.schema.json),
and **interpreted — never compiled or evaluated as code** (`REQ-THM-025`). On
desktop the interpreter maps it onto QML `Item` trees; on Android onto Compose
composables. Same document, two renderers, one schema.

Three top-level fields are required:

| Field | Notes |
|---|---|
| `schemaVersion` | `1` |
| `surface` | `main-window` \| `now-playing` \| `mini-player` \| `library` |
| `root` | the single root node |
| `minSize` | optional `{width, height}` below which the surface refuses to shrink |
| `description` | optional, ≤512 chars |

A surface your package does not provide keeps its built-in layout
(`REQ-THM-032`), so a skin that only replaces the mini-player is a complete,
valid skin.

### The 30 components

`REQ-THM-026` closes the vocabulary. Adding a component is a schema-version
change, not an implementation detail.

| Layout | Content | Interactive | Media |
|---|---|---|---|
| `Stack` (z-order) | `Text` | `Button` | `AlbumArt` |
| `Row` | `Icon` | `ToggleButton` | `Visualizer` |
| `Column` | `Image` | `Slider` | `SeekBar` |
| `Grid` | `Marquee` | `VolumeControl` | `PeakMeter` |
| `Panel` | `Divider` | `TransportBar` | `WaveformView` |
| `Spacer` | `Badge` | `SearchField` | `LyricsView` |
| `ScrollArea` | `Rating` | `TabBar` | |
| `SplitPane` | `ProgressBar` | `ListView` | |

Only the seven containers — `Stack`, `Row`, `Column`, `Grid`, `Panel`,
`ScrollArea`, `SplitPane` — may carry `children`. Putting `children` on a `Text`
is a validation error, not a no-op.

### Node properties

Every node requires `type` and accepts nothing outside this set
(`additionalProperties: false`).

#### Identity and conditions

| Property | Type | Notes |
|---|---|---|
| `type` | one of the 30 | required |
| `id` | `^[A-Za-z][A-Za-z0-9_-]{0,63}$` | author-facing only; validation messages name it (`REQ-THM-033`) |
| `when` | predicate | conditional presence — see [`when`](#when--conditional-visibility) |
| `enabledWhen` | predicate | conditional enablement, same grammar |
| `children` | array, ≤500 | containers only |

#### Box model

| Property | Type |
|---|---|
| `padding`, `margin`, `spacing` | a spacing token (`none`, `xs`, `sm`, `md`, `lg`, `xl`, `2xl`, `3xl`, `4xl`) or a raw 0–256 px number |
| `width`, `height` | a 0–16384 number, `fill`, `auto`, or a `"0%"`–`"100%"` string |
| `size` | `{width, height}` |
| `sizing` | `{width, height, minWidth, minHeight, maxWidth, maxHeight, grow}` — `grow` 0–32 |
| `align` | `start` \| `center` \| `end` \| `stretch` \| `baseline` |
| `justify` | `start` \| `center` \| `end` \| `spaceBetween` \| `spaceAround` |

#### Appearance

| Property | Type |
|---|---|
| `background`, `color` | a `color.*` token path — never a literal hex |
| `border` | `{color, width}` — width 0–8 |
| `radius` | a radius token (`none`, `sm`, `md`, `lg`, `xl`, `full`) or a 0–9999 number |
| `elevation` | integer 0–5 |
| `opacity` | 0–1 |
| `clip` | boolean |

A `color.*` path matches `^color\.[a-z][A-Za-z0-9]*(\.[a-z][A-Za-z0-9]*){0,2}$` —
for example `color.background.raised`. Literal hex is deliberately **not**
accepted: a layout that hard-codes colours would break every theme it is combined
with, and would slip past the `REQ-THM-041` contrast check, which only sees the
theme's palette.

#### Text content — exactly one source

| Property | Type | Notes |
|---|---|---|
| `text` | ≤512 chars | a literal string |
| `efs` | ≤1024 chars | an Arrow Format String (§10) |
| `bind` | a state path | a single whitelisted value |
| `style` | `typography.scale.<one of seven>` | on text components |
| `overflow` | `clip` \| `ellipsis` \| `wrap` \| `marquee` | |
| `maxLines` | 1–8 | |
| `textAlign` | `start` \| `center` \| `end` | |

`text`, `efs` and `bind` are **mutually exclusive**, and `Text`/`Marquee` require
exactly one of them. Three ways to fill one slot would make precedence a guessing
game.

#### Interaction and accessibility

| Property | Type | Notes |
|---|---|---|
| `action` | one of the closed enum | required on `Button` and `ToggleButton` |
| `tooltip` | ≤256 chars | |
| `accessibleName` | ≤256 chars | the validator warns when an interactive node has neither text nor this |

`Button` and `ToggleButton` require an `action`, because a button with no action
is decoration pretending to be a control — and a user who clicks it learns
nothing except that the skin is broken.

#### Assets

| Property | Type | Notes |
|---|---|---|
| `source` | `^(images\|icons)/[A-Za-z0-9._-]+$` | required on `Image` |
| `icon` | `^[a-z0-9][a-z0-9-]{0,63}$` | required on `Icon`; resolved against the active icon set, or `icons/` when `icons` is a declared capability |
| `iconSize` | 8–128 | |
| `fit` | `cover` \| `contain` \| `fill` \| `none` | |
| `fallback` | `placeholder` \| `none` \| `blurredColor` | `AlbumArt` with no artwork |

#### Per-component extras

| Property | Applies to | Type |
|---|---|---|
| `columns` | `Grid` (required) | 1–12 |
| `rows` | `Grid` | 1–64 |
| `orientation` | `SplitPane` (required), `Divider`, `Slider` | `horizontal` \| `vertical` |
| `split` | `SplitPane` | 0 < *x* < 1, the initial ratio |
| `scrollDirection` | `ScrollArea` | `vertical` \| `horizontal` \| `both` |
| `buttons` | `TransportBar` (required) | 1–11 unique of `previous`, `playPause`, `play`, `pause`, `stop`, `next`, `shuffle`, `repeat`, `stopAfterCurrent`, `loved`, `rating` |
| `min`, `max`, `step` | `Slider`, `VolumeControl` | numbers; `step` > 0 |
| `showBuffered` | `SeekBar`, `ProgressBar` | boolean |
| `showLabels` | `SeekBar`, `PeakMeter` | boolean |
| `placeholder` | `SearchField` | ≤128 chars |
| `barCount` | `Visualizer`, `PeakMeter` | 4–256 |
| `maxStars` | `Rating` | 3–10 |
| `speed` | `Marquee` | 1–200 px/s |
| `columnsSpec` | `ListView` | ≤16 columns of `{efs, header?, width?, align?, style?}` |
| `tabs` | `TabBar` | ≤9 of `{label, icon?, action}` |
| `style` | `Visualizer`, `PeakMeter`, `WaveformView` | `bars` \| `oscilloscope` \| `spectrum` \| `vuMeter` \| `none` |

`Marquee.speed` is bounded at 200 px/s so a skin cannot produce a
seizure-inducing surface, and it still honours reduced-motion regardless of what
you set (`REQ-UIX-002`).

`Visualizer.style` selects a renderer; it does not script one. projectM presets
are chosen in settings, never by a skin — a skin that could choose shader code
would be executable content, which is [ADR 0003](adr/0003-no-code-in-skins.md)
all over again.

`style` carries two vocabularies because `REQ-THM-031`'s normative example uses it
for both: a type-scale token on text components, and a renderer name on the three
visual-analysis components. The schema's `if`/`then` clauses pin down which
applies for a given `type`, so `"style": "bars"` on a `Text` node still fails
validation. This is the one place in the format where a property name is
overloaded, and it is overloaded because the specification's own example is.

### Resource budgets

`REQ-THM-033`: **≤500 component instances, nesting depth ≤24, ≤64 bindings per
component tree.** Exceeding a budget fails validation with a message naming the
offending node — which is what `id` is for.

The budgets are also why `REQ-THM-034` is achievable: parsing and instantiating a
layout must not block the UI thread for more than 16 ms, and larger trees are
built across frames behind a placeholder. A bounded tree is what makes that a
scheduling problem rather than an open-ended one.

### A worked layout

This is the `REQ-THM-031` mini-player, committed verbatim as
[`mini-player-req-thm-031.eclayout`](../shared-spec/conformance/theme-validation-cases/valid/mini-player-req-thm-031.eclayout):

```json
{
  "schemaVersion": 1,
  "surface": "mini-player",
  "minSize": { "width": 320, "height": 96 },
  "root": {
    "type": "Row",
    "padding": "md",
    "spacing": "sm",
    "background": "color.background.raised",
    "children": [
      { "type": "AlbumArt", "size": { "width": 72, "height": 72 },
        "radius": "md", "fallback": "placeholder" },
      { "type": "Column", "sizing": { "width": "fill" }, "spacing": "xs",
        "justify": "center",
        "children": [
          { "type": "Marquee", "efs": "%title%",
            "style": "typography.scale.title", "color": "color.text.primary" },
          { "type": "Text", "efs": "%artist%[ — %album%]",
            "style": "typography.scale.caption",
            "color": "color.text.secondary", "overflow": "ellipsis" },
          { "type": "SeekBar", "showBuffered": true, "height": 4 }
        ] },
      { "type": "TransportBar", "buttons": ["previous", "playPause", "next"],
        "iconSize": 24 },
      { "type": "Visualizer", "style": "bars",
        "sizing": { "width": 64, "height": "fill" },
        "when": "settings.showMiniVisualizer" }
    ]
  }
}
```

Twelve lines of that are the whole surface: art, a scrolling title, an EFS
secondary line that disappears its own separator when there is no album, a seek
bar, three transport buttons, and a visualiser the user can switch off. No code.
A fuller example is
[`main-window.eclayout`](../shared-spec/conformance/theme-validation-cases/valid/main-window.eclayout).

## Bindings — the complete state whitelist

`REQ-THM-027` requires this list to be documented exhaustively here, and requires
it to expose **nothing beyond presentation state** — no filesystem paths beyond
what a UI already shows, no settings values, no library queries. The enum in
`layout.schema.json` *is* the whitelist; this table is that enum with meanings.

A `bind` reads one value. It cannot write, and there is no path syntax for
reaching a sibling, an index, or a query.

**`track.*` — the currently displayed track**

| Path | Value |
|---|---|
| `track.title` | title |
| `track.artist` | artist |
| `track.albumArtist` | album artist |
| `track.album` | album |
| `track.genre` | genre |
| `track.composer` | composer |
| `track.year` | year |
| `track.trackNumber` | track number |
| `track.discNumber` | disc number |
| `track.duration` | duration, formatted |
| `track.rating` | 0–5 |
| `track.isLoved` | boolean |
| `track.hasArtwork` | boolean |
| `track.hasLyrics` | boolean |
| `track.codec` | e.g. `FLAC` |
| `track.bitrate` | kbps |
| `track.sampleRate` | Hz |
| `track.bitDepth` | bits |
| `track.channels` | channel count |
| `track.isLossless` | boolean |
| `track.fileName` | **base name only** — never a directory |

`track.fileName` is the whole filesystem surface, and it is the base name because
that is what a UI already shows. There is no `track.path`, no `track.directory`,
and no way to reconstruct one.

**`player.*` — transport state**

| Path | Value |
|---|---|
| `player.state` | `playing` \| `paused` \| `stopped` |
| `player.positionMs` | position |
| `player.durationMs` | duration |
| `player.remainingMs` | remaining |
| `player.volume` | 0–100 |
| `player.isMuted` | boolean |
| `player.repeatMode` | current repeat mode |
| `player.shuffleMode` | current shuffle mode |
| `player.speed` | playback rate |
| `player.isBitPerfect` | boolean — bit-perfect output is active |
| `player.replayGainApplied` | gain in dB, or absent |
| `player.hasNext` | boolean |
| `player.hasPrevious` | boolean |

`player.hasNext` / `hasPrevious` exist so a skin can grey out a transport button
instead of the interpreter having to guess; that is what `enabledWhen` is for.

**`queue.*` and `list.*` — position within a collection**

| Path | Value |
|---|---|
| `queue.index` | 1-based position in the queue |
| `queue.total` | queue length |
| `list.index` | 1-based position in the visible list |
| `list.total` | visible list length |
| `list.selectionCount` | how many rows are selected |

These are counts and offsets. There is no way to read *another* row's data, which
is what keeps "no library queries" true.

**`settings.*` — exactly five, and why**

| Path | Value |
|---|---|
| `settings.showMiniVisualizer` | boolean |
| `settings.showVisualizer` | boolean |
| `settings.showLyrics` | boolean |
| `settings.reducedMotion` | boolean |
| `settings.accessibleContrast` | boolean |

`REQ-THM-027` says the state model exposes no settings values. `REQ-THM-031`'s
own worked example binds `settings.showMiniVisualizer`. The narrowest reading that
keeps both statements true is followed: an allowlist of settings that are
themselves statements about *what the UI shows*, and nothing else. A skin can
never read `settings.libraryPaths`, a credential, or a device name — an allowlist
refuses an unknown key without anyone having to predict its name. The conflict is
recorded as OQ-003 in [`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) rather than
resolved silently.

**`app.*`**

| Path | Value |
|---|---|
| `app.version` | application version |
| `app.isPortable` | boolean — running in portable mode |

Anything not in these five tables fails validation at install time. A typo in a
binding is an error you see once, not a value that reads empty forever.

## Actions — the complete enum

`REQ-THM-028`: actions are **enum-only**, and the enum mirrors the §13.2 command
registry. `REQ-KEY-003` is the reason that matters — one registry backs menus,
shortcuts, the command palette, the tray menu, skin `action:` bindings, the
MPRIS/SMTC mappings, and the plugin SDK. **One registry, six consumers**, so an
action a skin references cannot fail to exist, and a command added once appears
everywhere.

An unknown action fails validation **at install time**, not silently at runtime.
A no-op button is an unexplainable skin; a rejected package is a fixable one.

**`player.*` — 21 actions**

`player.playPause` · `player.next` · `player.previous` · `player.stop` ·
`player.stopAfterCurrent` · `player.seekForward` · `player.seekBackward` ·
`player.seekForwardLarge` · `player.seekBackwardLarge` · `player.volumeUp` ·
`player.volumeDown` · `player.muteToggle` · `player.cycleRepeat` ·
`player.cycleShuffle` · `player.addBookmark` · `player.abRepeatSetA` ·
`player.abRepeatSetB` · `player.abRepeatClear` · `player.speedUp` ·
`player.speedDown` · `player.speedReset`

**`view.*` — 22 actions**

`view.focusSearch` · `view.commandPalette` · `view.gotoTab1` … `view.gotoTab9` ·
`view.nextTab` · `view.previousTab` · `view.fullScreenNowPlaying` ·
`view.toggleMiniPlayer` · `view.toggleWindowshade` · `view.toggleAlwaysOnTop` ·
`view.scrollToCurrentTrack` · `view.toggleLyrics` · `view.toggleVisualizer` ·
`view.showEqualizer` · `view.dismiss`

**`file.*`, `playlist.*`, `queue.*`**

`file.openFiles` · `file.openFolder` · `file.openUrl` · `file.deleteFromDisk` ·
`playlist.new` · `playlist.save` · `playlist.closeTab` · `playlist.selectAll` ·
`playlist.removeSelected` · `playlist.undo` · `playlist.redo` ·
`queue.addSelected` · `queue.playNext`

**`track.*` and `library.*`**

`track.showTechnicalInfo` · `track.editTags` · `track.toggleLoved` ·
`track.setRating0` … `track.setRating5` · `library.rescanAll`

**`window.*` — three actions, with a recorded caveat**

`window.close` · `window.minimize` · `window.maximizeRestore`

These three validate today, and you may bind them. But `REQ-THM-028` says the
action set "mirrors the command registry from §13.2", and §13.2 as written
enumerates no window command — §13.3's default-shortcut table lists only
`view.toggleWindowshade` and `playlist.closeTab` in that space. So read literally,
an action with no registry entry is not a valid action.

They are in the schema because `REQ-THM-032` needs them: it counts `close` among
the controls a skin "must never be able to remove", which means a skin must be
able to *invoke* it. The gap is recorded as OQ-039 in
[`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md), with the recommendation that §13.2
gain the three commands so `REQ-THM-028` holds by construction. Until then: the
schema decides, both engines follow the schema, and a close button you place works.

### What is deliberately absent

There is no action for changing a setting, no action for writing a tag other than
the rating and loved flags that the UI already exposes, and no action that takes a
parameter. `track.setRating0` … `setRating5` are six separate actions rather than
one parameterised action precisely because a parameter is the beginning of an
expression language.

## `when` — conditional visibility

`REQ-THM-029` allows exactly two forms of expression in a skin: EFS, and the
`when` predicate. `REQ-THM-030` gives the predicate grammar, deliberately a strict
subset of the smart-playlist comparison grammar — no functions, no nesting beyond
one level:

```ebnf
when      = clause { ("and"|"or") clause } ;
clause    = [ "not" ] atom ;
atom      = state-path operator literal | state-path ;
operator  = "==" | "!=" | ">" | "<" | ">=" | "<=" ;
```

Predicates are ≤256 characters. A bare state path is truthy-tested; with an
operator it is compared against a quoted string (≤64 chars), a number, `true`, or
`false`.

```json
{ "when": "player.state == 'playing' and track.hasArtwork" }
{ "when": "not settings.reducedMotion" }
{ "when": "queue.total > 1" }
{ "enabledWhen": "player.hasNext" }
```

There is no `and`/`or` precedence to learn because there is no nesting: clauses
evaluate left to right. If you need a third level of logic, you are describing
behaviour, and behaviour is a plugin.

Two properties use the grammar. `when` decides whether a node **exists**;
`enabledWhen` decides whether an existing interactive node is **enabled**. Use
`enabledWhen` on transport buttons — a button that vanishes when there is no next
track moves everything beside it, and a moving layout is worse than a greyed one.

The schema's `pattern` for a predicate is a **shape check only**. The validator
parses the predicate properly and rejects any state path outside the
[whitelist](#bindings--the-complete-state-whitelist), so a typo fails at install
time rather than silently evaluating false forever. Do not rely on the pattern
alone to tell you a predicate is valid.

## EFS in a skin

Arrow Format Strings are the one place a skin computes anything. `REQ-EFS-001`
uses one engine for playlist columns, the window title, the tray tooltip, Now
Playing text, notifications, file-naming patterns, and skin `Text` bindings — one
language, one engine, one test suite. The full reference is §10 of the
specification; what follows is what a skin author needs.

`REQ-EFS-002` makes EFS **pure and total**: no side effects, no I/O, no loops, no
recursion, no user-defined functions, and evaluation of any input terminates in
time linear in the length of the pattern. That is a hard requirement *because*
patterns arrive inside downloadable skins.

### Optional blocks are the core idea

`REQ-EFS-003`: a `[...]` block renders as the empty string if **every** field
reference inside it resolved to an absent value, and renders in full if at least
one resolved. This is what lets one pattern serve tracks with and without a disc
number, with no conditionals:

```text
%artist%[ — %album%]
[%discnumber%-]%tracknumber:02% %title%
```

The first drops its own em dash when there is no album. The second drops the
hyphen with the disc number. `REQ-EFS-004` defines *absent* precisely: NULL, or
empty after trimming. An empty-string **literal** is not absent, so
`[$if2(%album%,)]` does not vanish.

### Fields

All the `builtin-field` names from `REQ-PLS-011` are available, plus these
presentation and playback fields, which are the ones a skin actually reaches for:

| Field | Meaning |
|---|---|
| `%length%` | duration as `m:ss`, or `h:mm:ss` when ≥ 1 h |
| `%length_seconds%` | duration in whole seconds |
| `%position%` / `%remaining%` | position / remaining, formatted like `%length%` |
| `%playing_state%` | `playing` \| `paused` \| `stopped` |
| `%queue_index%` / `%queue_total%` | 1-based queue position, and its size |
| `%list_index%` / `%list_total%` | 1-based position in the visible list, and its size |
| `%codec%`, `%bitrate%`, `%samplerate%`, `%bitdepth%`, `%channels%` | technical |
| `%filesize_natural%` | e.g. `8.4 MB`, localised |
| `%rating_stars%` | rating rendered as star glyphs |
| `%replaygain_applied%` | the gain actually applied, in dB, or absent |
| `%is_bitperfect%` | `1` when bit-perfect output is active |

### Functions

`REQ-EFS-008` closes this set: adding a function is a spec change, not an
implementation detail. Every function returns a string — `REQ-EFS-005` is explicit
that there is no numeric type at the surface, so numeric functions parse, compute
and re-render, and non-numeric input yields *absent* rather than an error.

**Conditional** — a table rather than a list, because the argument shapes differ:

| Function | Behaviour |
|---|---|
| `$if(cond,then[,else])` | `then` if `cond` is non-empty, else `else` |
| `$if2(a,b)` | `a` if non-empty, else `b` |
| `$if3(a,b,…)` | first non-empty argument |
| `$ifequal(x,y,then,else)` | numeric equality |
| `$ifgreater(x,y,then,else)` / `$ifless(…)` | numeric comparison |
| `$iflonger(s,n,then,else)` | string-length comparison |

**String** — `$upper(s)` · `$lower(s)` · `$title(s)` · `$trim(s)` · `$len(s)` ·
`$sub(s,start[,len])` · `$left(s,n)` · `$right(s,n)` · `$pad(s,n[,ch])` ·
`$padright(s,n[,ch])` · `$cut(s,n)` (truncate, no ellipsis) · `$abbr(s[,n])`
(initials) · `$replace(s,find,repl)` · `$strchr(s,ch)` · `$strstr(s,sub)` ·
`$insert(s,ins,at)` · `$repeat(s,n)` (with `n ≤ 256`, enforced) · `$caps(s)` ·
`$meta_sep(field[,sep])` (join a multi-valued field)

**Numeric** — `$add(a,b,…)` · `$sub2(a,b)` · `$mul(a,b,…)` · `$div(a,b)`
(÷0 → absent) · `$mod(a,b)` · `$min(a,…)` · `$max(a,…)` · `$num(n,width)`
(zero-pad) · `$round(n[,dp])` · `$abs(n)`

**Time** — `$time(seconds)` → `m:ss` / `h:mm:ss` · `$timems(ms)` ·
`$date(iso[,fmt])` (locale-aware) · `$year(iso)` ·
`$age(iso)` (e.g. `3 days ago`, localised)

**Presentation** — `$char(codepoint)` · `$crlf()` · `$tab()` ·
`$progress(pos,total,width[,fill,empty])` → a text progress bar ·
`$stars(rating[,max])` · `$fixed(s,n)` (pad or cut to exactly `n`)

`$progress` is what makes the built-in **Arrow Console** skin possible — a
monospaced, text-forward, keyboard-first layout with ASCII progress bars, which
exists to prove EFS and the layout DSL together can produce a radically different
product feel without a line of code.

### The output cap

`REQ-EFS-009`: `$repeat` and `$progress` enforce a hard cap of **4096 characters**
per pattern evaluation. A pattern inside a downloaded skin must not be able to
produce a gigabyte string. `$repeat`'s `n` is separately capped at 256.

`REQ-EFS-006` completes the picture: **errors never throw**. A malformed pattern
renders as much as it can and exposes a parse error to the *editor* UI, but a
malformed pattern in a skin must not crash, block, or blank the whole surface — it
renders the literal remainder. So a typo in one `ListView` column degrades that
column, not the window.

Validation step 10 parses every EFS pattern in the package and verifies the output
cap before the skin is ever applied, so a pattern that would breach the cap is a
rejected package rather than a slow surface.

## Validation — the ten steps

`REQ-THM-040` fixes the order, and the package is **rejected at the first
failure** with a precise, human-readable reason. The order is not arbitrary:
every step assumes the previous ones passed, which is what lets step 8 probe an
image's dimensions knowing the file is where the manifest said it would be.

| Step | What it checks | Judged by |
|---|---|---|
| 1 | ZIP structural integrity; the `REQ-THM-017` limits | code |
| 2 | path safety for every entry (`REQ-THM-018`) | code |
| 3 | `manifest.json` against its schema; checksum verification of every file | schema + code |
| 4 | `theme.json` against `theme-schema.json` | schema |
| 5 | contrast enforcement (`REQ-THM-041`) | code |
| 6 | every `.eclayout` against `layout.schema.json`, plus whitelist and budget checks | schema + code |
| 7 | every SVG sanitised (`REQ-THM-042`) | code |
| 8 | every raster image probed for dimensions **before** decode | code |
| 9 | fonts: format check, and `fonts/LICENSE-fonts` presence | code |
| 10 | every EFS pattern parsed and its output cap verified | code |

Because the pipeline stops at the first failure, a package is only ever tested
against one step. That is also how the conformance corpus is organised: each
fixture is a test of its own step and no other. The corpus lives in
[`shared-spec/conformance/theme-validation-cases/`](../shared-spec/conformance/theme-validation-cases/)
with 122 cases and their expected verdicts in `index.json`, and `REQ-GEN-031`
requires the desktop and Android engines to reach **identical** verdicts on all of
them. The verdicts live in the corpus rather than in either implementation's
tests, precisely so that neither implementation is the definition.

Four verdicts exist: `accept`, `reject`, `accept-with-warning` (contrast, in the
default configuration), and `table` (the fixture is itself a list of sub-cases).

### SVG sanitisation

`REQ-THM-042`, step 7. Icons are SVG only, and every one is sanitised. The
following are **stripped, and the package rejected if present**:

- `<script>`
- `<foreignObject>`
- `<use>` with an external reference
- `<image>` with a non-`data:` href
- any `on*` event attribute
- any `href` / `xlink:href` that is not an internal fragment
- `<style>` with `@import`
- any external entity or DOCTYPE declaration

The element count is capped at **10,000**, and parsing runs with **entity
expansion disabled** — the billion-laughs defence. Note that `<style>` itself is
allowed; only `@import` is not, because `@import` is a network request wearing a
stylesheet.

A safe reference icon is committed as
[`icon-play-safe.svg`](../shared-spec/conformance/theme-validation-cases/valid/icon-play-safe.svg).
If your icons come out of a design tool, expect to have to strip a DOCTYPE and an
editor's metadata; most exporters add both.

### Raster images and fonts

Step 8 probes dimensions **before** decode, capped at 8192 × 8192. Probing first
is the point: a decoder that has already allocated for a 40000 × 40000 image has
already lost. Formats are PNG, WebP and JPEG.

Step 9 checks font format (TTF, OTF, WOFF2) and requires `fonts/LICENSE-fonts` to
exist whenever `fonts/` is non-empty. Fonts are the most commonly
mis-redistributed asset in skin ecosystems; the file is required so the question
is answered before the package is published, not after a complaint.

## Reading a validation error

`REQ-THM-060` specifies `tools/theme-validate`: a CLI that validates a theme, a
layout, or a full `.arrowskin`, and **prints every error with a JSON Pointer to
the offending node**. It runs in CI over all bundled skins.

A JSON Pointer (RFC 6901) is a path into the document: `/root/children/2/style`
means "the `style` key of the third child of `root`". Array indices are 0-based,
so `children/2` is the third element. Read a pointer left to right against your
own file and it lands on exactly one place.

The contract the tool must meet, so you know what to expect from an error:

| Requirement | What it obliges the message to contain |
|---|---|
| `REQ-THM-060` | a JSON Pointer to the offending node, for **every** error, not just the first |
| `REQ-THM-033` | the offending node **named** when a resource budget is exceeded — the `id` you gave it |
| `REQ-THM-040` | a precise, human-readable reason, and the step that produced it |
| `REQ-THM-052` | when the cause is a version mismatch, the app version needed |

Which is why giving your nodes `id`s is worth the keystrokes: `id` has no effect
on rendering and exists only so that the message says `transportRow` instead of
`/root/children/1/children/3`.

Common rejections and what they actually mean:

| Message shape | Cause |
|---|---|
| `additionalProperties` at some pointer | a typo'd or invented key — the schemas are closed at every level |
| a `type` value not in the enum | an invented component; the vocabulary is closed (`REQ-THM-026`) |
| a `bind` value not in the enum | a state path outside the whitelist, including a plausible one like `track.path` |
| `children` not allowed | `children` on a non-container |
| missing `action` | a `Button` or `ToggleButton` with nothing to do |
| missing `columns` / `orientation` / `buttons` | a `Grid`, `SplitPane` or `TransportBar` that did not declare what it needs |
| a `style` value rejected | the two `style` vocabularies crossed — a renderer name on a text node, or a type token on a `Visualizer` |
| a file in the package with no `checksums` entry | an unlisted file; add it or remove it |
| `LICENSE-fonts` missing | `fonts/` is non-empty |

`REQ-THM-051` promises hot-reload for authors: a developer mode that watches an
**unpacked** skin directory and reapplies within 300 ms, showing validation errors
as an in-app overlay rather than a dialog. Author against a directory, package
once at the end.

## Contrast is enforced, not requested

`REQ-THM-041` replaced an earlier claim that "contrast ratios [are] enforced by
the design tokens themselves", which named no mechanism. The mechanism is: the
loader computes the WCAG 2.2 contrast ratio for **every (text colour, background
colour) pair the theme can produce**, and:

| Ratio | Behaviour |
|---|---|
| ≥ 4.5 : 1 normal text, ≥ 3 : 1 large text (≥ 18.66 px regular / ≥ 14 px bold) | accept |
| below the AA floor | accept **with a warning** listing the offending pairs — unless *Enforce accessible contrast* is on |
| *Enforce accessible contrast* on (default **off**; forced **on** by the High Contrast theme and by the OS high-contrast setting) | **reject**, or offer auto-correction that nudges lightness until AA is met |

Two consequences for an author. First, the default configuration warns rather than
rejects, so a low-contrast theme installs — but it will hard-fail for any user who
has turned enforcement on, and for every user on an OS high-contrast setting. A
theme that only works with the accessibility switch off is not finished. Second,
because the check runs over pairs the theme *can produce*, an unset token that
inherits a value you never looked at can still fail; `theme-full.json` sets every
pair explicitly for exactly that reason.

`REQ-THM-072` holds the built-in themes to this: all four must pass at the AA
floor, and High Contrast must meet **AAA** (7 : 1) throughout. `REQ-UIX-058` adds
a rule contrast cannot express — information must never be conveyed by colour
alone, so playback state, selection, ratings and error states each need a
non-colour indicator too.

## Trust, and what "Verified" does not mean

`REQ-THM-043` is deliberate: there is **no signature requirement** for skins,
because §11.1 removed the code-execution surface that would make signing
necessary. Instead:

- skins from the curated gallery are marked **Verified** — reviewed by maintainers;
- skins installed from a file are marked **Unverified**, and their capabilities are
  shown before applying;
- **the badge is about provenance and taste, never about safety.** Safety comes
  from validation, and validation is *identical* for both.

`REQ-THM-043` requires this document to state that explicitly, so: an
**Unverified** skin is not a dangerous skin. It has been through the same ten
validation steps as a gallery skin, with the same limits, the same path checks and
the same sanitisation. What it has not been through is a human deciding it looks
good and does what it says. Treat the badge the way you would treat a curated
playlist, not the way you would treat a code signature.

The corollary is worth saying too: a **Verified** badge is not a promise the skin
is well-made for your setup. It cannot know your display, your DPI, or whether you
have contrast enforcement on.

## Packaging a skin

There is no packaging tool in this tree yet, so these are the steps a tool would
take and that you can take by hand. The order matters: `checksums` must be
computed last, over the final bytes of every other file.

1. **Author unpacked.** A directory with the layout from
   [§ Tier 2](#tier-2--the-skin-package). Iterate with hot-reload
   (`REQ-THM-051`) rather than repacking.
2. **Write `theme.json`.** Start from `theme-minimal.json`; set `mode` and either
   `extends` or enough of `color` to stand alone.
3. **Write layouts** into `layout/<surface>.eclayout`, one per surface you are
   replacing. You do not need all four.
4. **Add assets.** SVG in `icons/`, raster in `images/`, fonts in `fonts/` with
   `LICENSE-fonts` beside them.
5. **Write `LICENSE`** — the full text, not just an SPDX id.
6. **Render `preview.png`** at exactly 1280 × 800.
7. **Write `manifest.json`.** `capabilities` must list every tier you actually use
   and nothing more; `targetSurfaces` must list the surfaces you replaced.
8. **Compute `checksums`.** SHA-256, lower-case hex, of every file except
   `manifest.json`, keyed by package-relative path with forward slashes.
9. **Zip it.** Deflate or stored only, no encryption, extension `.arrowskin`.
   No directory entries are needed, and no `__MACOSX`, `.DS_Store` or
   `Thumbs.db` — an unlisted file is a rejection, and those are the three that
   most often sneak in.
10. **Validate before publishing.** `tools/theme-validate` over the package, and
    fix every pointer it prints.

A theme on its own needs none of this. A `theme.json` is a file a user can apply
directly; the package format exists for the tiers above it.

## Compatibility and versioning

`REQ-THM-052`:

| Situation | Behaviour |
|---|---|
| skin's `schemaVersion` **<** current | loaded through a documented migration path; the user is not warned |
| skin's `schemaVersion` **>** current | refused, stating the app version needed |
| skin's `minAppVersion` **>** app version | refused, stating the app version needed |
| unknown **token** present | ignored, and the author warned in dev mode |
| unknown **component / action / binding** | **rejected at install time** |

The asymmetry in the last two rows is the whole compatibility policy. An unknown
*token* is forward compatibility — a future theme should still apply on an older
app, minus the token it does not know. An unknown *component, action or binding*
is a structural claim that cannot be partially honoured: silently no-oping it
produces a skin whose behaviour nobody can explain, so it is refused where the
author can still see it.

`REQ-THM-053` and `REQ-BLD-035` make `theme-schema.json`, `layout.schema.json` and
`settings.schema.json` versioned artefacts under `shared-spec/`, each with an
independent integer version. Additive changes keep the version and must be ignored
gracefully by older readers; removals or semantic changes increment it and require
a documented migration on the reading side.

Removal is also specified: `REQ-THM-044` requires an installed skin to be **fully
removable, leaving no files behind**, and removing the *active* skin falls back to
the default without restarting the app. `REQ-THM-050` requires applying a theme or
skin to take effect without a restart and **without interrupting playback by even
one sample**, cross-fading the affected surfaces over `motion.duration.normal`.

## What does not exist yet

`docs/OPEN-QUESTIONS.md` §5 is the authoritative verification table; this is the
skin-specific slice of it, so that nothing above reads as a description of working
software.

### What exists and is verified

| Artefact | State |
|---|---|
| `shared-spec/schemas/theme-schema.json` | committed; parses; validated structurally by `tools/validate-shared-spec.py` |
| `shared-spec/schemas/skin-manifest.schema.json` | committed; same |
| `shared-spec/schemas/layout.schema.json` | committed; same |
| `shared-spec/design-system/tokens.json` | committed; the canonical values above are read from it |
| the 122-case validation corpus | committed with expected verdicts; fixtures parse |
| `.github/scripts/compare_verdicts.py` | committed with a self-test; compares two engines' verdict files |
| `docs/adr/0003-no-code-in-skins.md` | committed |

### What is absent

| Missing | Consequence |
|---|---|
| `tools/theme-validate` | the directory is empty. **No validator transcript in this document is real**, and none is shown. `REQ-THM-060` is unmet, and `REQ-THM-072`'s CI check over the built-in skins cannot run |
| the skin loader and installer | steps 1, 2, 3, 8 and 9 of the pipeline have no implementation; the `REQ-THM-017` limits and `REQ-THM-018` path checks are specified and fixtured, not enforced |
| the layout interpreter | `.eclayout` files are validated data with no renderer; `REQ-THM-025`, `REQ-THM-032`, `REQ-THM-033` and `REQ-THM-034` are unimplemented |
| the contrast checker | `REQ-THM-041` has fixtures and expected verdicts, no computation |
| the SVG sanitiser | `REQ-THM-042` likewise |
| the EFS engine | `REQ-EFS-*` is specified and fixtured; the field and function tables above describe a language with no evaluator in this tree yet |
| the four built-in themes and three built-in skins | `REQ-THM-070` and `REQ-THM-071` are unmet; the `valid/` corpus fixtures are the closest thing that exists |
| the in-app skin browser, hot-reload, and the skin editor | `REQ-THM-061`, `REQ-THM-051`, `REQ-THM-062` unmet |

So: **you can author against these formats today and your files will be
structurally correct, but nothing in this tree will render them.** The schemas are
committed and versioned, and the corpus pins the verdicts, which is what makes it
safe to author early — when the loader arrives it has to agree with the corpus, not
the other way round.

`spec-ci.yml`'s `native` job used to build and run `theme-validate` unconditionally,
which meant it could not pass. It now detects whether the engine is in the tree —
deciding on `tools/theme-validate/CMakeLists.txt`, since git cannot track an empty
directory — and while it is absent it emits a warning, writes a summary naming the
122 corpus cases nobody runs, and uploads no verdict file. The day the CLI lands,
every build and run step in that job becomes blocking with no edit to the workflow.
Recorded as OQ-040 in [`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) rather than left
to look like a passing gate — and the table above still says `REQ-THM-060` is unmet,
because it is.

## See also

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — where the theme and layout engines
  sit in the five layers
- [`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) — every assumption this
  implementation had to make, including OQ-003, OQ-039 and OQ-040 above
- [`docs/adr/0003-no-code-in-skins.md`](adr/0003-no-code-in-skins.md) — the
  decision record behind `REQ-THM-002`
- [`shared-spec/README.md`](../shared-spec/README.md) — the versioning policy for
  the schemas and the corpus
- [`eclipse-player.md`](../eclipse-player.md) §10–§12 — the normative source for
  everything here

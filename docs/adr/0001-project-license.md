# ADR 0001 — Project licence: MPL-2.0 with LGPL-only dependencies

- **Status:** Accepted
- **Date:** 2026-08-23
- **Requirements:** REQ-GEN-010 … REQ-GEN-023

## Context

Arrow Player is a free, open-source music player. The licence choice is not
cosmetic: it determines which decoders we may legally ship, whether closed-source
plugins can link against our SDK, and what obligations attach to every release.
Specification v1.x contained no licensing analysis at all, which is a release
blocker for an open-source player that links FFmpeg and Qt.

## Decision

**Arrow Player's own source is `MPL-2.0`.** All third-party dependencies must be
permissive or LGPL; **no GPL-only component may be linked.**

The plugin SDK headers are additionally dual-licensed `Apache-2.0 OR MPL-2.0`
(REQ-GEN-011) so plugin authors of any licence can include them unambiguously.

## Rationale

- **File-level copyleft** keeps improvements to Arrow's own files open, which
  protects the project, without infecting anything that merely links against it.
- **Compatible with LGPL dependencies** without forcing the combined work to GPL.
- **Permits closed-source plugins** to link the SDK. Winamp's plugin ecosystem is
  the single clearest reason it outlived its owner (§2.3); a licence that blocked
  proprietary plugins would forfeit that.
- **Does not force GPL on downstream distributions**, so distro packagers and Qt
  commercial-licence holders are both unblocked.

## Precedent

AIMP — a comparable freeware player — ships **FFmpeg 7.1.1 and SoundTouch under
LGPLv2.1** and publishes source-offer links for both. That is direct evidence
that an LGPL-only media stack is workable in a shipping player, and we adopt the
same compliance pattern (REQ-GEN-020).

## Consequences

**Accepted constraint: no GPL components.** Concretely this means:

- FFmpeg must be configured **without** `--enable-gpl` and **without**
  `--enable-nonfree` (REQ-GEN-014), and CI asserts this mechanically by checking
  `avutil_license()` at runtime (REQ-GEN-015). A review step would not be enough.
- `libsamplerate` must be **≥ 0.1.9**; earlier releases were GPL.
- `libfdk_aac` is unusable (non-free). AAC decode uses FFmpeg's native LGPL
  decoder; AAC encoding is not offered.
- The Monkey's Audio SDK must not be vendored; FFmpeg's native LGPL `ape`
  decoder is used instead.
- Qt must be **dynamically linked** so users can replace it, as LGPL-3.0
  requires (REQ-GEN-013). `cmake/ArrowDependencies.cmake` fails the configure
  step if a static Qt is detected, so this cannot regress silently.
- ASIO support requires the Steinberg SDK, which we may not redistribute; it is
  therefore an opt-in build with a user-supplied SDK (REQ-GEN-018).

**Obligation: a published source offer.** Every release must link the exact
corresponding source for every LGPL component, keyed by release tag
(REQ-GEN-020), plus a CycloneDX SBOM (REQ-GEN-021).

## Alternatives considered

**GPL-3.0-or-later** would simplify compliance and allow GPL-enabled FFmpeg
components. Rejected because it would prevent closed-source plugins from linking
the SDK, forfeiting the ecosystem argument above, and would exclude Qt
commercial-licence users.

**Apache-2.0 / MIT** would maximise adoption but would let a vendor fork the
player, close it, and ship it without contributing fixes back. MPL-2.0's
file-level copyleft is the middle position that protects the project's own code
while leaving the plugin boundary open.

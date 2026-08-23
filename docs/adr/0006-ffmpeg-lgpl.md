# ADR 0006 — FFmpeg: decode-only, LGPL configuration, asserted in CI

- **Status:** Accepted
- **Date:** 2026-08-23
- **Requirements:** REQ-GEN-014, REQ-GEN-015, REQ-GEN-016, REQ-GEN-017

## Context

ADR 0001 commits the project to MPL-2.0 with no GPL-only dependencies. FFmpeg can
be built as LGPL **or** GPL depending on configure flags, and the GPL build is
what many distributions ship. Linking a GPL FFmpeg into an MPL-2.0 application
and distributing the result is a licence violation.

This is not a hypothetical risk. It is a single missing configure flag, invisible
in the source tree, that would only be discovered by someone auditing a release.

## Decision

Shipped FFmpeg builds are configured with:

```
--disable-gpl  --disable-nonfree
--disable-programs --disable-doc
--disable-encoders --disable-muxers --disable-filters --disable-devices
--disable-network
--enable-shared --disable-static
```

Two of these deserve explanation:

- **`--disable-network`.** Eclipse does all its own HTTP through one internal
  client so that the global network switch, TLS policy and redaction are enforced
  in exactly one place (REQ-NET-002). Leaving FFmpeg's network layer enabled would
  create a second, unpoliced egress path — which would quietly undermine the
  zero-connection guarantee in REQ-SET-010.

- **`--disable-encoders`.** v1.0 decodes only. Encoders are enabled selectively
  when the converter lands (REQ-GEN-016), and only LGPL-clean ones.
  `libfdk_aac` is permanently excluded as non-free.

## The part that matters: this is asserted, not documented

REQ-GEN-015 requires CI to **verify at build time** that the linked FFmpeg reports
neither GPL nor non-free, by checking `avutil_license()` and failing the build
otherwise.

A configure flag recorded in a document is a flag that will eventually be wrong.
A test that fails the build is a flag that stays right. This is the same reasoning
applied to the static-Qt check (REQ-GEN-013) and the analytics-denylist scan
(REQ-SET-010): every licence and privacy claim in this project should be
mechanically enforced, because claims that depend on human vigilance decay.

## Precedent

AIMP ships FFmpeg 7.1.1 under LGPLv2.1 with a published source offer, which is
direct evidence that an LGPL-only FFmpeg is sufficient for a full-featured
player's decode needs.

## Consequences

**Positive.** Licence compliance is a build property. Release artifacts are
smaller, since programs, docs, filters, muxers and devices are all excluded.
There is exactly one network egress path in the application.

**Negative.** Release builds cannot use a distribution's FFmpeG package and must
build it from source with these flags. Developer builds may use the system
package, which is why the assertion runs in CI rather than at configure time —
configure cannot tell the difference reliably, but a runtime check can.

**Negative.** Formats requiring GPL components are unavailable. In practice this
affects nothing in the §29.1 support matrix: every format there has an LGPL
decoder.

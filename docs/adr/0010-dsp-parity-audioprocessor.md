# ADR 0010 — Custom Media3 `AudioProcessor`, not `android.media.audiofx`

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-AUD-107, REQ-AUD-108, REQ-AUD-082

## Context

The desktop EQ is specified exactly (§8.9.1): ISO band centre frequencies, RBJ
biquad kernels, Q derived from the band spacing, a stated gain range. The
coefficients are computed from published formulas, and the implementation is unit
tested against them.

Android ships an equalizer in the platform: `android.media.audiofx.Equalizer`. It
is the path of least resistance — no DSP code, hardware offload on some devices,
and it is what most Android players use.

It cannot produce parity with the desktop engine, for reasons that are properties
of the API rather than bugs:

1. **Band count is device-dependent**, and typically 5. The specification requires
   10 and 18 band configurations. There is no way to ask for a band count.
2. **Band centre frequencies are chosen by the device**, not by the caller. Two
   phones give different frequencies for "band 3", and neither necessarily matches
   an ISO centre.
3. **The filter topology is unspecified.** Q, filter order and whether the bands
   are peaking EQs at all are OEM implementation details. Nothing is documented,
   so nothing can be matched.
4. **Behaviour varies by OEM even at identical settings**, because some route
   through a DSP offload with its own tuning.

The consequence is that a user's saved EQ preset would mean one thing on their
desktop, another on a Pixel, and a third on a Samsung — while the settings-export
format (§19.4) synchronises the numbers between them. Numbers that transfer
faithfully but *mean* different things are worse than numbers that do not
transfer, because the user has no way to notice.

## Decision

**Android implements the EQ and effects as custom Media3 `AudioProcessor`
instances that share the identical biquad coefficient formulas from
`REQ-AUD-082`.** `android.media.audiofx.Equalizer` is not used for the main EQ.

- The coefficient formulas are the shared artefact — the same RBJ derivations,
  the same ISO frequencies, the same Q derivation, applied to the same input
  samples in the same order.
- `REQ-AUD-108` makes the parity claim testable rather than asserted: a
  conformance test feeds the **same impulse** and the **same EQ settings** through
  both implementations and asserts the outputs match within **−90 dBFS** RMS
  error. That threshold is a real bound, not a formality — it is far below what
  any listener could detect, and far above the noise floor of the comparison, so
  a failure means a genuine divergence rather than a rounding artefact.
- `android.media.audiofx` remains available for what it is genuinely good at:
  system-level effects the user has configured outside the app, and hardware
  offload paths where the user has explicitly opted into device processing. It is
  never the main EQ.

## Consequences

**Positive.** "Same DSP on both platforms" becomes a fact with a test behind it.
An EQ preset exported from desktop and imported on Android produces the same
frequency response, which is what a user assumes a preset means.

**Positive.** The EQ is fully specified on both platforms, so a support question
about EQ behaviour has one answer rather than one per device.

**Positive.** Full control of the band count, so the 18-band configuration is
possible at all — the platform EQ makes it impossible regardless of tuning.

**Negative.** DSP runs in our code on the CPU, not in a possible hardware offload
path. That costs battery, and battery is a budgeted resource (§20.7). Mitigated by
the fact that biquad cascades are cheap relative to decoding, and by bypassing the
chain entirely when every band is at unity gain — a passthrough, not a chain of
identity filters, the same rule `REQ-AUD-110` applies to the limiter.

**Negative.** More code on the Android side, in the real-time audio path, subject
to the same RT-safety constraints as desktop (§8.2.3) but in Kotlin/JNI. That is
harder to get right than calling a platform API.

**Negative.** Devices whose users rely on a system-wide OEM equalizer will see it
apply *after* ours if they have both enabled. Documented rather than fought.

## Alternatives considered

**`android.media.audiofx.Equalizer`** — rejected for the four API-level reasons
above. Convenience is not worth a preset that means something different on every
device.

**Restricting the desktop EQ to 5 bands to match Android's floor** — rejected, and
worth naming explicitly because it is the shape of mistake §0.1 rule 2 forbids:
weakening a requirement so an implementation passes. The specification calls for
10 and 18 bands with ISO frequencies; the correct response to a platform that
cannot do that is to implement it, not to lower the target.

**A shared native DSP library via NDK, compiled into both platforms** — rejected
for v1.0, and the closest call here. It would guarantee bit-identical output
rather than −90 dBFS agreement, and remove a whole class of divergence. It was
rejected because §5 forbids the two platforms importing each other's code and
because a shared native library becomes a third build system, a third set of ABI
concerns, and a JNI boundary inside the RT path. The −90 dBFS conformance test
buys most of the assurance at a fraction of the structural cost. If parity
failures recur in practice, this is the alternative to revisit.

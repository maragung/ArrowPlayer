# ADR 0002 — Audio output: an `IAudioSink` port with native backends

- **Status:** Accepted
- **Date:** 2026-08-23
- **Requirements:** REQ-AUD-060 … REQ-AUD-077, REQ-AUD-075 (bit-perfect contract)
- **Supersedes:** the RtAudio choice in specification v1.x

## Context

Specification v1.x named **RtAudio** as the desktop audio output layer and claimed:

> Bit-perfect / exclusive output mode where the OS allows (WASAPI exclusive on
> Windows, ALSA/PipeWire direct on Ubuntu, both via RtAudio's low-level device
> access)

Bit-perfect output is one of the three product differentiators (§2.2), so this
claim is load-bearing. It was verified against RtAudio's official README and API
notes before implementation, and it does not hold.

## What the verification found

1. **RtAudio's Linux backends are OSS, ALSA, JACK and PulseAudio.** There is no
   PipeWire backend. The v1.x document's "ALSA/PipeWire … via RtAudio" was simply
   incorrect.

2. **RtAudio's public API exposes no share-mode selection.** There is no way to
   request `AUDCLNT_SHAREMODE_EXCLUSIVE`, so WASAPI exclusive mode is
   unreachable through it.

3. **RtAudio converts formats internally.** Its documentation states that *"all
   necessary data format conversions, channel compensation, de-interleaving, and
   byte-swapping is handled by internal RtAudio routines"*, and that its ALSA
   implementation *"makes no use of the ALSA 'plug' interface"* while still
   converting internally. Internal conversion is fundamentally incompatible with
   a guarantee that the decoder's integer samples reach the DAC unmodified.

4. **RtAudio's ASIO support requires Steinberg SDK sources** that cannot be
   redistributed under our licence (§4.6).

Beyond the bit-perfect problem, sample-exact gapless (§8.4) and the DSP chain
(§8.9) both need control over the device period, the clock and the buffer
lifecycle that a general-purpose wrapper does not expose.

## Decision

Define our own `IAudioSink` port (layer 2) and implement it with native
per-platform backends:

| Platform | Backend | Modes | Tier |
|---|---|---|---|
| Windows | WASAPI via `IAudioClient`/`IAudioClient3` | shared **and** exclusive | v1.0 |
| Linux | ALSA via `libasound` | `plughw:` shared, `hw:` direct | v1.0 |
| Linux | PulseAudio via `libpulse` async | shared | v1.0 |
| Linux | PipeWire via `libpipewire` | shared, low-latency | v1.x |
| Linux | JACK | pro-audio routing | v1.x |
| Windows | ASIO | exclusive | v1.x, opt-in build, SDK user-supplied |

RtAudio is retained **only** as an optional last-resort Linux fallback behind
`ECLIPSE_ENABLE_RTAUDIO` (default `OFF`), and when active the UI must show a
persistent "compatibility output — bit-perfect unavailable" indicator
(REQ-AUD-073).

## Alternatives considered

**`miniaudio`** and **PortAudio** both expose WASAPI exclusive mode, and
PortAudio additionally covers ASIO. Either would be a reasonable substitute for
the Tier-2 backends, and both are permissively licensed. They were not chosen as
the primary because bit-perfect verification (§8.11 test 5) requires asserting on
the exact bytes handed to the device, which is easier to guarantee when we own
the sink. **If maintaining the native backends proves more costly than expected,
swapping the PulseAudio and PipeWire backends for one of these is a local change
behind `IAudioSink` and should be recorded as a follow-up ADR rather than treated
as a redesign.**

**Qt Multimedia** was rejected outright: no exclusive mode, no latency
guarantees, and it would pull the audio path into the UI framework.

## Consequences

**Positive.** The bit-perfect contract in REQ-AUD-075 becomes achievable and
testable. Device-loss recovery (§8.10.2) can be driven by real platform
notifications (`IMMNotificationClient`, ALSA `-EPIPE`) rather than a wrapper's
lowest common denominator. The claim we make to users is one we can verify.

**Negative.** Substantially more platform code to write and maintain — roughly
one backend per platform-API rather than one dependency. Each backend needs its
own device-enumeration, format-negotiation and error-mapping logic. This is
accepted because the alternative is dropping a headline feature.

**Neutral.** `IAudioSink` is a narrow interface (enumerate, open, start, stop,
close, callback, latency, device-lost), so a new backend is a self-contained
addition and each one is independently testable against a mock.

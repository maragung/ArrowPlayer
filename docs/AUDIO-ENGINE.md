# Audio Engine

§27 requires this document to cover *signal chain, RT rules, gapless formulas,
bit-perfect*. It also carries §8.11 — how each audio claim is **proven** — because
`REQ-TST-001` is unambiguous on the point: an audio feature without its
verification test is not done.

Everything here is transcribed from `arrow-player.md` §8, with the arithmetic
kept exact and the reasoning kept attached to it. Where the tree does not yet
implement something, this document says so in the section that describes it
rather than in a footnote.

- [Internal format](#internal-format)
- [The signal chain](#the-signal-chain)
- [Threads and the real-time rules](#threads-and-the-real-time-rules)
- [Gapless — the formulas, per format](#gapless--the-formulas-per-format)
- [Fades and crossfade](#fades-and-crossfade)
- [Resampling and dither](#resampling-and-dither)
- [Bit-perfect mode — the binding contract](#bit-perfect-mode--the-binding-contract)
- [Robustness and recovery](#robustness-and-recovery)
- [How each claim is proven](#how-each-claim-is-proven)
- [What exists today](#what-exists-today)

## Internal format

`REQ-AUD-001`: the internal processing format is **32-bit IEEE-754 float,
planar (de-interleaved), non-normalised**, with nominal full scale at ±1.0.

Two consequences that are easy to get wrong:

**Values above ±1.0 are legal everywhere except the output stage**
(`REQ-AUD-002`). No intermediate stage may clamp. ReplayGain boost and EQ gain
both legitimately push past full scale, and a stage that clamps "to be safe"
destroys exactly the headroom the limiter at stage 11 exists to reclaim. Clamping
mid-chain is a correctness bug, not a conservative choice.

**Planar, not interleaved** (`REQ-AUD-003`), because every per-channel filter —
biquad EQ, crossfeed, balance — operates on contiguous channel data.
Interleaving happens exactly once, at the sink boundary.

## The signal chain

`REQ-AUD-004`: this order is **normative**.

```text
 1. Decode              → planar float32 @ source rate/channels
 2. Gapless splice      → sample-exact concatenation across track boundaries (§8.4)
 3. ReplayGain          → single scalar gain, per-track or per-album (§8.9.4)
 4. Channel matrix      → balance, mono downmix, channel remap (§8.9.5)
 5. Equalizer           → cascaded biquads, per channel (§8.9.1)
 6. Effects chain       → bass boost, stereo widener, reverb — each bypassable (§8.9.3)
 7. Tempo / pitch       → SoundTouch, bypassed entirely at 1.0×/0 cents (§8.9.2)
 8. Resample            → libsamplerate, only if source rate ≠ device rate (§8.6)
 9. Fade / crossfade    → transport and boundary envelopes (§8.5)
10. Volume              → user volume, applied AFTER fades so fades are volume-independent
11. True-peak limiter   → transparent unless engaged (§8.9.8)
12. Dither + convert    → to device format, dither only when reducing to fixed point
13. Interleave → Sink
```

Three positions in that list are deliberate and are the ones a refactor is most
likely to disturb:

- **ReplayGain precedes the EQ**, so the EQ operates on loudness-normalised
  material. Swap them and every EQ preset behaves differently depending on how
  loud the album was mastered.
- **Volume follows the fades**, so a fade sounds identical at any volume. If
  volume came first, a 150 ms fade-out at 20 % volume would be a fade over a
  fifth of the range and would sound like a cut.
- **The limiter is last before format conversion**, so nothing downstream can
  reintroduce clipping.

`REQ-AUD-005`: every stage is individually bypassable, and bypass is a **true**
bypass — the stage is *skipped*, not run with neutral parameters. Running a
biquad at 0 dB still costs cycles and still perturbs the last bits. This is
asserted by test 2, the null test.

`REQ-AUD-006`: in bit-perfect mode, stages 3–12 are all bypassed and stage 1's
output is converted to the device format with no arithmetic other than the
integer format change.

## Threads and the real-time rules

```text
  Decoder thread A (current track) ──┐
                                     ├──▶ 2 × lock-free SPSC ring ──▶ RT callback ──▶ Sink
  Decoder thread B (next track)   ──┘                                    │
                                                                         ├──▶ event ring ──▶ UI
                                                                         └──▶ spectrum ring ──▶ visualizer
```

`REQ-AUD-010`: exactly **two** decoder threads — one for the currently playing
source, one prefetching the next. That is what makes gapless and crossfade
possible without allocating on the boundary.

`REQ-AUD-011`: each decoder thread owns a **single-producer / single-consumer**
lock-free ring of pre-allocated planar float frames, using `std::atomic` with
explicit `memory_order_acquire` / `memory_order_release` on the read and write
indices. `memory_order_seq_cst` is permitted only where profiling shows it free.

`REQ-AUD-012`: ring capacity is **≥ 4 × the device period** and **≥ 250 ms** of
audio, whichever is larger, allocated once when the stream opens. Resizing a ring
while streaming is forbidden; a format change closes and reopens the stream.

`REQ-AUD-013`: prefetch starts when the current track has
**≤ max(5 s, crossfade duration + 2 s)** remaining, so a splice never waits on
I/O or on codec initialisation. `REQ-AUD-014`: if prefetch has not produced at
least one full device period by the boundary, the engine inserts silence rather
than blocking the RT thread, emits `AudioEvent::PrefetchUnderrun`, and logs it
once per occurrence — **from the UI thread, never the RT thread**.

### Forbidden inside the callback

`REQ-AUD-015` admits no exceptions:

| Forbidden | Why |
|---|---|
| `malloc`, `free`, `new`, `delete`, any container that may allocate | unbounded latency, may take a global lock |
| `std::mutex`, `std::condition_variable`, any blocking primitive | priority inversion → dropout |
| file I/O, socket I/O, any syscall that may block | unbounded latency |
| logging of any kind | allocates, locks, does I/O |
| C++ exceptions (`throw`) | unbounded unwinding, may allocate |
| `std::shared_ptr` copy on the hot path | atomic refcount contention |
| anything time-unbounded (`std::regex`, sorting unbounded input) | deadline miss |
| reading a `std::string` owned by another thread | data race |

The list is not a style guide. Each row is a mechanism by which the callback
misses its deadline, and a missed deadline is an audible click.

### How parameters reach the callback

`REQ-AUD-016`: parameter changes — volume, EQ gains, bypass flags, ReplayGain —
reach the RT thread through a **lock-free parameter snapshot**. The UI thread
writes a fully-populated, pre-allocated parameter block and publishes it with a
single `release` store of an index; the RT thread reads it with a single `acquire`
load. Individual parameters that the RT thread is reading are never mutated in
place.

This is why `Equalizer::configure()` is documented as *not* RT-safe while
`Equalizer::process()` is. `configure()` allocates and computes transcendental
functions; it belongs on the UI thread, and its output is what gets published.

`REQ-AUD-085` adds the audible half of the same rule: coefficient sets are
cross-ramped over **32 ms** rather than swapped instantaneously, because an
instantaneous coefficient change is a discontinuity in the filter state and
sounds like a zipper. `kCoeffRampMs` in `audio/dsp/equalizer.hpp` is that
constant; the ramp itself belongs to the RT graph, which is not written yet.

### The annotation, and why it is enforced

`REQ-AUD-017`: every function callable from the RT thread carries `/// RT-SAFE:`
in its doc-comment, stating why. A function without that annotation must not be
called from the callback. [`tools/check-rt-safety.py`](../tools/check-rt-safety.py)
makes the annotation load-bearing: a function *claiming* RT safety while
containing `new`, a lock, a `throw`, container growth, `std::to_string`,
`std::shared_ptr`, or a log call fails the build. A false claim is worse than no
claim, because the next maintainer will trust it.

`REQ-AUD-018`: the callback is exercised under **ThreadSanitizer** in CI with a
mock sink driving it at realistic rates, concurrently with parameter changes,
seeks, and track transitions. The `linux-tsan` preset exists for exactly this;
what it does not yet have is a callback to drive.

### Latency budget

| Mode | Default period | User-selectable range |
|---|---|---|
| Shared (WASAPI / PulseAudio / ALSA `plughw`) | **20 ms** | 5–200 ms |
| Exclusive (WASAPI excl. / ALSA `hw:`) | device default period | device-reported min–max, never below the reported minimum |
| Android (AAudio) | device-preferred burst | multiples of the burst |

`REQ-AUD-020`: total engine latency (decode ring + DSP + sink) is reported in the
UI to the millisecond and must be within **15 %** of measured wall-clock latency
(test 11). `REQ-AUD-021`: worst-case callback execution stays **below 50 % of the
period** with the full chain engaged — 18-band EQ plus all effects at
192 kHz / 2 ch — on the §20.1 reference hardware, benchmarked in CI with a
regression threshold of 10 %.

## Gapless — the formulas, per format

> `REQ-AUD-035`, the definition: for two tracks encoded from one continuous
> source, the concatenated decoded output must be **sample-identical** to the
> decoded output of the original continuous source, with **zero** inserted silence
> and **zero** dropped samples at the boundary.

That is a byte-comparable claim, and §8.11 test 3 compares the bytes. It is also
the single most-faked feature in music players, which is why the metadata parsing
below was the first audio code written in this repository and why each parser has
its own unit-test suite.

`REQ-AUD-036`: `GaplessInfo` is extracted at `open()` time.

```c
struct GaplessInfo {
    uint32_t skip_start_frames;   // encoder delay / priming to discard at the head
    uint32_t skip_end_frames;     // padding to discard at the tail
    uint64_t valid_frames;        // exact playable frame count, or UNKNOWN
    Source   source;              // Native | XingLame | ITunSMPB | OpusHead | Granule | None
};
```

`source` is not bookkeeping. `REQ-AUD-038` requires the UI to be able to explain
*why* a given boundary is not gapless, and "the source file lacks a LAME gapless
tag" is an honest answer that belongs in the technical-info panel. A player that
silently falls back teaches its user nothing.

### MP3 — Xing/Info plus the LAME tag

`REQ-AUD-037`: parse the first frame for a `Xing` or `Info` header. If a LAME
extension is present, read the 12-bit encoder delay and 12-bit encoder padding,
then:

```text
skip_start_frames = encoder_delay + DECODER_DELAY
skip_end_frames   = max(0, encoder_padding - DECODER_DELAY)

where DECODER_DELAY = 529   // MPEG-1 Layer III polyphase/MDCT filterbank delay
```

The 529 is not a fudge factor: it is the group delay every compliant Layer III
decoder introduces, so it must be added to the head and subtracted from the tail.

`REQ-AUD-038`: with no LAME tag, fall back to `skip_start_frames = DECODER_DELAY`,
`skip_end_frames = 0`, `source = None`. `REQ-AUD-039`: validate the LAME tag's
CRC where present — a tag failing CRC is **ignored, not trusted**.

Finding the tag needs the frame header first, because the tag sits after the
header plus the Layer III side information, whose size depends on MPEG version
and channel mode. That is why `parse_xing_lame()` takes a parsed header rather
than a raw offset.

### AAC and ALAC in MP4 — `iTunSMPB`

`REQ-AUD-040`: read the `----:com.apple.iTunes:iTunSMPB` free-form tag, whose
value is a space-separated run of hexadecimal fields, and use:

| Field | Meaning | Maps to |
|---|---|---|
| 2 | priming / encoder delay | `skip_start_frames` |
| 3 | remainder / padding | `skip_end_frames` |
| 4 | original sample count (64-bit) | `valid_frames` |

`REQ-AUD-041`: absent the tag, fall back to the `esds`/decoder-reported priming —
commonly 1024 or 2048 frames for AAC-LC — and set `source = None`.

`REQ-AUD-042`: the parser is **defensive**. A wrong field count, a non-hex
character, or a value exceeding the file's frame count rejects the tag outright.
It never produces a negative or overflowing skip. It is untrusted input in the
ordinary sense — the tag arrives inside a file the user downloaded — so it is
fuzzed today by `tests/fuzz/fuzz_gapless.cpp`, which drives the tag *value*
through all twenty-one of that harness's committed seeds, six of them written for
this parser in particular. `fuzz_mp4atoms`, the target `REQ-SEC-011` names,
does not exist yet: it belongs to the atom *tree* the value arrives in, which is
Phase 2 work. See `docs/TESTING.md` and `OQ-043`.

### Opus

`REQ-AUD-043`: read `pre_skip` from the `OpusHead` identification packet and
discard exactly that many frames **measured at 48 kHz**, rescaling if the output
rate differs. Also read `output_gain` (Q7.8 dB) and apply it as RFC 7845
requires — **independently of, and in addition to, ReplayGain**. Those are two
different gains with two different jobs, and collapsing them is a real bug that
sounds like "Opus files are quiet".

### FLAC, WavPack, APE, WAV, ALAC

`REQ-AUD-044`: these carry exact frame counts natively, so
`skip_start = skip_end = 0` and `valid_frames` comes from `STREAMINFO` or the
container header. For FLAC, prefer the seektable; if absent, binary-search the
frame headers and record in the technical-info panel that the file has no
seektable.

### Ogg Vorbis

`REQ-AUD-045`: use the granule position of the final page for exact length and
truncate the final block accordingly. A **negative initial granule position** —
an encoder-trimmed start — must be honoured.

### The splice itself

`REQ-AUD-046`: the `GaplessScheduler` runs **inside** the RT callback and must:

1. consume from ring A until `valid_frames - skip_end_frames` is reached;
2. switch to ring B **within the same callback invocation**, mid-buffer, filling
   the remainder of the output block from ring B starting after
   `skip_start_frames`;
3. emit a `TrackChanged` event through the event ring — never call into layer 4
   directly;
4. do all of it with no allocation, no lock, and no branch on file I/O.

Step 2 is the whole feature. Waiting for the next callback to start track two
inserts exactly one device period of silence, which is the gap that "gapless"
players ship with.

### Gapless versus crossfade

`REQ-AUD-047`: they are mutually exclusive per boundary. Precedence, highest
first:

| Situation | Behaviour |
|---|---|
| user pressed *next* manually | crossfade (if enabled), regardless of gapless metadata |
| same album **and** both sides carry valid gapless metadata | **gapless splice**, crossfade suppressed |
| crossfade enabled, metadata absent or albums differ | crossfade |
| crossfade disabled | gapless splice if metadata allows, else hard cut with the §8.5 boundary fade |

`REQ-AUD-048` makes row 2 a user-facing setting — **"Always gapless within an
album"**, default **on**. `REQ-AUD-049`: a format change across the boundary
(44.1 → 96 kHz, stereo → mono) makes a sample-exact splice impossible, so the
engine performs a **40 ms crossfade** to mask the device reconfiguration and emits
`AudioEvent::BoundaryFormatChange` so the panel can explain it.

## Fades and crossfade

`REQ-AUD-050` requires seven independently configurable fades: play (150 ms),
pause (150 ms), stop (300 ms), seek (60 ms each way), manual next/previous
(200 ms), automatic crossfade (0 ms — off by default), and sleep-timer expiry
(20 s).

`REQ-AUD-051`: crossfade uses an **equal-power** curve, not linear:

```text
gain_out(t) = cos(t · π/2)
gain_in (t) = sin(t · π/2)      for t ∈ [0,1]
```

Two linear ramps sum to a dip of about −3 dB mid-transition and sound like a hole.
Only the seek and sleep-timer fades ramp linearly, where the dip is inaudible or
wanted. Test 4 exists to fail a linear implementation.

`REQ-AUD-052` places fades at stage 9, before volume. `REQ-AUD-053`: pausing
fades out, **then** stops pulling from the ring, and does not discard buffered
audio; resuming fades in from the same sample position. A pause/resume cycle is
sample-lossless, asserted by test 13. `REQ-AUD-054`: a crossfade shorter than its
configured duration is *shortened*, never allowed to truncate the outgoing track.

## Resampling and dither

`REQ-AUD-056`: resample **only** when the source rate differs from the negotiated
device rate; when they match, the resampler is bypassed entirely. Quality tiers
map to libsamplerate converters: **Best** (`SRC_SINC_BEST_QUALITY`, desktop
default), **Balanced** (`SRC_SINC_MEDIUM_QUALITY`), **Fast**
(`SRC_SINC_FASTEST`), and **Minimal** (`SRC_LINEAR`, diagnostics only and
labelled as low quality).

`REQ-AUD-058` gives the user three device-rate policies: **follow source**
(reconfigure the device per track — best fidelity), **fixed rate** (pin and
resample everything), and **follow source family** (switch only between the
44.1 kHz family and the 48 kHz family, resampling within one) — the recommended
compromise, because it avoids restarting the device on every track.

`REQ-AUD-059` — **dither**: when converting float32 to a fixed-point device
format of **16 bits or fewer**, apply TPDF dither at 1 LSB amplitude. For 24- and
32-bit output, dither must **not** be applied: it is inaudible there and it breaks
bit-perfect verification. Dither is bypassed in bit-perfect mode.

## Bit-perfect mode — the binding contract

`REQ-AUD-075`: when the indicator is lit, **all nine** of these hold. If any
cannot, the mode refuses to engage and states **which** condition failed.

| # | Guarantee |
|---|---|
| 1 | the sink is in exclusive/direct mode (WASAPI exclusive, ALSA `hw:`, or ASIO) |
| 2 | device rate equals source rate exactly — no resampling |
| 3 | device channel count equals source channel count — no mixing or upmixing |
| 4 | device bit depth ≥ source bit depth |
| 5 | volume is fixed at unity and the software volume control is **disabled in the UI**, not merely set to 100 % |
| 6 | ReplayGain, EQ, all effects, tempo/pitch, balance, and the limiter are bypassed — true bypass |
| 7 | no dither |
| 8 | no fade, including transport fades; crossfade is unavailable |
| 9 | the only arithmetic between decoder output and device is the integer format conversion, which is exact and covered by a bit-equality unit test |

Condition 5 is the one that separates a real implementation from a claim. A
volume slider that still works is a multiply, and a multiply is not bit-perfect.

`REQ-AUD-076`: the mode is **verified, not asserted** — test 5 must demonstrate a
byte-identical round trip for at least one device configuration in CI or on
documented reference hardware. If CI cannot verify it, the limitation is recorded
in the parity matrix and the manual test goes into the release checklist.
`REQ-AUD-077`: the indicator has three states — active, available but not active,
and unavailable **with the specific failed condition** named, not a generic
message.

Why this needs our own sink layer at all is in
[`docs/ARCHITECTURE.md`](ARCHITECTURE.md#why-the-sink-layer-is-ours): conditions
1–3 require share-mode, device-period, and clock control that general-purpose
wrappers do not expose, and a wrapper that converts formats internally violates
condition 9 by construction.

## Robustness and recovery

**Underruns** (`REQ-AUD-115`): the callback outputs silence for the missing
frames, increments an atomic counter, and continues. It does not block, retry, or
attempt recovery — all three are forbidden on that thread. `REQ-AUD-116`: rolling
statistics feed a diagnostics panel, and more than **3 underruns in 10 seconds**
raises the buffer period by one step (up to the configured maximum) and tells the
user once, non-modally.

**Device loss** (`REQ-AUD-117`) covers all of: unplug, default-device change,
another application changing the device format, exclusive mode being stolen,
PulseAudio/PipeWire server restart, Windows session change, Bluetooth
disconnect, and suspend/resume. `REQ-AUD-118` requires an **explicit state
machine**, not scattered conditionals:

```text
        ┌──────────┐   open ok    ┌─────────┐   start   ┌─────────┐
        │  Closed  │─────────────▶│ Opened  │──────────▶│ Running │
        └──────────┘              └─────────┘           └─────────┘
             ▲                         ▲                   │
             │                         │      device lost / │
             │  give up after          │      format change │
             │  N attempts             │                    ▼
             │                    ┌──────────┐  backoff ┌──────────┐
             └────────────────────│  Failed  │◀─────────│Recovering│
                                  └──────────┘  exhausted└──────────┘
                                                            │ retry
                                                            └──▶ (reopen)
```

`REQ-AUD-119`: retry backoff is **100 ms, 250 ms, 500 ms, 1 s, 2 s, 5 s** — six
attempts — preserving exact position and playing/paused state. On `Failed`, the
queue and position stay intact, playback enters paused, and the error names the
device. `REQ-AUD-120`: follow the system default when the user chose "system
default"; when the user **pinned** a device, never silently move — report that the
pinned device is gone.

`REQ-AUD-121` — format renegotiation: fade out (40 ms) → stop → close → reopen
with the new format → fade in, position preserved, within **300 ms** on typical
hardware, falling back to resampling into the existing stream if the reopen fails.

## How each claim is proven

`REQ-TST-001`: each row below must exist as an automated test. The **Status**
column is this repository today, not an aspiration.

| # | Claim | Verification | Status |
|---|---|---|---|
| 1 | decode correctness | every §8.3.1 format against a reference WAV from an independent tool: RMS error < −120 dBFS for lossless, bit-exact for WAV/FLAC/ALAC/WavPack | not written — needs the FFmpeg adapter |
| 2 | **null test** | decode → full chain, every stage bypassed → capture; assert **bit-identical** to the decoder output | not written — needs the DSP graph |
| 3 | gapless boundary | split one continuous source; play both halves; assert the concatenation is sample-identical to the continuous decode. MP3 (LAME), AAC (`iTunSMPB`), FLAC, Opus, Vorbis, WavPack | not written — the metadata half is unit-tested (51 cases) |
| 4 | crossfade equal-power | summed RMS through the transition within ±0.5 dB of steady state | not written |
| 5 | bit-perfect loopback | bytes delivered to the sink identical to the file's PCM payload | not written — needs a sink |
| 6 | EQ transfer function | white noise, FFT magnitude vs the analytic cascade, ±0.25 dB over 20 Hz–20 kHz | half: the analytic side is implemented and tested (`magnitude_db`, 9 response cases); the measured side is not |
| 7 | THD+N | 1 kHz full scale, bypassed: < −100 dB; EQ flat and enabled: < −90 dB | not written |
| 8 | no clipping | +6 dB ReplayGain material, limiter on: no sample and no 4× inter-sample peak over the ceiling | not written |
| 9 | RT safety | TSan, mock sink at 5 ms periods, 60 s of concurrent volume/EQ/preset/seek/skip changes; zero findings, zero underruns | not written — the `linux-tsan` preset is ready, the callback is not |
| 10 | RT allocation freedom | override the global allocator during a callback-driven test; assert **zero** allocations inside the callback | not written |
| 11 | latency accuracy | reported vs measured loopback latency within 15 % | not written |
| 12 | recovery | simulate device loss, format change, server restart; position within ±1 frame, state restored | not written |
| 13 | pause losslessness | 100 random pause/resume cycles, output sample-identical to uninterrupted playback | not written |
| 14 | desktop/Android DSP parity | same impulse and settings through both implementations, RMS error within −90 dBFS (`REQ-AUD-108`) | untestable — the Android app is a Phase 0 scaffold with no DSP ([ADR 0012](adr/0012-restore-android.md) restored the target; OQ-018) |
| 15 | seek exactness | 1000 random seeks per format; first decoded frame matches the reference at that index | not written |

Thirteen of fifteen are not written because the engine they test does not exist
yet: there is no decoder adapter, no ring buffer, no RT thread, no sink. What is
written is the arithmetic those tests will be checking, and that arithmetic is
under test now — see below. The corpus that tests 1, 3, and 15 need is §29.4's
job; generating the MP3-LAME half of it needs an encoder, which
[OQ-024](OPEN-QUESTIONS.md) resolves.

Nothing in the table is marked passing that is not. `docs/TESTING.md` carries the
suite inventory and [`docs/OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md) §5 carries the
verification register.

## What exists today

Three of the engine's modules are implemented, all in layer 3, all pure C++20:

| Module | Requirements | Unit tests |
|---|---|---|
| `audio/dsp/biquad.hpp` | `REQ-AUD-082` RBJ coefficients, `REQ-AUD-083` Q from bandwidth, `REQ-AUD-084` Nyquist bypass, Direct Form I with float64 state | 26 (response, Q, Nyquist, bypass, stability, reset, cascade) |
| `audio/dsp/equalizer.hpp` | `REQ-AUD-080`–`REQ-AUD-088`: both graphic band sets, parametric mode, ranges, presets, computed response | 30 (bands, settings, presets, cascade response) |
| `audio/decode/gapless_info.hpp` | `REQ-AUD-036`–`REQ-AUD-045`: MPEG header, Xing/LAME, `iTunSMPB`, `OpusHead`, granule, native | 49, plus 3 randomised-input cases |

`ctest` reports **210 passing** over the whole tree — 206 GoogleTest cases from
the three modules above plus `core/error`, `core/text`, `core/json` and the
application layer, and four fuzz-corpus replays. That number is a local run, not
a claim: see
`docs/TESTING.md` for how to reproduce it and what it does and does not cover.

Direct Form I is chosen over transposed forms for a reason that belongs here: its
state is the raw input/output history, so it stays meaningful when coefficients
change, which is what makes the `REQ-AUD-085` cross-ramp well-behaved. Float64
state is likewise not caution — float32 state accumulates audible error at low
frequencies, and `REQ-AUD-082` requires float64 explicitly.

Not yet written, in the order Stage 4 needs them: the `IDecoder` and `IAudioSink`
ports, the SPSC ring, the RT thread, the gapless scheduler, the transport fades,
the FFmpeg adapter, and the file-backed sink that makes tests 2, 3, and 5
possible without a sound card.

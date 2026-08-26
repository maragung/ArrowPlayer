# Eclipse Player

A free, open-source, privacy-first music player for people who own their music
files, care about audio fidelity, and want to make the player theirs.

**Status: early development.** The specification is complete; implementation is
in progress. See [Implementation status](#implementation-status) for exactly what
works today — no feature is claimed here before it is built and tested.

- **Specification:** [`eclipse-player.md`](eclipse-player.md) — 4,200 lines, 525
  numbered requirements, each with acceptance criteria
- **Licence:** [MPL-2.0](LICENSE) ([why](docs/adr/0001-project-license.md))

## What it is meant to be

Two siblings sharing one design language, one skin format and one library model:

- **Desktop** — a single C++20 codebase producing native builds for Windows 10/11
  and Ubuntu 22.04/24.04 LTS
- **Android** — a native app with full Android Auto integration

Both are offline-first and standalone. No accounts, no ads, no telemetry. Every
network feature is off until you turn it on.

## The three differentiators

**1. A real skin engine.** Per-skin *layout* control, not palette swapping — and
safe enough to install from the internet. Skin packages contain **no executable
code**: a token tier for themes and a declarative, non-Turing-complete layout DSL
for skins. The design goal is that a malicious skin can, at worst, produce an
ugly UI — never code execution ([ADR 0003](docs/adr/0003-no-code-in-skins.md)).

**2. Audio-engineering credibility.** Sample-exact gapless playback verified by
byte comparison, genuine bit-perfect output with a binding nine-point contract,
and a DSP chain with published coefficients. Every audio claim has a test that
proves it (§8.11).

**3. Privacy as an engineering property.** Not a policy in a README — a build
that fails. CI scans the dependency graph against an analytics denylist, and a
zero-connection test asserts no outbound traffic during a full session.

## Implementation status

| Module | Status | Tests |
|---|---|---|
| Error taxonomy, `Result<T>` (§22.1) | done | 13 |
| UTF-8 / text / sort keys (§9.2.3–9.2.4) | done | 43 |
| Hardened JSON parser (§21.2) | done | 25 |
| RBJ biquads + 10/18-band EQ (§8.9.1) | done | 56 |
| Gapless metadata: Xing/LAME, iTunSMPB, OpusHead, granule (§8.4) | done | 52 |
| Fuzz harnesses + committed corpus (§21.6, `REQ-SEC-011`) | 1 of 17 named, 3 supporting | 4 replays, 49 seeds |
| `shared-spec/` contract: schemas, grammars, 455 fixtures (§11, §10, §9.6) | done | 455 fixtures |
| Architecture gates: layers, SQL safety, RT safety, spec/fixture sync | done | 4 gates |
| Audio graph, sinks, decoders | not started | — |
| Library index, scanner, tagging | not started | — |
| Format strings, smart playlists, theme engine | not started | — |
| Qt UI | not started | — |

**193 tests, all passing** from a clean build — the 189 GoogleTest cases in the
table above plus the four fuzz-corpus replays — clean under `-Werror` with a
strict warning set, and passing again under ASan+UBSan and under ThreadSanitizer.
Reproduced locally on GCC 14.2 / CMake 3.31 / Ninja 1.12, not asserted from CI
alone. Two things are unavailable locally: Qt, so the UI is **CI-verified only**;
and Clang, so the libFuzzer binaries are built only in CI and just the corpus
replay runs here. Both are recorded in
[`docs/OPEN-QUESTIONS.md`](docs/OPEN-QUESTIONS.md).

Notable results already verified:

- The EQ's analytic frequency response matches its **measured** response (impulse
  → DFT) within ±0.25 dB across 20 Hz–20 kHz — the independent cross-check
  required by §8.11 test 6.
- A bypassed filter chain returns **bit-identical** samples, not merely close
  ones (§8.11 test 2, the null test).
- MP3 gapless trim follows the spec formula exactly, and a LAME tag failing CRC
  is **ignored rather than trusted** (REQ-AUD-039).
- Byte parsers survive several thousand pseudo-random and bit-flipped buffers
  under ASan+UBSan without a crash, hang, or out-of-range trim value.
- The fuzz corpora found two real defects on their **first** replay, before
  mutating anything: `normalize_relative_path()` accepted an absolute path and a
  filename containing a NUL, both of which `REQ-THM-018` requires an archive entry
  be rejected for; and `gapless_from_granule()` negated `INT64_MIN`, which is
  undefined behaviour, on a value read straight out of an Ogg page. Both fixed, and
  each pinned by a test that states the invariant that broke.

## Build

```bash
cd desktop
cmake --preset linux-release      # or windows-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

Needs only a C++20 compiler, CMake 3.28 and Ninja. Every external library is
optional at configure time — the domain layer, where the audio maths and parsers
live, links against nothing but the standard library by design (REQ-GEN-050).

Full instructions, including Qt and the optional adapters:
[`docs/BUILDING.md`](docs/BUILDING.md).

## Architecture

Five layers with a mechanically enforced dependency direction (§7.2):

```text
5  PRESENTATION   QML + Widgets  |  Compose        no business logic
4  APPLICATION    use cases, playback session, queue
3  DOMAIN         pure entities and rules          zero dependencies
2  PORTS          IDecoder, IAudioSink, ITagReader, ILibraryIndex
1  ADAPTERS       FFmpeg, WASAPI/ALSA, TagLib, SQLite, Qt, projectM
```

Rules are enforced by scripts, not review:

```bash
python3 tools/check-layers.py      # layer direction        (REQ-GEN-051)
python3 tools/check-sql-safety.py  # no interpolated SQL    (REQ-SEC-009)
python3 tools/check-rt-safety.py   # RT-SAFE claims are true (REQ-AUD-017)
python3 tools/check-hardening.py   # hardening in the binary (REQ-SEC-018)
```

The last of those reads the ELF or PE headers of the linked binaries, because
`REQ-SEC-018` asks for the flags to be verified in the artifact rather than in
the build files — and the function that sets them had been called by nothing for
several commits, so the build files would have answered yes.

Four of the scripts under `tools/` carry a `--self-test` that plants the defect
they exist to catch and requires it to be caught; the three source-level gates
above do not have one yet, which is [OQ-045](docs/OPEN-QUESTIONS.md). An earlier
version of this line claimed all of them did.

## Design references

Studied for behaviour and design, never for assets:

- **Winamp** — the plugin SDK (7 categories; 66 plugins within 9 months of the
  1998 release) is why it outlived its owner. Also two-tier skins, title
  formatting, windowshade, and MilkDrop's preset ecosystem.
- **AIMP** — its FFmpeg-LGPL-plus-source-offer arrangement is the exact
  compliance pattern we adopt. Also the published Windows/Linux parity matrix,
  extended fade rules, separate tempo/pitch/speed controls, and cue sheets.
- **foobar2000** — title-formatting semantics and rule-based smart playlists.

Both Winamp and AIMP were also studied for what to **reject**: §2.4 names 11
non-goals outright, including the scope sprawl that hollowed out Winamp.

## Non-goals

Refused, not deferred: video playback, CD ripping/burning, portable-device sync,
cloud streaming, DRM, NFT/crypto features, bundled offers, mandatory accounts,
default-on analytics, macOS/iOS builds (no CI hardware or maintainer).

## Contributing

Read [`eclipse-player.md`](eclipse-player.md) first — it is a specification, not
a suggestion list. Then §1.3, the Definition of Done.

Work proceeds in the phases defined in §28. Each phase has hard exit gates, and
the next phase does not start until they are green on every platform. Notably
Phase 1 cannot pass without the null test and sample-exact gapless verification,
because every later phase assumes the audio path is provably correct.

If a requirement is wrong, write an ADR and change the specification
deliberately. Do not weaken a requirement to make a gate pass.

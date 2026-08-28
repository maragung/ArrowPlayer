# Roadmap

Derived from the MoSCoW tiers in `eclipse-player.md` §0.3. This document covers
what comes **after** 1.0.0 — the `[v1.x]` and `[v2]` tiers — and the §2.4
non-goals, which are refused rather than deferred.

For what 1.0.0 itself contains and in what order, see §28 (build phases and exit
gates). For what exists **today**, see [`PARITY.md`](PARITY.md) and the
`CHANGELOG.md` "Not yet true" section — this file is about intent, and intent is
the cheapest thing in a repository.

## The tier rules

These are constraints on how this roadmap may be used, not just labels:

| Tier | Meaning | Rule |
|---|---|---|
| `[v1.0]` | **MUST.** Ships in 1.0.0. | 1.0.0 cannot be tagged while any `[v1.0]` requirement is unmet. |
| `[v1.x]` | **SHOULD.** Ships in a 1.x minor. | Architecture must leave a seam for it — **no implementation before 1.0.0 ships.** |
| `[v2]` | **COULD.** Post-1.x. | Recorded only so the v1 design does not preclude it. |
| `[NON-GOAL]` | **WON'T.** | Refused if requested. |

The `[v1.x]` rule is the one that gets broken in practice, so it is worth saying
plainly: **a `[v1.x]` feature implemented before 1.0.0 ships is a defect**, not
progress. It consumes the effort a `[v1.0]` requirement needed and it adds
surface that has to be maintained through the 1.0.0 hardening phase. The correct
`[v1.x]` work before 1.0.0 is leaving a clean extension point and nothing more.

## `[v1.x]` — after 1.0.0 ships

56 requirements carry this tier as their whole-requirement tag. Grouped by
area, with the reason each waits.

| Area | Count |
|---|---|
| NET (network features) | 17 |
| PLG (plugin SDK) | 13 |
| AUD (audio) | 7 |
| LIB (library and metadata) | 5 |
| UIX (UI/UX) | 5 |
| THM (theme and skin) | 3 |
| GEN, OSI | 2 each |
| PLS, SYN | 1 each |

### Network features — 17 requirements

Radio (`REQ-NET-010`…`015`), podcasts (`REQ-NET-020`…`022`), metadata and artwork
lookup (`REQ-NET-030`…`033`), scrobbling and credential storage
(`REQ-NET-040`…`043`).

*Why it waits:* every one of these is an outbound connection, and §19.5 makes the
zero-connection default a binding contract with a test behind it (`REQ-TST-023`).
The `IHttpClient` single-chokepoint design (`REQ-NET-002`) has to exist and be
proven before anything is allowed to use it. Shipping a player that plays local
files perfectly and connects nowhere is a coherent 1.0.0; shipping one with
half-finished network features is not.

*Seam required in v1.0:* `IHttpClient` as a port, the global network switch
(`REQ-NET-001`) gating it, and the credential storage abstraction
(`REQ-NET-043`).

### Plugin SDK — 13 requirements

`REQ-PLG-001`…`013`: the C ABI, seven categories, capability model, consent flow,
crash quarantine, RT watchdog.

*Why it waits:* a plugin ABI is a compatibility promise that lasts as long as the
project. Making it before the audio engine, theme engine and library are stable
means promising interfaces to code that has not settled. Winamp's ecosystem
outlived its owner because its ABI did not change; that only works if the ABI is
right the first time.

*Seam required in v1.0:* the ABI shape itself, decided now in
[ADR 0004](adr/0004-plugin-c-abi.md) — pointer-free, buffer-explicit, C only —
because `REQ-PLG-011` item 5 (out-of-process hosting for the parser plugins)
becomes impossible if the interfaces assume a shared address space.

### Audio — 7 requirements

Native PipeWire (`REQ-AUD-069`), JACK (`REQ-AUD-070`), ASIO on Windows
(`REQ-AUD-071`), the RtAudio fallback (`REQ-AUD-073`), silence removal at
boundaries (`REQ-AUD-055`), BPM detection (`REQ-AUD-093`), per-device DSP
profiles (`REQ-AUD-106`).

*Why it waits:* ALSA and PulseAudio cover Linux for 1.0.0. PipeWire is the
correct modern backend and is genuinely wanted — §29.6 item 2 records that the
earlier draft claimed PipeWire support via RtAudio, which has **no PipeWire
backend at all** — but a fourth Linux backend is depth, not coverage.

*Seam required in v1.0:* `IAudioSink` with no assumptions that only ALSA or
PulseAudio can satisfy ([ADR 0002](adr/0002-audio-output.md)).

### Library and metadata — 5 requirements

Tag-from-filename and filename-from-tag (`REQ-LIB-037`), rename/organise on disk
(`REQ-LIB-038`), custom artwork per track and album (`REQ-LIB-069`), the
converter (`REQ-LIB-080`), the audio cutter (`REQ-LIB-081`).

*Why it waits:* these write to the user's files. `REQ-LIB-038` in particular
moves them. Getting the read path — scanner, tags, cue sheets, artwork — correct
and tested first is the prerequisite for being trusted with the write path. The
converter also needs encoders, which means relaxing the decode-only FFmpeg
configuration (`REQ-GEN-016`) — selectively, and only for LGPL-clean encoders.

### UI/UX — 5 requirements

Windowshade mode (`REQ-UIX-014`), detachable panels (`REQ-UIX-015`), the Android
Quick Settings tile (`REQ-UIX-029`), in-app lyrics editing (`REQ-UIX-049`), the
Localization Editor (`REQ-UIX-077`).

*Why it waits:* windowshade and detachable panels are strong Winamp-parity items
and are wanted, but both interact with the skin engine's layout model. Building
them before the layout DSL has shipped and been used by real skin authors means
guessing at what floating and collapsed states need to express.

### Theme and skin — 3 requirements

`tools/skin-editor` (`REQ-THM-062`), icon packs as a separate `.arrowicons`
artefact (`REQ-THM-063`), a public skin gallery with screenshots, search and
author pages (`REQ-THM-064`).

*Why it waits:* an authoring tool for a format that has not been used yet
optimises for imagined authors. The format ships first; the tool follows the
friction real authors report. AIMP shipped a Skin Editor and it mattered — but
after the format was established.

### OS integration — 2 requirements

Windows jump list (`REQ-OSI-005`), Android alarm / wake-with-music
(`REQ-OSI-045`).

### Playlists — 1 requirement

Stop after current track, plus shutdown/sleep/hibernate on completion
(`REQ-PLS-044`).

### Sync — 1 requirement

The optional self-hosted relay server (`REQ-SYN-013`).

*Why it waits:* LAN sync needs no server, and the server must never become
required. It is a store-and-forward relay of **already-encrypted** change sets
that cannot read library contents, and no default configuration may point at any
hosted instance. Building it after LAN sync ships keeps that ordering honest —
the relay is an addition to a working system, not the way sync works.

### General — 2 requirements

Selective LGPL-clean encoders for the converter (`REQ-GEN-016`) — FFmpeg's native
`flac`, `alac`, `opus`, `vorbis`, `wavpack`, `pcm_*`; **`libfdk_aac` never**
(non-free); `libmp3lame` permitted but only as a separately-enabled component so
the default build stays minimal. And the licence side of ASIO (`REQ-GEN-018`):
behind `ARROW_ENABLE_ASIO`, default off, Steinberg SDK supplied by the user,
never vendored, never in a release artifact (§4.6). `REQ-AUD-071` is the same
feature seen from the audio side.

## Tier markers inside `[v1.0]` requirements

Three requirements ship in 1.0.0 but carry a deferred *part*. These are easy to
miss because the requirement's own tag is `[v1.0]`:

| Requirement | Ships in 1.0.0 | Deferred part |
|---|---|---|
| `REQ-AUD-030` **DSD honesty** | The separation itself — the UI must never conflate the two | DSD **decode-to-PCM** is `[v1.x]`; DoP/native **passthrough** is `[v2]` |
| `REQ-AUD-059` **Dither** | TPDF dither at 1 LSB for output of 16 bits or fewer | **Noise-shaped** dither is `[v1.x]` |
| `REQ-UIX-016` **Dialogs** | Preferences, Tag Editor, Equalizer, Skin Browser, Duplicate Finder, ReplayGain Scanner, About/Licences, Technical Info | The **Converter** dialog is `[v1.x]`, following `REQ-LIB-080` |

`REQ-AUD-030` is worth reading in full. It exists because the earlier draft said
*"DSD where the decode library permits"*, which conflates decoding DSD to PCM
with passing DSD through to a DAC untouched — §29.6 item 9 records this as a
found defect. The requirement's binding clause is that passthrough
**MUST NOT be claimed, advertised, or implied before it exists**. That is a
constraint on marketing copy, release notes, and this file, not only on code.

## `[v2]` — recorded so v1 does not preclude them

Not planned, not promised. Present so that a v1 design decision does not
accidentally make them impossible.

| Item | Why it is only recorded | What v1 must not preclude |
|---|---|---|
| **Audio CD (CD-DA) playback** | Optical drives are near-extinct. Note that CD *ripping* and *burning* are non-goals (§2.4) — playback is a different, much smaller thing. | The decoder port must not assume a seekable file — a CD is a track-and-sector source |
| **Tracker modules** (MOD/XM/S3M/IT) | A distinct decode model — a synthesiser with instrument samples, not a stream decoder. Beloved by exactly the audience this player targets, which is why it is recorded rather than refused. | `IDecoder` must not assume one input file maps to one PCM stream with a known duration |
| **MIDI / KAR** | Requires a synthesiser and a soundfont. Out of v1 scope. | as above |
| **DoP / native DSD passthrough** | Requires exclusive-mode output, DoP marker encoding, and DAC-specific handling. `REQ-AUD-030` forbids claiming it before it exists. | The bit-perfect contract (§8.8) must be able to express "pass these bytes through untouched" |
| **Wear OS companion** | A separate app surface with its own constraints. | the Android `MediaSession` model must not assume a phone-sized UI |
| **Out-of-process hosting for Decoder and Metadata plugins** | The correct long-term answer to the fact that a native in-process plugin cannot be sandboxed (`REQ-PLG-011`). Those two categories parse untrusted data, so they are the ones worth isolating. | the plugin ABI must stay free of pointer-sharing beyond explicit buffers — the binding reason [ADR 0004](adr/0004-plugin-c-abi.md) chose C |

## `[NON-GOAL]` — refused, not deferred

From §2.4. If one of these is requested, the answer is this table.

| Refused | Rationale |
|---|---|
| Video playback | Doubles the decode and render surface for an audience that already has a video player. This is how Winamp got diluted. |
| CD ripping and burning | Optical drives are near-extinct; the code is high-maintenance and platform-specific. Audio-CD *playback* is `[v2]`. |
| Portable-media-device sync (iPod, MTP, PlaysForSure) | An enormous device-quirk matrix for a vanishing user base. |
| Cloud music streaming / subscription services | Contradicts offline-first; requires accounts and DRM. |
| DRM of any kind | Would require a proprietary component and closed-source blobs in an MPL-2.0 project. |
| NFT, crypto-wallet, or blockchain features | Actively user-hostile. Named explicitly because a reference player shipped this. |
| Bundled third-party offers, adware, or an "optimizer" upsell | Non-negotiable. |
| Mandatory account creation for any feature whatsoever | Sync pairs devices; it does not authenticate users (§18.2). |
| Analytics or telemetry enabled by default | §19.5 forbids it structurally — absent from the dependency tree, enforced by a CI denylist — not merely by policy. |
| macOS and iOS builds | No CI hardware and no maintainer. The architecture must not preclude them; we simply do not claim them. Someone with both could change this — it is a capacity refusal, not a design one. |
| A web / Electron / React-Native shell | Fails the native-integration and memory requirements in §20. |

Two further non-goals are declared inline rather than in the §2.4 table, so they
are easy to miss:

- **Upmixing** (`REQ-AUD-105`). The channel matrix downmixes multichannel to
  stereo using ITU-R BS.775 coefficients. It does not synthesise channels that
  were never recorded.
- **Syncing media files** (`REQ-SYN-003`). Sync carries playlists, play counts,
  skip counts, ratings, loved flags, bookmarks, resume positions, last-played
  timestamps and the shortcut map. Moving audio files is a file-sync tool's job.

Two of the table entries deserve emphasis, because they are the ones a
well-meaning contribution is most likely to breach:

- **"Analytics enabled by default"** is refused *structurally*. A pull request
  adding an analytics dependency does not get a policy discussion; it fails the
  dependency-denylist gate in `security.yml`. Adding it as opt-in still requires
  `REQ-SET-011`'s full ceremony — a prompt defaulting to no, field-by-field
  documentation, user inspection before sending, permanent disable, and a **major
  version bump**.
- **"Mandatory accounts"** extends to any future skin gallery: browsing must
  require no account, and only publishing may (`REQ-SET-016`).

## How this roadmap changes

By ADR, not by edit. Adding a `[v1.x]` item, promoting a `[v2]` item, or — in
particular — reconsidering a non-goal means writing an ADR with context,
decision, consequences and rejected alternatives, and assigning the requirement
an ID in the right area. See
[Changing the specification](../CONTRIBUTING.md#changing-the-specification).

Promoting a non-goal is not forbidden. The macOS row is a capacity refusal that a
maintainer with hardware could reverse. The NFT row is not.

# Privacy

`REQ-SET-015`. This document states, in plain language, what Eclipse Player
stores, where it stores it, what leaves your device and under exactly which
action, what never leaves, and **how you can verify all of it yourself** — because
a privacy claim you cannot check is a promise, and promises are not a security
property.

The short version: Eclipse Player is a local music player. With default settings
it makes **zero network connections**. There are no accounts, no analytics, no
crash reporting, no advertising identifiers, and no dependency in the build that
could provide them.

## The binding contract

Five requirements, quoted rather than paraphrased, because these are the
commitments the rest of this document explains:

- **`REQ-SET-010` — Zero telemetry.** No analytics SDK, no crash-reporting SDK, no
  attribution SDK, no advertising-id access, no fingerprinting. *Not disabled by a
  flag — absent from the dependency tree.* CI scans the resolved dependency graph
  against a denylist and fails the build. A policy that lives only in a README is
  not a guarantee; a build that fails is.
- **`REQ-NET-001` — Global network switch**, off by default, gating every outbound
  connection. With it off the app makes zero connections of any kind.
- **`REQ-SET-012` — Crash reporting is local-first.** A crash writes a file to
  your logs directory. Nothing is transmitted automatically, ever.
- **`REQ-SET-016` — No forced accounts anywhere**, including sync.
- **`REQ-SET-011`** — if analytics were ever introduced they would have to be
  opt-in with a prompt defaulting to no, documented here field by field,
  inspectable before sending, permanently disableable, and shipped with a **major
  version bump** and a changelog entry at the top of the release notes. In other
  words: it cannot happen quietly in a patch release.

## What is stored, and where

Everything is a plain file or a SQLite database on your own machine. Nothing is
encrypted at rest except secrets, which go to the OS secret store — there is no
cloud copy to protect.

| Data | Windows | Linux | Android |
|---|---|---|---|
| Settings | `%APPDATA%\EclipsePlayer\` | `$XDG_CONFIG_HOME/eclipse-player/` | DataStore, app-private |
| Library database | `%LOCALAPPDATA%\EclipsePlayer\` | `$XDG_DATA_HOME/eclipse-player/` | app-private `databases/` |
| Skins and plugins | `%APPDATA%\EclipsePlayer\skins\|plugins\` | `$XDG_DATA_HOME/eclipse-player/skins\|plugins/` | app-private `files/skins/` |
| Artwork cache | `%LOCALAPPDATA%\EclipsePlayer\cache\` | `$XDG_CACHE_HOME/eclipse-player/` | `context.cacheDir` |
| Logs | `%LOCALAPPDATA%\EclipsePlayer\logs\` | `$XDG_STATE_HOME/eclipse-player/logs/` | app-private `files/logs/` |
| Secrets (only if you enable a feature needing them) | Windows Credential Manager | Secret Service / kwallet | Android Keystore |

Paths come from `QStandardPaths` and the Android `Context` APIs; none are
hard-coded (`REQ-SET-003`).

**In portable mode** (`portable.txt` beside the executable, or `--portable`),
*everything* above lives in a `data/` directory inside the installation folder
and nothing is written to your user profile or the Windows registry
(`REQ-SET-005`). Portable mode is shown in Help → About so you can confirm it is
active, and it refuses to start rather than silently falling back if that
directory is not writable (`REQ-SET-006`).

### What the library database contains

The index of your music: file paths, tags, durations, audio properties,
artwork references, and your listening data — play counts, skip counts, ratings,
loved flags, bookmarks, resume positions, last-played timestamps.

This is the most personal data the app holds. It is a description of your taste
and your habits. It lives in one SQLite file that you can open with any SQLite
browser and read in full. It is never uploaded anywhere.

### What the caches contain

Decoded artwork and thumbnails. Caches live in the OS cache location precisely so
a system cleaner can delete them, and the app tolerates its cache vanishing at any
moment without data loss (`REQ-SET-004`). Deleting the cache directory is always
safe.

### What the logs contain

Diagnostics. `REQ-SET-013` requires that logs **never** contain credentials,
tokens, or sync keys, and that at `info` level and above filesystem paths are
reduced to basenames — `song.flac`, not `/home/you/Music/…/song.flac`. Full paths
appear only at `debug` and `trace`, and the log viewer warns you before you share
a `debug`-level log. Log sharing is always something you do deliberately.

## What leaves your device

With default settings: **nothing**.

Every network feature is off by default, and one switch (`REQ-NET-001`) gates all
of them. Each row below happens only after you turn that switch on *and* enable
the specific feature *and*, for most of them, take an explicit action:

| What leaves | Under which action | Where it goes |
|---|---|---|
| An HTTP request for a radio stream | You play a station you added | The stream URL you entered |
| A podcast feed fetch | You subscribe to a feed and it refreshes | The feed URL you entered |
| A metadata query — artist, album, track title, or an acoustic fingerprint | You ask for metadata lookup on specific tracks | MusicBrainz / AcoustID |
| An artwork request | You ask for artwork lookup | The configured artwork provider |
| A scrobble — track title, artist, album, timestamp | You connect a scrobbling account and play a track | The scrobbling service you connected |
| A version check | You enable update checking | The update endpoint |
| Change sets — playlists, play counts, ratings, loved flags, bookmarks, resume positions, shortcut map | You pair a device and sync | Directly to **your own** paired device on your LAN |
| A crash report | You read the local report and choose "send", which opens your browser with a pre-filled GitHub issue | Wherever you then choose to submit it |

Three properties of that table matter more than its contents:

1. **All outbound HTTP goes through one internal client** (`REQ-NET-002`). One
   place enforces the global switch, TLS, timeouts, proxy and redaction. FFmpeg's
   own network layer is compiled out (`--disable-network`, §4.4) specifically so
   that no second path can exist.
2. **The `User-Agent` is fixed and honest** (`REQ-NET-004`):
   `EclipsePlayer/<version> (+https://eclipse-player.org)`. No OS build, no
   hardware details, no unique id — nothing usable for fingerprinting.
3. **TLS is mandatory and certificate validation cannot be disabled**
   (`REQ-NET-003`). Plain `http://` is permitted only for a radio stream you typed
   yourself, and the UI marks such a stream as unencrypted.

## What never leaves, under any setting

- **Your audio files.** Syncing media files is an explicit non-goal (§2.4) — that
  is a file-sync tool's job.
- **Your library index** as a whole. Sync exchanges listening data and playlists,
  never the index itself (`REQ-SYN-003`).
- **Your filesystem paths.** Not in a scrobble, not in a metadata query, not in a
  sync change set. Settings export prefers relative forms and excludes absolute
  paths where a relative one suffices (`REQ-SET-009`).
- **Machine identifiers.** No advertising id, no MAC address, no hardware
  fingerprint, no installation UUID sent anywhere. The sync device UUID exists but
  is exchanged only with a device **you** paired, on your LAN.
- **Credentials, tokens, or the sync device key.** Excluded from settings export
  (`REQ-SET-009`), never imported (`REQ-SET-008`), never logged (`REQ-SET-013`).
- **Anything at all, when the global network switch is off.** That is the
  strongest statement here and the one with a test behind it (below).

## Settings export: readable on purpose

The export (`REQ-SET-007`) is a single **human-readable JSON** file validated
against
[`shared-spec/schemas/settings.schema.json`](../shared-spec/schemas/settings.schema.json).
Readability is a privacy feature: you can open the file and see exactly what you
are about to hand to someone. It deliberately excludes credentials, tokens,
machine identifiers and the sync key, and import never restores secrets — those
are re-authenticated on the new device (`REQ-SET-008`, `REQ-NET-043`).

## Deleting everything

**Delete all data** (`REQ-SET-014`) removes the database, settings, caches, logs,
skins, plugins **and secret-store entries**. Before doing so it names exactly what
will be destroyed and offers to export first. It is a real delete, not a reset to
defaults with the data left behind.

Uninstalling does not delete your data on any platform, by design — a reinstall
finds your library intact. Use **Delete all data** first if that is not what you
want.

## How to verify these claims

None of this requires trusting the text. In rough order of effort:

1. **Watch the network.** Block Eclipse Player in your firewall, or run it under
   observation:

   ```bash
   # Linux: every socket the process opens, live.
   sudo strace -f -e trace=network -p "$(pgrep -x eclipse-player)"

   # Or watch for packets attributable to it.
   sudo ss -tp | grep eclipse

   # Windows, PowerShell:
   Get-NetTCPConnection | Where-Object { $_.OwningProcess -eq (Get-Process eclipse-player).Id }
   ```

   With the global switch off, a full session — scan a library, play, seek, change
   themes, quit — must produce **zero** connections. This is not only a manual
   check: `REQ-TST-023` makes it an automated test in CI (§23.9), so a regression
   fails the build rather than waiting for someone to notice.

2. **Check the dependency graph yourself.** Run
   `python3 tools/check-dependency-denylist.py`, which scans every dependency
   this repository declares against an analytics/attribution/ads/crash-SDK
   denylist (`REQ-SET-010`, §25.6). Nothing runs it for you yet — see *Current
   status* below. The committed
   [`docs/sbom/eclipse-player.cdx.json`](sbom/eclipse-player.cdx.json) lists
   every component with its SPDX licence (`REQ-SEC-014`); §25.5 step 6 will
   attach one to each release once there is a release. Look for an analytics SDK
   in either; there is not one to find.

3. **Read the source.** It is MPL-2.0. The interesting places are small and
   findable: `desktop/src/net/` is the only code that opens a socket, and every
   request in it passes through the one `IHttpClient` implementation
   (`REQ-NET-002`).

4. **Read your own data.** The library index is a SQLite file. Open it with any
   SQLite browser. The settings export is JSON you can read in a text editor.
   There is no opaque blob anywhere in the design.

If any claim in this document turns out to be false, that is a security issue —
see [`SECURITY.md`](../SECURITY.md). Unexpected outbound traffic is explicitly in
scope even if it leaks nothing.

## Current status

The privacy properties above are specified and, for the parts that exist, built
that way. Being accurate about what "exists" means:

- **There is no network code yet at all.** No `IHttpClient`, no radio, no
  scrobbling, no update check. The zero-connection claim is currently true in the
  strongest possible sense — and also not yet a meaningful test of anything, since
  the code that would connect has not been written.
- **`REQ-TST-023`, the automated zero-connection test, is not yet implemented.**
  It lands with the network layer, and until then the claim rests on the absence
  of network code rather than on a test.
- **The dependency denylist gate is not yet wired into CI.** It is specified in
  §25.6 and belongs to `security.yml`; tracked as `OQ-015` in
  [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md).
- **The local FFmpeg is built `--disable-network` with `--enable-protocol='file,pipe'`.**
  Supporting evidence rather than proof: the decode path has no network transport
  available to it even in principle. Release builds must be configured the same
  way.

These are recorded because a privacy document that describes an intended state as
though it were verified is exactly the kind of document this project should not
ship.

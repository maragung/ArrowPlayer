# Eclipse Sync Protocol v1

**Authors:** Arrow Player contributors
**Version:** 1.0.0
**Status:** Normative
**Supersedes:** None

This document is the contract required by `REQ-SYN-012`: it specifies the wire
format, every message type, version negotiation, size limits, timeouts, and the
complete state machine, in enough detail for a third party to write a compatible
implementation without reading Arrow Player's source. `REQ-SYN-014`'s threat model
is §9.

Specification context: `eclipse-player.md` §18. Database side: `eclipse-player.md`
§9.4 (`change_log`). Wire-format decision: [ADR 0008](../docs/adr/0008-sync-wire-format.md).

---

## 0 · Overview

Arrow Player Sync connects two devices on the same LAN. There are no accounts,
no cloud, and no server (`REQ-SYN-002`). Two instances discover each other via
mDNS, pair once with a 6-digit code (REQ-SYN-006), and exchange change sets
directly over a TLS 1.3 mutual-auth pipe.

Sync is **disabled by default** and the application is fully functional with it
permanently off (`REQ-SYN-001`). Nothing in this document describes behaviour that
occurs before a user enables sync and completes pairing.

---

## 0.1 Scoped Entities

**Syncs** (`REQ-SYN-003`): playlists (manual and smart rules), play counts,
skip counts, ratings, loved flags, bookmarks, resume positions, last-played
timestamps, and the shortcut map.

**Never syncs**: audio files, artwork cache, the library index itself, settings
unrelated to the above, and secrets of any kind. Media-file sync is a declared
`[NON-GOAL]` — that is a file-sync tool's job, and doing it badly here would be
worse than not doing it.

Sync is an optional module, **disabled by default**, and the application is fully
functional with it permanently off (`REQ-SYN-001`). Nothing in this document
describes behaviour that occurs before a user enables sync and completes pairing.

---

## 1 · Scope

**Syncs** (`REQ-SYN-003`): playlists (manual and smart rules), play counts, skip
counts, ratings, loved flags, bookmarks, resume positions, last-played
timestamps, and the shortcut map.

**Never syncs**: audio files, artwork cache, the library index itself, settings
unrelated to the above, and secrets of any kind. Media-file sync is a declared
`[NON-GOAL]` — that is a file-sync tool's job, and doing it badly here would be
worse than not doing it.

There are no accounts, no cloud, and no server in the default topology
(`REQ-SYN-002`). Two instances on a LAN discover each other, pair once, and
exchange change sets directly.

---

## 2 · Wire format

**Length-prefixed JSON.** Each message on the wire is:

```text
+--------+--------+--------+--------+----------------------------+
|          uint32  length  (big-endian)                          |
+--------+--------+--------+--------+----------------------------+
|          UTF-8  JSON  object,  exactly  `length`  bytes        |
+----------------------------------------------------------------+
```

- The prefix is **4 bytes, big-endian, unsigned**, counting only the JSON body.
- The body is a single JSON **object** (not an array, not a bare value), encoded
  UTF-8, with no byte-order mark and no trailing newline.
- `length` MUST be ≥ 2 and ≤ **8 388 608** (8 MiB). A peer receiving a prefix
  outside that range MUST send `Error` with code `message_too_large` (or
  `malformed` for a prefix below 2) and close. It MUST NOT allocate the declared
  size before validating it — a 4-byte prefix is a 4-gigabyte allocation request
  if you trust it.
- Messages are read strictly in order on one TLS 1.3 stream. There is no
  multiplexing, no interleaving, and no message id: a request is answered by the
  next message of the expected type.

JSON rather than CBOR is a deliberate, recorded choice (ADR 0008). Two reasons
carry it: the `change_log.value_json` column is already JSON, so change payloads
cross the wire without transcoding and without a lossy round-trip; and Eclipse
already contains one hardened, fuzzed JSON parser (§21.2), whereas CBOR would add
a *second* parser for untrusted network input — a new attack surface bought for
bandwidth that a LAN does not lack. Change sets are small; correctness is not.

### 2.1 Parser requirements

The body is untrusted input and MUST be parsed under the §21.2 limits:
nesting depth ≤ 64, no duplicate object keys (reject, do not last-wins),
`\uXXXX` surrogate pairs validated, and numbers rejected if they cannot be
represented exactly as an IEEE-754 double or as an int64 where an integer is
expected. Unknown members MUST be ignored, not rejected — that is what makes
minor-version extension possible (§4).

### 2.2 Common envelope

Every message carries exactly these two members in addition to its own:

| Member | Type | Meaning |
|---|---|---|
| `type` | string | One of the six names in §3. Unknown ⇒ `Error` / `unsupported_type`. |
| `v` | integer | Protocol major version in force. MUST equal the version agreed in §4. |

---

## 3 · Message types

Eight message types: six session messages, one pairing exchange (two messages), and
the error wrapper. `REQ-SYN-012` enumerates the six session messages; the pairing
exchange adds `PairRequest` and `PairAccept`. Message type names in JSON are
case-sensitive strings.

### 3.1 `Hello`

Sent by both peers immediately after the TLS handshake completes. The initiator
sends first; the responder replies with its own `Hello`.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `deviceUuid` | string | yes | RFC 4122 UUID, stable for the life of the installation. |
| `deviceName` | string | yes | ≤ 64 chars, user-supplied, display only. Never trusted for identity. |
| `protocolVersions` | array of integer | yes | Major versions supported, ascending, 1–8 entries. |
| `appVersion` | string | yes | `MAJOR.MINOR.PATCH`. Informational; MUST NOT gate behaviour. |
| `platform` | string | yes | `windows` \| `linux` \| `android`. Informational. |

`deviceUuid` MUST match the UUID bound to the pinned key from pairing. A
mismatch is `Error` / `identity_mismatch`, then close — a peer presenting
someone else's key with its own UUID is either misconfigured or attacking.

### 3.2 `Capabilities`

Sent by both peers after `Hello` succeeds, before any change data.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `entities` | array of string | yes | Which entity kinds this peer will send and accept. v1 set: `track_state`, `playlist`, `playlist_member`, `bookmark`, `shortcut`. |
| `lamport` | integer | yes | This peer's current Lamport clock, ≥ 0. |
| `maxMessageBytes` | integer | yes | ≤ 8 388 608. The effective limit is the **minimum** of the two peers' values. |
| `maxChangesPerMessage` | integer | yes | 1–10 000. Effective limit is the minimum of the two. |
| `tombstoneHorizonDays` | integer | yes | ≥ 90 (`REQ-SYN-010`). See §6.3. |

The effective entity set is the **intersection**. An entity kind absent from the
intersection is not synced in either direction for this session; it is not an
error, and it is not silently dropped from the local change log either — the
entries simply remain unsent and will be sent to a peer that supports them.

### 3.3 `ChangesSince`

A request. Either peer may send it; in practice both do, so the exchange is
symmetric and each side pulls what it lacks.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `perDevice` | object | yes | Map of `deviceUuid` → highest `lamport` already held from that device. 0–512 entries. |
| `entities` | array of string | no | Restrict to these kinds. Default: the whole effective set. |
| `limit` | integer | no | Max changes wanted in the reply, 1 – `maxChangesPerMessage`. |

`perDevice` is a **per-origin-device watermark**, not one global cursor. A single
cursor cannot express "I have everything from device A up to 900 and everything
from device B up to 12", which is exactly the state a three-device household is
in. Omitting a device from the map means "I have nothing from it".

### 3.4 `Changes`

The reply to `ChangesSince`. Carries a batch of `change_log` rows (§9.4),
serialised one-to-one with the table so nothing is invented at the boundary.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `changes` | array of Change | yes | 0 – effective `maxChangesPerMessage`. |
| `more` | boolean | yes | `true` if the sender holds further changes matching the request. |
| `highWater` | object | yes | Map of `deviceUuid` → highest `lamport` included in *this* batch. |

Each **Change**:

| Member | Type | Req. | Notes |
|---|---|---|---|
| `entity` | string | yes | Entity kind. MUST be in the effective set. |
| `entityUuid` | string | yes | RFC 4122 UUID of the entity. |
| `field` | string \| null | yes | `null` = whole-entity create or delete. |
| `op` | integer | yes | `0` = upsert, `1` = delete. |
| `value` | any | no | Present iff `op` = 0 and `field` ≠ null. The field's new value. |
| `lamport` | integer | yes | Origin device's logical clock at the change, ≥ 1. |
| `deviceUuid` | string | yes | Origin device — **not** the forwarding device. |
| `identity` | object | no | Cross-device track identity (§5). Required when `entity` = `track_state`. |

`createdAt` from the local table is deliberately **not** on the wire. It is a
wall-clock value, and `REQ-SYN-008` forbids wall-clock ordering: shipping it
would invite an implementation to sort by it, and clock skew would then corrupt
merges silently. Ordering is by `(lamport, deviceUuid)` — the device UUID
breaking ties gives a total order that every peer computes identically.

A receiver MUST apply changes in that order and MUST treat application as
idempotent: a `(deviceUuid, lamport, entity, entityUuid, field)` tuple already
applied is discarded without effect (`REQ-SYN-011`).

### 3.5 `Ack`

Confirms durable application. The sender of `Changes` MUST NOT advance any
state on the strength of having sent them; only an `Ack` proves the peer stored
them.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `applied` | integer | yes | Count applied, ≥ 0. |
| `skipped` | integer | yes | Count discarded as already-applied. |
| `pending` | integer | yes | Count retained as pending, unmatched track identity (§5). |
| `highWater` | object | yes | Map of `deviceUuid` → highest `lamport` now durably held. |
| `conflicts` | array of Conflict | no | 0–256 entries. Resolutions worth surfacing to the user. |

Each **Conflict**: `{ entity, entityUuid, field, resolution, winner }` where
`resolution` is one of `sum`, `lww`, `max`, `union`, `tombstone`, `manual` and
`winner` is the `deviceUuid` whose value stood (absent for `sum` and `union`,
which have no loser). A `manual` resolution means the peer could not decide and
has queued it for the user (`REQ-SYN-004` forbids guessing at ambiguity).

`Ack` MUST be sent only after the changes are committed to durable storage. An
`Ack` sent before `fsync` turns a crash into silent data loss, because the
sender will never offer those changes again.

### 3.6 `Error`

Terminal unless stated otherwise. After sending `Error` a peer MUST close the
connection, except for `busy`, where it MAY remain open.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `code` | string | yes | From the table below. Closed set. |
| `message` | string | yes | ≤ 256 chars, English, for logs. Never shown as-is to a user. |
| `retryAfterSeconds` | integer | no | Only with `busy` or `rate_limited`. |

### 3.7 `PairRequest`

Carries the device identity and the PAKE verifier the pairing module generated
from the 6-digit code (`REQ-SYN-006`). Sent by the initiating peer over the TLS
pipe. The receiving peer's pairing module validates the verifier; if valid, it
responds with `PairAccept`. If invalid, it responds with `Error` /
`pairing_failed`.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `deviceUuid` | string | yes | This device's RFC 4122 UUID. |
| `deviceName` | string | yes | ≤ 64 chars, user-supplied, display only. |
| `verifier` | string | yes | Lower-case hex of the PAKE verifier `v`. |
| `codeExpiresAt` | integer | yes | Unix epoch seconds when the code expires. |

### 3.8 `PairAccept`

Acknowledgement that the pairing verifier was accepted. Confirms the shared
secret has been derived and stored in the OS secret store (`REQ-NET-043`).

| Member | Type | Req. | Notes |
|---|---|---|---|
| `deviceUuid` | string | yes | This device's UUID — mirrors the `PairRequest`. |
| `deviceName` | string | yes | Display name of the accepting device. |
| `verifier` | string | yes | The accepting device's own verifier `v`. |

After both `PairRequest` and `PairAccept` are exchanged, both devices derive the
long-term key via HKDF-SHA256 from the PAKE shared secret and store it in the
OS secret store under `sync/peer/<deviceUuid>`. The TLS pipe then carries the
normal HELLO handshake; the pre-shared key bound to the device identity in the
secret store authenticates the connection (`REQ-SYN-007`).

### 3.9 `SyncRequest`

A request for a set of changes from a specific Lamport watermark onwards. Either
peer may send it; in practice both do so the exchange is symmetric.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `perDevice` | object | yes | Map of `deviceUuid` → highest `lamport` already held from that device. 0–512 entries. |
| `entities` | array of string | no | Restrict to these entity kinds. Default: the whole effective set from `Capabilities`. |
| `limit` | integer | no | Max changes wanted, 1 – `maxChangesPerMessage`. |

### 3.10 `SyncResponse`

The reply to `SyncRequest`. Carries a batch of `change_log` rows.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `changes` | array of Change | yes | 0 – `maxChangesPerMessage`. |
| `more` | boolean | yes | `true` if the sender holds further changes matching the request. |
| `highWater` | object | yes | Map of `deviceUuid` → highest `lamport` included in *this* batch. |

Each **Change** is defined in §3.4. Ordering and idempotency requirements are
identical to §3.4.

### 3.11 `ChangeBatch`

An unsolicited batch of changes, sent by either peer without a corresponding
request. Used when a peer generates a local change while the session is open —
the peer pushes the batch rather than waiting for the other side to poll.
Reception triggers an `Ack` in the same way as `SyncResponse`.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `changes` | array of Change | yes | The new changes. |
| `highWater` | object | yes | Per-device high-water mark for this batch. |

### 3.12 `Goodbye`

Sent by either peer before closing the TLS connection cleanly. The sender MUST
not send any further messages; the receiver MUST close the connection after
processing any in-flight `Ack`.

| Member | Type | Req. | Notes |
|---|---|---|---|
| `reason` | string | no | Machine-readable reason for closing. Optional; for logs only. |

| Code | Meaning | Recovery |
|---|---|---|
| `version_unsupported` | No common major version (§4) | None — versions are static |
| `unsupported_type` | Unknown `type` member | None |
| `malformed` | Prefix, JSON, or schema invalid | None |
| `message_too_large` | Prefix or body exceeds the effective limit | Sender retries with smaller batches |
| `identity_mismatch` | `deviceUuid` inconsistent with the pinned key | Re-pair |
| `not_paired` | No pinned key for this peer | Pair |
| `revoked` | Pairing was revoked | Re-pair |
| `unexpected_state` | Message not valid in the current state (§7) | Reconnect |
| `busy` | Another sync in progress | Retry after `retryAfterSeconds` |
| `rate_limited` | Flood control tripped (§9.3) | Retry after `retryAfterSeconds` |
| `internal` | Local failure, no detail disclosed | Retry once |

`message` is never surfaced verbatim: it is peer-controlled text, and rendering
it in the UI would let a hostile peer write the user's error dialog. The UI
shows a local string chosen by `code`.

---

## 4 · Version negotiation

1. Both peers send `Hello` with `protocolVersions`.
2. The agreed version is the **highest integer present in both lists**.
3. If the intersection is empty, both send `Error` / `version_unsupported` and
   close. Neither peer downgrades to anything not in its own list.
4. Every subsequent message carries `v` = the agreed version. A message whose
   `v` differs is `Error` / `unexpected_state`.

Within a major version, additive change is permitted and MUST be tolerated:
unknown object members are ignored (§2.1), and a new optional member never
changes the meaning of the members already defined. Anything that removes a
member, narrows a type, or changes a resolution rule is a **new major version**.

There is no minor-version number on the wire, deliberately. A capability that a
peer needs to know about belongs in `Capabilities` where it can be negotiated,
not in a version number that has to be interpreted.

---

## 5 · Track identity

`track_state` changes carry an `identity` object rather than a local row id,
because a row id means nothing on the other device. Resolution order is fixed by
`REQ-SYN-004` and MUST be tried in exactly this order:

1. `musicbrainzRecordingId` — MusicBrainz recording MBID
2. `chromaprint` — fingerprint, with `durationMs`
3. normalised `(artist, album, title, durationMs ± 2000 ms)`
4. `relativePath` — path relative to its library source, never absolute

| Member | Type | Notes |
|---|---|---|
| `musicbrainzRecordingId` | string | UUID form, optional |
| `chromaprint` | string | optional, ≤ 4096 chars |
| `artist`, `album`, `title` | string | optional, ≤ 512 chars each |
| `durationMs` | integer | optional, ≥ 0 |
| `relativePath` | string | optional, ≤ 4096 chars |

At least one member MUST be present. An entry matching nothing is **retained as
pending** — counted in `Ack.pending`, never dropped, and never merged into a
different track. A rung that matches **more than one** local track is ambiguous:
the receiver MUST NOT pick one. It records the ambiguity for user resolution and
counts the entry as pending.

Absolute paths MUST NOT appear. `REQ-SET-009` forbids exporting an absolute path
where a relative form suffices, and the same reasoning applies with more force
here: `/home/ana/Music/...` on the wire discloses a username to every peer for
no benefit.

---

## 6 · Merge semantics

The receiver applies changes with the per-field rules of `REQ-SYN-009`. They are
restated here because an implementation that gets them wrong is not compatible,
however well it parses.

### 6.1 Per-field rules

| Field | Rule | Why |
|---|---|---|
| `play_count`, `skip_count` | **Sum of per-device deltas** | Last-writer-wins would discard plays made on the other device |
| `rating`, `is_loved` | LWW by `(lamport, deviceUuid)` | Genuinely a single-value user opinion |
| `last_played_at` | Maximum | Monotonic by nature |
| `resume_position_ms` | From the device with the greatest `last_played_at` | The most recent listener is authoritative |
| Playlist membership | **Ordered-set union** with tombstones; a delete wins only if its Lamport clock is later than every competing insert | Prevents both resurrection and accidental mass deletion |
| Playlist rename | LWW | Single value |
| Smart-playlist rule | LWW, **losing version retained in history** | Rules are hand-authored and expensive to lose |
| Bookmarks | Union by UUID, tombstones for deletes | Additive by nature |

Because counters sum deltas, a peer MUST store per-origin-device counter state,
not just a total. A single total cannot be reconciled: two devices each holding
"47" cannot tell whether the answer is 47 or 94.

### 6.2 Ordering

Total order is `(lamport ASC, deviceUuid ASC)`. Wall-clock time is never used for
ordering (`REQ-SYN-008`). On receiving a change with `lamport = L`, a peer sets
its own clock to `max(local, L) + 1` before recording any local change.

### 6.3 Tombstones

Deletes are tombstones (`op` = 1), retained **at least 90 days**
(`REQ-SYN-010`). Compaction MUST NOT run below that window: dropping a tombstone
early lets a device that was offline resurrect a deleted playlist, which the user
experiences as the app undoing their work.

`Capabilities.tombstoneHorizonDays` publishes each peer's window. A peer whose
watermark for some device is older than the *other* peer's horizon cannot be
merged safely by watermark alone; the responder MUST answer such a
`ChangesSince` with a full-state exchange for the affected entities rather than a
delta, since it can no longer prove which deletes the requester has missed.

### 6.4 Idempotence and resumption

Applying the same change set twice MUST produce the same state, and an
interrupted sync MUST resume without duplication (`REQ-SYN-011`). Both properties
are proven by a property-based test that applies random change-log permutations
and asserts convergence (§23.8) — a claim of idempotence without that test is a
claim, not a property.

---

## 7 · State machine

One connection, one sync session. States, with the message that leaves each:

```text
                          ┌─────────────────┐
                          │      IDLE       │  sync disabled or no paired peers
                          └────────┬────────┘
                    enable + peer found │
                                   │
                                   ▼
                          ┌─────────────────┐
                          │   PAIRING_WAIT   │  displaying 6-digit code
                          └────────┬────────┘
                                   │ peer scans code
                                   ▼
                          ┌─────────────────┐   pairing fails
                          │  PAIR_REQUEST    │─────────────────────► IDLE
                          └────────┬────────┘
                                   │ PairRequest ──► PairAccept
                                   ▼
                          ┌─────────────────┐   no pinned key / revoked
                          │   CONNECTING     │─────────────────────► IDLE
                          └────────┬────────┘
                 TLS 1.3 mutual auth (pre-shared key from pairing)
                                   ▼
                          ┌─────────────────┐   identity mismatch
                          │   HANDSHAKING    │─────────────────────► IDLE
                          │   Hello ⇄ Hello │   version unsupported
                          └────────┬────────┘
                                   │
                          ┌────────▼────────┐
                          │  CAPABILITIES   │  Capabilities ⇄ Capabilities
                          └────────┬────────┘
                                   │
                          ┌────────▼────────┐
                          │    SYNCING     │──────────────────────► CLOSING
                          │  ChangesSince  │
                          │  ──► Changes  │  both directions, pipelined
                          │  ──► Ack       │  more=true loops
                          └────────┬────────┘
                                   │ more=false both sides
                                   ▼
                          ┌─────────────────┐
                          │     CLOSING     │  Goodbye, TLS close_notify
                          └────────┬────────┘
                                   │
                                   ▼
                                 IDLE
```

**Transitions in words:**

| From | Event | To | Notes |
|---|---|---|---|
| IDLE | sync enabled + paired peer discovered | PAIRING_WAIT | |
| PAIRING_WAIT | user scans 6-digit code + network handshake succeeds | PAIR_REQUEST | PAKE exchange runs in background |
| PAIR_REQUEST | PairRequest + PairAccept exchanged, keys stored | CONNECTING | |
| PAIRING_WAIT / PAIR_REQUEST | user cancels or pairing times out | IDLE | |
| CONNECTING | TLS 1.3 mutual auth succeeds | HANDSHAKING | Unpaired peers are refused before any library data (`REQ-SYN-007`) |
| CONNECTING | auth fails / no pinned key / revoked | IDLE | `Error` `not_paired` or `revoked`; revocation is immediate |
| HANDSHAKING | both `Hello` valid, version agreed | CAPABILITIES | |
| HANDSHAKING | no common version or UUID mismatch | IDLE | `Error`, then close |
| CAPABILITIES | both `Capabilities` valid | SYNCING | Effective limits = pairwise minimum; entity set = intersection |
| SYNCING | `Changes` / `SyncResponse` received | SYNCING | Apply durably, then `Ack` |
| SYNCING | `Ack` with `more` outstanding | SYNCING | Pull next batch from acked watermark |
| SYNCING | unsolicited `ChangeBatch` received | SYNCING | Apply, reply with `Ack` |
| SYNCING | both directions report `more` = false | CLOSING | |
| any | protocol violation | IDLE | `Error`, then close |
| any | timeout (§8) | IDLE | Close without `Error`; the peer is not answering |

A message arriving in a state that does not expect it is `Error` /
`unexpected_state` followed by close. In particular, `SyncRequest` before
`Capabilities` MUST be refused — accepting library data before limits are agreed
is how a peer gets to send an 8 MiB batch to a device that said it could take
64 KiB.

---

## 8 · Timeouts and limits

| Parameter | Value | Rationale |
|---|---|---|
| TCP connect | 10 s | Matches `REQ-NET-012`'s connect timeout; a dead peer must not hang the UI |
| TLS handshake | 10 s | |
| `Hello` after TLS | 5 s | Nothing to compute; slowness here is a stalled or hostile peer |
| `Capabilities` after `Hello` | 5 s | |
| Any read while SYNCING | 30 s | Allows for a large batch being assembled from disk |
| `Ack` after `Changes` | 60 s | The peer may be doing durable writes and identity resolution |
| Whole session | 15 min | Bounds a pathological sync; the next session resumes from the watermark |
| Max message | 8 MiB | Bounded allocation; matches the §17.2 feed cap for one hardening story |
| Max changes per message | 10 000 | |
| Max `perDevice` entries | 512 | |
| Reconnect backoff | 1, 2, 5, 10, 30 s then every 60 s | Same ladder as `REQ-NET-013`, so one policy governs all reconnection |

Every timeout is enforced by the receiver, not merely documented. A read with no
deadline is a denial-of-service primitive: one peer holding a connection open
forever costs the other a thread and a socket for as long as it likes.

---

## 9 · Threat model (`REQ-SYN-014`)

Every scenario `REQ-SYN-014` names, each with a mitigation or an explicit
accepted risk. Sync is off by default, so none of this is reachable for a user
who never enables it.

### 9.1 A malicious peer on the LAN

**Threat.** Anything on the network can answer mDNS, advertise
`_eclipsesync._tcp`, and attempt a connection — a coffee-shop Wi-Fi, a guest
device, a compromised IoT appliance.

**Mitigation.** Discovery advertises only device name, device UUID, protocol
version, and a public-key fingerprint (`REQ-SYN-005`) — no library data, no
counts, nothing about the music. Pairing requires a 6-digit code shown on device
A and typed on device B, bound to a PAKE exchange (SPAKE2 or equivalent), so
observing the network yields nothing usable: the code is not a password sent over
the wire (`REQ-SYN-006`). The code expires after **120 s** and attempts are rate
limited to **5 tries, then a 5-minute lockout**. Post-pairing transport is TLS
1.3 with **mutual** authentication against the keys pinned at pairing
(`REQ-SYN-007`); an unpaired peer is refused before any library data is exchanged.

**Accepted risk.** An unpaired peer learns that an Eclipse instance exists on the
network and its user-chosen device name. Suppressing that would mean giving up
zero-configuration discovery entirely. Users who object can disable discovery
independently of sync (`REQ-SYN-005`) and pair by direct address.

### 9.2 A peer that replays old change sets

**Threat.** A paired-then-hostile device, or an attacker with a captured
transcript, resends old changes to roll back a rating, resurrect a deleted
playlist, or restore a stale resume position.

**Mitigation.** TLS 1.3 defeats replay of the *transport*: a captured session
cannot be re-injected into a new one. At the application layer, ordering is by
Lamport clock, and application is idempotent — a
`(deviceUuid, lamport, entity, entityUuid, field)` tuple at or below the recorded
watermark is discarded with no effect. LWW fields cannot be rolled back, because
an older `lamport` loses to the
value already held. Counters are per-device deltas, so a resent delta is
recognised by its Lamport position rather than added again. Playlist deletes win
only if their clock is later than every competing insert, so a replayed delete
cannot remove content added afterwards.

**Accepted risk.** A *paired* device may legitimately send changes that the user
regards as wrong (someone else's rating on a shared device). That is an
authorisation question, answered by revocation, not by replay defence.

### 9.3 A peer that floods the change log

**Threat.** A paired device streams millions of changes to exhaust disk, CPU, or
the tombstone window — accidentally (a buggy build in a loop) or deliberately.

**Mitigation.** Per-message caps (8 MiB, 10 000 changes) and the 15-minute
session cap bound any single session. Beyond that, a token bucket per paired
device limits sustained intake — default **20 000 changes per hour**, with
`Error` / `rate_limited` and `retryAfterSeconds` once tripped, which is a request
to slow down rather than a disconnection. Changes are applied in bounded
transactions so a flood cannot hold one open indefinitely. A device that trips
the limit repeatedly is surfaced in the sync UI with a one-click revoke, because
the honest fix for a misbehaving device is to stop trusting it.

**Accepted risk.** A determined paired peer can still consume storage up to the
rate limit over time. Sync writes are bounded by the entity kinds in §1 — no
audio, no artwork — so the ceiling is text, and the user can see and revoke the
source.

### 9.4 A stolen paired device

**Threat.** A phone or laptop already holding a long-term shared key is lost or
stolen. The thief can sync, and can read whatever the OS lets them read.

**Mitigation.** Long-term keys live in the OS secret store — Windows Credential
Manager, Secret Service/`libsecret` on Linux, Android Keystore-backed
`EncryptedSharedPreferences` — never in a plain settings file (`REQ-NET-043`,
`REQ-SET-009`). Devices are **individually revocable and revocation takes effect
immediately** (`REQ-SYN-007`): the surviving device removes the pinned key, and
the stolen device's next connection fails at mutual auth with `revoked`. The
settings export deliberately omits the sync device key, so restoring a backup
onto a new machine cannot clone a paired identity — it requires re-pairing, which
requires physical access to a trusted device.

**Accepted risk.** Data already on the stolen device is gone with the device;
that is disk encryption's problem, not sync's. Revocation cannot retroactively
un-share what was already synced. Eclipse does not attempt remote wipe — it has
no server to send the command from, and inventing one would contradict
`REQ-SYN-002`.

### 9.5 A compromised relay

**Threat.** The `[v1.x]` optional self-hosted relay (`REQ-SYN-013`) is breached,
or its operator is hostile, or DNS points at an impostor.

**Mitigation.** The relay is a **store-and-forward relay of already-encrypted
change sets**; it MUST NOT be able to read library contents. End-to-end
encryption is between the paired devices, keyed by the pairing exchange, so the
relay sees ciphertext, sizes, and timing. It never holds a key that decrypts
anything. It is never required for core functionality, and **no default
configuration points at any hosted instance** — a user who has not deployed a
relay has no relay in their threat model at all.

**Accepted risk.** A compromised relay can drop, delay, or reorder messages, and
can observe metadata: which device UUIDs talk, when, and how much. Reordering
and duplication are harmless by §6.4 (idempotent, order-independent merge);
dropping is a denial of service, visible to the user as sync not completing.
Traffic-analysis resistance — padding, cover traffic — is out of scope for v1 and
recorded here as accepted rather than solved.

---

## 10 · Conformance

An implementation is compatible if it:

1. frames every message as §2 specifies, and refuses a prefix outside 2 – 8 MiB
   **without allocating** the declared size;
2. implements all six message types of §3 with the members marked required, and
   ignores unknown members;
3. negotiates the version by §4 and never downgrades outside its own list;
4. resolves track identity in the four-rung order of §5, retaining unmatched and
   ambiguous entries as pending rather than guessing;
5. applies the per-field rules of §6.1 exactly, ordering by
   `(lamport, deviceUuid)` and never by wall clock;
6. retains tombstones for at least 90 days;
7. is idempotent and resumable under arbitrary change-log permutations;
8. enforces every timeout in §8 as a receiver, not just as a sender's courtesy;
9. refuses `Changes` before `Capabilities`, and any message out of state, with
   `Error` / `unexpected_state`.

`REQ-GEN-031` requires the desktop and Android engines to agree; the
property-based convergence test of §23.8 is the shared evidence, run against the
same permutations on both platforms.

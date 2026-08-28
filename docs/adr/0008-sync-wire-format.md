# ADR 0008 — Length-prefixed JSON for the sync protocol, not CBOR

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-SYN-012, REQ-SYN-014, REQ-SEC-002, REQ-LIB-050

## Context

`REQ-SYN-012` requires the sync wire format to be either length-prefixed JSON or
CBOR, "chosen and recorded in an ADR". This is that record; the resulting format
is specified in full in `shared-spec/sync-protocol.md` §2.

The two candidates are close on the criteria that usually decide such a choice.
Both are self-describing, schema-free, widely implemented, and adequate for the
message set (`Hello`, `Capabilities`, `ChangesSince`, `Changes`, `Ack`, `Error`).
CBOR is more compact and faster to parse. On bandwidth and CPU alone, CBOR wins.

Those are not the binding constraints here.

**The payload is already JSON.** The `change_log` table in §9.4 stores each change
as `value_json`. Sync is a merge of change-log entries, so the bulk of every
`Changes` message is content that exists as JSON text in both peers' databases. A
JSON wire format moves it verbatim. A CBOR wire format transcodes it twice per
sync, and every transcode is a place where a number's type or a string's encoding
can change subtly — through a JSON number that does not round-trip through CBOR's
integer/float distinction, for example. The user-visible consequence of a lossy
round-trip is a play count or a rating that drifts, which is precisely what
`REQ-SYN-009`'s per-field rules exist to prevent.

**A second parser is a second attack surface.** Sync peers are "authenticated but
not trusted" (§21.1) — pairing proves which device is talking, not that it is
behaving. Message bodies are untrusted input subject to `REQ-SEC-002`. Arrow
already has exactly one hardened, fuzzed JSON parser, written for exactly this
threat model. Adding CBOR means a second parser for untrusted network input, with
its own fuzz targets, its own indefinite-length and tag-handling edge cases, and
its own decade of CVEs to inherit. That cost is paid to save bandwidth on a LAN,
which is the one network where bandwidth is not scarce.

## Decision

**Length-prefixed JSON**, specified in `shared-spec/sync-protocol.md` §2:

- A **4-byte big-endian unsigned length prefix** counting only the body, followed
  by exactly that many bytes of UTF-8 JSON.
- The body is a single JSON **object** — not an array, not a bare value — with no
  BOM and no trailing newline.
- `length` MUST be ≥ 2 and ≤ 8 MiB. **A peer MUST validate the prefix before
  allocating**: a 4-byte prefix is a 4-gigabyte allocation request if believed. Out
  of range is `Error`/`message_too_large` (or `malformed` below 2) followed by
  close.
- Bodies are parsed under the §21.2 limits: depth ≤ 64, duplicate keys rejected
  rather than last-wins, surrogate pairs validated, numbers rejected when not
  exactly representable.
- **Unknown members are ignored, not rejected.** This is what makes minor-version
  extension possible without a protocol break, and it is the wire-format
  counterpart of `REQ-THM-052` for schemas.
- One TLS 1.3 stream, messages strictly in order, no multiplexing and no message
  ids: a request is answered by the next message of the expected type.

## Consequences

**Positive.** Change payloads cross the wire without transcoding, so no round-trip
can alter a value. One parser, already hardened and fuzzed, handles all untrusted
structured input in the process.

**Positive.** The protocol is debuggable with ordinary tools. `REQ-SYN-012`
requires the specification to be detailed enough for a third party to write a
compatible implementation; a format someone can read in a hex dump, or reproduce
with a shell script and `jq`, lowers that bar considerably. For an optional,
self-hosted, no-accounts protocol, third-party implementability is a feature, not
a courtesy.

**Positive.** Interoperability with the `[v1.x]` relay server (`REQ-SYN-013`) is
trivial in any language, which matters because that component is explicitly meant
to be a small, replaceable, self-hosted thing rather than a product.

**Negative.** Messages are larger — plausibly 30–50 % over CBOR for
change-log-shaped data. Accepted: sync happens over a LAN, message size is capped
at 8 MiB, and change sets are small by construction. If a user's first sync of a
large library proves slow in practice, the fix is pagination via `ChangesSince`
(already in the protocol), not a wire-format change.

**Negative.** JSON parsing is slower and allocates more than CBOR. Irrelevant
here: sync runs on a worker thread, never on the RT path (§8.2.3), and is not in
any latency budget.

**Negative.** JSON has no native binary type, so any future binary payload needs
base64 — a 33 % expansion. Currently nothing in `REQ-SYN-003` is binary: audio
files, artwork and the library index explicitly do not sync. If that changes, it
is a protocol major-version decision, made deliberately, rather than something
this format silently permits.

**Neutral, worth stating.** The choice is *not* justified by "JSON is easier". It
is justified by having one parser for untrusted input and by not transcoding data
that is already JSON. If the change log were stored in a binary format, this ADR
would likely have gone the other way.

## Alternatives considered

**CBOR** (RFC 8949) — rejected for the two reasons above: a second untrusted-input
parser, and a transcode on every change payload. Its compactness advantage applies
to the one resource a LAN sync does not lack.

**Protocol Buffers / FlatBuffers** — rejected. Both require a schema compiler and a
build step on both platforms, and both make protocol evolution a code-generation
concern. Worse, a fixed schema fits the change log badly: `value_json` is
deliberately schema-free so that new syncable fields do not require a protocol
change, and "ignore unknown members" is the extension mechanism.

**MessagePack** — rejected: CBOR's tradeoffs with a less rigorous specification
and no standards-body pedigree. If a binary format were chosen, CBOR would be it.

**HTTP/2 or gRPC as the transport framing** — rejected as disproportionate. Sync is
a single stream between two paired peers on a LAN, with mutual TLS 1.3 against
keys pinned at pairing (`REQ-SYN-007`). Framing, flow control and multiplexing
solve problems this protocol does not have, and gRPC would drag in a large
dependency and a code generator for six message types.

**Newline-delimited JSON** — rejected in favour of a length prefix. NDJSON forces
the receiver to scan for a delimiter before it knows how much to read, which means
either an unbounded read or a delimiter-aware size check; a length prefix lets a
peer reject an over-large message from its first four bytes, before allocating
anything. That property is what makes the flood-resistance mitigation in
`REQ-SYN-014` implementable.

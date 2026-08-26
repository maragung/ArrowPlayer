#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regenerates the committed fuzz seed corpus (REQ-SEC-011).

The seeds are committed, not generated at build time — REQ-SEC-011 says
*committed*, and a corpus the build invents is a corpus that changes under you.
This script exists so the ones that are not human-readable can be reviewed:
nobody can check an MP3 frame's LAME CRC by squinting at a hexdump, but anyone
can read the code that computes it.

Run it after adding a seed here; run it never otherwise. It writes only the files
it knows about and leaves everything else in `corpus/` alone, so inputs added by
libFuzzer or by REQ-SEC-012's regression rule are never overwritten.

    python3 desktop/tests/fuzz/make-seeds.py [--check]

`--check` regenerates into memory and compares, so CI can prove the committed
bytes still match the code that documents them.

Standard library only: no pip, no venv.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORPUS = HERE / "corpus"


# ---------------------------------------------------------------------------
#  fuzz_json — shapes that reach a different branch of the parser
# ---------------------------------------------------------------------------
def json_seeds() -> dict[str, bytes]:
    deep = "[" * 60 + "1" + "]" * 60
    return {
        # Every value kind in one document, so a mutation lands on a real node.
        "kitchen-sink.json": (
            b'{"str":"hello","num":-12.5e3,"int":9007199254740993,'
            b'"t":true,"f":false,"n":null,"arr":[1,2,3],"obj":{"a":{"b":[]}}}'
        ),
        # RFC 6901 escaping: the two characters a JSON Pointer must escape, which
        # is the invariant the harness checks by walking every node's own path.
        "pointer-escapes.json": b'{"a/b":1,"c~d":2,"~/":3,"":4,"nested":{"x/y~z":5}}',
        # JSONC, which the theme and layout formats rely on (allow_comments).
        "comments.json": (
            b"// leading line comment\n{\n  /* block */ \"a\": 1,\n  \"b\": [2, 3,],\n}\n"
        ),
        # Escapes, including a surrogate pair and the control-character forms.
        "escapes.json": rb'{"s":"\u00e9\ud83d\ude00\\\/\b\f\n\r\t\"","k\u0041":1}',
        # Nesting well inside max_depth (64) so mutation can push it over.
        "deep-nesting.json": deep.encode(),
        # Duplicate keys are rejected, never silently overwritten — the fact the
        # pointer invariant depends on. A seed keeps that branch reachable.
        "duplicate-keys.json": b'{"a":1,"a":2}',
        # Truncated mid-string: the "did the parser stop cleanly" path.
        "truncated.json": b'{"a":"unterminated',
        # A bare scalar is a valid document.
        "bare-scalar.json": b'"just a string"',
        # UTF-8 BOM, which real theme files acquire from Windows editors.
        "bom.json": b"\xef\xbb\xbf{}",
        # Numbers at the edges of double conversion.
        "numbers.json": b"[0,-0,1e308,1e-308,1E+2,0.0000000000000001,123456789012345678901]",
    }


# ---------------------------------------------------------------------------
#  fuzz_text — malformed UTF-8 taxonomy plus the path-safety inputs
# ---------------------------------------------------------------------------
def text_seeds() -> dict[str, bytes]:
    return {
        # One of each malformed class: continuation without a lead, truncated
        # two/three/four-byte sequences, overlong forms, an encoded surrogate,
        # a codepoint above U+10FFFF, and the 5- and 6-byte forms UTF-8 never had.
        "malformed-utf8.bin": bytes(
            [0x80, 0xBF, 0xC2, 0xE0, 0xA0, 0xF0, 0x90, 0x80]
            + [0xC0, 0xAF]              # overlong '/'
            + [0xE0, 0x80, 0xAF]        # overlong '/'
            + [0xF0, 0x80, 0x80, 0xAF]  # overlong '/'
            + [0xED, 0xA0, 0x80]        # U+D800 encoded
            + [0xF4, 0x90, 0x80, 0x80]  # beyond U+10FFFF
            + [0xF8, 0x88, 0x80, 0x80, 0x80]
            + [0xFE, 0xFF]
        ),
        # Valid multi-byte text across the plane boundaries, plus combining marks
        # that strip_diacritic() and sort_key() have to handle.
        "valid-utf8.txt": "aé漢🎵Å\u0301e\u0301 \u200bAsh".encode(),
        # sort_key's article stripping, the leading-article edge cases.
        "articles.txt": b"The Beatles / A Tribe / An Album /  the  spaced  ",
        # Path safety: the traversal and absolute forms is_unsafe_relative_path
        # exists to reject, one per line so a mutation keeps most of them intact.
        "paths.txt": (
            b"../etc/passwd\n"
            b"skin/../../etc/passwd\n"
            b"/absolute/path\n"
            b"C:\\Windows\\system32\n"
            b"\\\\server\\share\n"
            b"a/./b/../c\n"
            b"nested/ok/file.png\n"
            b"trailing/..\n"
            b"..\n"
            b".\n"
        ),
        # A NUL and control characters, which the path check treats as unsafe and
        # the decoder must treat as ordinary codepoints.
        "control-bytes.bin": bytes(range(0x00, 0x21)) + b"name\x00.png",
        "empty.bin": b"",
    }


# ---------------------------------------------------------------------------
#  fuzz_xinglame — real MPEG-1 Layer III frames with Xing/Info + LAME
# ---------------------------------------------------------------------------
def crc16(data: bytes) -> int:
    """CRC-16 as LAME writes it: poly 0x8005, MSB-first, initial value 0.

    Same algorithm as `test_gapless.cpp`'s helper. Duplicated deliberately: the
    test proves the parser accepts what LAME produces, and this proves the seeds
    are what LAME produces. Sharing one implementation would let a single mistake
    make both agree on the wrong bytes.
    """
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def mp3_frame(
    *,
    mono: bool = False,
    magic: bytes = b"Xing",
    frame_count: int | None = 1000,
    byte_count: int | None = None,
    toc: bool = False,
    lame: bool = True,
    delay: int = 576,
    padding: int = 1800,
    break_crc: bool = False,
    frame_bytes: int = 417,
) -> bytes:
    """One MPEG-1 Layer III 44.1 kHz frame carrying a Xing/Info tag.

    Field layout per the Xing header specification and LAME's `VBRTag.c`; the
    offsets are the ones `parse_xing_lame` derives, which is the point — a seed
    built from the parser's own assumptions would prove nothing.
    """
    # 0xFF 0xFB: sync, MPEG-1, Layer III, no CRC protection.
    # 0x90: bitrate index 9 (128 kbps), sample-rate index 0 (44.1 kHz).
    header = bytes([0xFF, 0xFB, 0x90, 0xC0 if mono else 0x00])
    side_info = bytes(17 if mono else 32)

    flags = 0
    tail = b""
    if frame_count is not None:
        flags |= 0x0001
        tail += frame_count.to_bytes(4, "big")
    if byte_count is not None:
        flags |= 0x0002
        tail += byte_count.to_bytes(4, "big")
    if toc:
        flags |= 0x0004
        tail += bytes((i * 255) // 100 for i in range(100))

    tag = magic + flags.to_bytes(4, "big") + tail

    if lame:
        block = bytearray(36)
        block[0:9] = b"LAME3.100"
        # Bytes 21..23 pack the 12-bit encoder delay and 12-bit padding.
        block[21] = (delay >> 4) & 0xFF
        block[22] = ((delay & 0x0F) << 4) | ((padding >> 8) & 0x0F)
        block[23] = padding & 0xFF
        crc = crc16(header + side_info + tag + bytes(block[:34]))
        if break_crc:
            crc ^= 0xFFFF
        block[34] = (crc >> 8) & 0xFF
        block[35] = crc & 0xFF
        tag += bytes(block)

    frame = header + side_info + tag
    return frame.ljust(frame_bytes, b"\x00")


def xinglame_seeds() -> dict[str, bytes]:
    return {
        # The ordinary case: what LAME writes for a VBR stereo encode.
        "xing-lame-stereo.mp3": mp3_frame(),
        # Mono, whose 17-byte side information puts the tag at a different offset
        # — the arithmetic most likely to be got wrong.
        "info-lame-mono.mp3": mp3_frame(mono=True, magic=b"Info"),
        # The 100-byte seek table, which is the largest variable-length field and
        # therefore the one that pushes the LAME block near the frame's end.
        "xing-toc.mp3": mp3_frame(toc=True, byte_count=417_000),
        # A failed CRC must be treated as no tag at all (REQ-AUD-039).
        "xing-lame-bad-crc.mp3": mp3_frame(break_crc=True),
        # Both 12-bit fields at maximum against a one-frame stream: the trim
        # exceeds the audio, so the parser must demote the tag rather than return
        # a stream that plays nothing.
        "xing-trim-exceeds-stream.mp3": mp3_frame(frame_count=1, delay=0xFFF, padding=0xFFF),
        # A frame count of zero, and no LAME block at all.
        "xing-zero-frames.mp3": mp3_frame(frame_count=0, lame=False),
        # Flags set for fields that are not there: the truncation the flag word
        # invites, since every offset after it is flag-derived.
        "xing-flags-without-fields.mp3": (
            bytes([0xFF, 0xFB, 0x90, 0x00]) + bytes(32) + b"Xing" + (0x000F).to_bytes(4, "big")
        ),
        # Truncated inside the LAME block.
        "xing-truncated.mp3": mp3_frame()[:60],
        # A header with nothing after it.
        "header-only.mp3": bytes([0xFF, 0xFB, 0x90, 0x00]),
        # Reserved bitrate index (15) and reserved sample-rate index (3): both
        # must be rejected before any offset is computed from them.
        "reserved-fields.mp3": bytes([0xFF, 0xFB, 0xFC, 0x00]) + bytes(32),
        # MPEG-2.5, whose 576-sample frames and narrower side information the
        # header parser has to distinguish from MPEG-1.
        "mpeg25-header.mp3": bytes([0xFF, 0xE3, 0x50, 0x00]) + bytes(17) + b"Xing" + bytes(4),
        # Sync bytes that are not a frame — the "is this even an MP3" path.
        "not-mp3.bin": b"ID3\x04\x00\x00\x00\x00\x00\x00" + b"\xff\x00" * 8,
    }


# ---------------------------------------------------------------------------
#  fuzz_gapless — the three gapless parsers fuzz_xinglame does not reach
# ---------------------------------------------------------------------------
def opus_head(
    *,
    version: int = 1,
    channels: int = 2,
    pre_skip: int = 312,
    rate: int = 48000,
    gain_q7_8: int = 0,
    mapping: int = 0,
    table: bytes = b"",
) -> bytes:
    """An RFC 7845 §5.1 identification header. 19 bytes, plus a mapping table."""
    return (
        b"OpusHead"
        + bytes([version, channels])
        + pre_skip.to_bytes(2, "little")
        + rate.to_bytes(4, "little")
        + (gain_q7_8 & 0xFFFF).to_bytes(2, "little")
        + bytes([mapping])
        + table
    )


def itunsmpb(priming: int, padding: int, total: int) -> bytes:
    """The 12-field space-padded form Apple writes, of which we read three."""
    fields = [
        "00000000",
        f"{priming:08X}",
        f"{padding:08X}",
        f"{total:016X}",
    ] + ["00000000"] * 8
    return (" " + " ".join(fields) + " ").encode()


def gapless_seeds() -> dict[str, bytes]:
    # The harness derives the granule pair from bytes 0-15 big-endian, so a seed
    # aimed at one parser is a plausible input to the others. That overlap is the
    # point: it is how one buffer exercises three code paths.
    return {
        # --- iTunSMPB (REQ-AUD-040, REQ-AUD-042) ---
        "itunsmpb-typical.txt": itunsmpb(0x840, 0x1C0, 0x00A3B140),
        # priming + padding == total exactly: the boundary the range check allows.
        "itunsmpb-trim-equals-total.txt": itunsmpb(0x100, 0x100, 0x200),
        # One field short of the four we read — the arity rejection.
        "itunsmpb-three-fields.txt": b" 00000000 00000840 000001C0 ",
        # Non-hex in the field we parse: REQ-AUD-042's outright rejection.
        "itunsmpb-non-hex.txt": b" 00000000 0000084Z 000001C0 00A3B140 ",
        # Values past 32 bits, which must be refused rather than truncated.
        "itunsmpb-overflow.txt": b" 00000000 1FFFFFFFF 00000001 FFFFFFFFFFFFFFFF ",
        # total == 0, which the parser maps to "unknown" rather than a zero stream.
        "itunsmpb-zero-total.txt": itunsmpb(0x840, 0x1C0, 0),

        # --- OpusHead (REQ-AUD-043) ---
        "opus-head-typical.bin": opus_head(),
        # Mono at 44.1 kHz input rate, with a negative output gain (-6 dB in Q7.8).
        "opus-head-gain.bin": opus_head(channels=1, rate=44100, gain_q7_8=-1536),
        # Mapping family 1 promises a table sized by channel_count; here it is
        # present, so the accepted-with-table branch stays reachable.
        "opus-head-mapping-family1.bin": opus_head(
            channels=2, mapping=1, table=bytes([2, 0, 0, 1])
        ),
        # The same promise, unkept: the truncation rejection.
        "opus-head-mapping-truncated.bin": opus_head(channels=8, mapping=1),
        # Extreme pre_skip, which is what the rescale multiplies.
        "opus-head-max-preskip.bin": opus_head(pre_skip=0xFFFF, rate=0),
        # An unsupported major version (high nibble non-zero).
        "opus-head-bad-version.bin": opus_head(version=0x10),
        # Zero channels — rejected, and the divisor a mapping table would use.
        "opus-head-zero-channels.bin": opus_head(channels=0),
        # Right magic, one byte short of the 19-byte minimum.
        "opus-head-truncated.bin": opus_head()[:18],

        # --- Ogg granule (REQ-AUD-045) ---
        # final = 0x00A3B140, initial = 0: the ordinary Vorbis case.
        "granule-plain.bin": (0x00A3B140).to_bytes(8, "big") + (0).to_bytes(8, "big"),
        # A negative initial granule is a head trim REQ-AUD-045 must honour.
        "granule-negative-initial.bin": (0x00A3B140).to_bytes(8, "big")
        + (-576 & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "big"),
        # A negative final granule: rejected, and the signed-to-unsigned path.
        "granule-negative-final.bin": (-1 & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "big")
        + (0).to_bytes(8, "big"),
        # A head trim larger than the stream it trims — the inconsistency check.
        "granule-trim-exceeds.bin": (100).to_bytes(8, "big")
        + (-1000 & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "big"),
        # int64 min as the initial granule: negating it is the overflow no
        # implementation may perform on attacker input.
        "granule-int64-min.bin": (1).to_bytes(8, "big") + (1 << 63).to_bytes(8, "big"),
        # All bits set: kUnknownFrames as a derived length, plus every numeric
        # argument at its maximum.
        "granule-all-ones.bin": b"\xff" * 24,
        # Empty. Every derived number is zero and every parser sees no bytes.
        "empty.bin": b"",
    }


SEEDS: dict[str, dict[str, bytes]] = {
    "fuzz_json": json_seeds(),
    "fuzz_text": text_seeds(),
    "fuzz_xinglame": xinglame_seeds(),
    "fuzz_gapless": gapless_seeds(),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed seeds match this script instead of writing them",
    )
    check = parser.parse_args().check

    stale: list[str] = []
    written = 0
    for target, seeds in SEEDS.items():
        directory = CORPUS / target
        if not check:
            directory.mkdir(parents=True, exist_ok=True)
        for name, payload in seeds.items():
            path = directory / name
            if check:
                if not path.exists():
                    stale.append(f"{target}/{name}: missing")
                elif path.read_bytes() != payload:
                    stale.append(f"{target}/{name}: differs from make-seeds.py")
                continue
            path.write_bytes(payload)
            written += 1

    total = sum(len(s) for s in SEEDS.values())
    if check:
        if stale:
            print(f"{len(stale)} seed problem(s):\n", file=sys.stderr)
            for problem in stale:
                print(f"  {problem}", file=sys.stderr)
            print(
                "\nRun `python3 desktop/tests/fuzz/make-seeds.py` and commit the result.",
                file=sys.stderr,
            )
            return 1
        print(f"fuzz seeds: {total} generated seed(s) match make-seeds.py")
        return 0

    print(f"fuzz seeds: wrote {written} of {total} seed(s) under {CORPUS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

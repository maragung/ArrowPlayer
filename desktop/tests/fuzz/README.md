# Fuzz targets

Spec §21.6 — `REQ-SEC-011` (targets and a committed, growing corpus) and
`REQ-SEC-012` (targets build with ASan+UBSan; any finding is a release blocker
and its input enters the regression corpus).

## What exists, and what does not

`REQ-SEC-011` names seventeen targets. **One of them exists**, plus three
supporting targets that are not among the seventeen. A fuzz target needs a parser
to point at, and sixteen of those parsers have not been written yet — they belong
to Phase 2 and later. Empty harnesses would satisfy a file listing and nothing
else, so they are not here; the gap is recorded in
[OQ-043](../../../docs/OPEN-QUESTIONS.md).

| Target | State | Reads |
|---|---|---|
| `fuzz_xinglame` | **present** | MP3 frame headers, the Xing/Info tag, the LAME extension and its CRC |
| `fuzz_id3` | absent — no tag layer (Phase 2) | ID3v1/v2 frames |
| `fuzz_vorbiscomment` | absent — no tag layer (Phase 2) | Vorbis comment blocks |
| `fuzz_apev2` | absent — no tag layer (Phase 2) | APEv2 items |
| `fuzz_mp4atoms` | absent — no container parser (Phase 2) | MP4 atom trees |
| `fuzz_cue` | absent — no cue parser (Phase 3) | cue sheets |
| `fuzz_playlist` | absent — no playlist I/O (Phase 3) | M3U, PLS, XSPF |
| `fuzz_lrc` | absent — no lyrics support (Phase 7) | LRC timing lines |
| `fuzz_theme` | absent — no theme engine (Phase 5) | `theme.json` |
| `fuzz_layout` | absent — no layout engine (Phase 5) | `.eclayout` |
| `fuzz_skinzip` | absent — no skin loader (Phase 5) | skin archives |
| `fuzz_efs` | absent — no format-string engine (Phase 4) | Eclipse Format Strings |
| `fuzz_smartrule` | absent — no smart-playlist engine (Phase 3) | smart-playlist rules |
| `fuzz_icy` | absent — no streaming (Phase 8) | ICY metadata |
| `fuzz_rss` | absent — no podcast support (Phase 8) | RSS feeds |
| `fuzz_ipc` | absent — no IPC (Phase 9) | single-instance messages |
| `fuzz_syncmsg` | absent — no sync (Phase 10) | §18 wire messages |

Three supporting targets are not in that list of seventeen and are here anyway.
The rule they share: the parser exists, and untrusted bytes reach it today. A
parser that is shipped and unfuzzed is the gap that matters, whatever the spec's
target list happens to name it.

| Target | Why it exists |
|---|---|
| `fuzz_json` | `fuzz_theme` and `fuzz_layout` both feed bytes through `core/json` before a single schema keyword is consulted, so this is their shared foundation. Fuzzing it now means those two arrive with the parser underneath them already hardened, rather than being the first thing to exercise it. |
| `fuzz_text` | Every one of the seventeen reaches `core/text`: tag values, file names, cue sheets and theme strings all arrive as bytes and become `std::string`. A decoder that can be walked off the end of a buffer would be reachable through all of them at once. |
| `fuzz_gapless` | `fuzz_xinglame` covers the MP3 side of `audio/decode/gapless_info.hpp`. Three other parsers in that same header take untrusted input and had no coverage: `parse_itunsmpb` (which `REQ-AUD-042` names a fuzz target in so many words), `parse_opus_head` (where `channel_mapping` promises a table sized by `channel_count`), and `gapless_from_granule` (where a negative granule becomes an unsigned skip). `fuzz_mp4atoms` will drive the iTunSMPB *value* parser through a real atom tree in Phase 2; that target is about the tree, this one is about the value. |

None is a stand-in for a named target, and none is counted as one.

## Two binaries per target

```text
fuzz_<name>          libFuzzer. Built only where the compiler supports
                     -fsanitize=fuzzer, i.e. clang. Explores new inputs.
fuzz_<name>_replay   the same harness with a plain main(). Built everywhere,
                     registered as the CTest case fuzz_corpus.fuzz_<name>.
                     Replays the committed corpus and nothing else.
```

The split follows from what `REQ-SEC-011` and `REQ-SEC-012` actually ask for.
Exploring new inputs needs a fuzzing engine; keeping old crashes dead does not.
Making the corpus an ordinary test means it is replayed by **every** preset —
including `linux-asan`, whose ASan+UBSan is the instrumentation `REQ-SEC-012`
names — so a regression is caught by the same `ctest` run a contributor already
does, not only by a nightly job. A corpus that only the fuzzing job replays is an
archive, not a regression suite.

The replay driver walks the corpus directory at **run time**. Dropping a crash
input into `corpus/<target>/` makes it a regression case immediately, with no
CMake re-run — because a step that needs a re-configure to notice a new seed is a
step somebody forgets during an incident.

## The corpus

Seeds live in `corpus/<target>/`, one directory per target, holding inputs and
nothing else — the driver has no exclusion list, because a driver that skips
files by name can be made to skip the crash input.

`make-seeds.py` generates the seeds that a human cannot review as bytes: nobody
can check an MP3 frame's LAME CRC by squinting at a hexdump, but anyone can read
the code that computes it. It writes only the files it knows about, so inputs
added by libFuzzer or by `REQ-SEC-012`'s regression rule are never overwritten.

```bash
python3 desktop/tests/fuzz/make-seeds.py            # regenerate
python3 desktop/tests/fuzz/make-seeds.py --check    # CI: committed bytes match
```

Seeds are **committed**, as `REQ-SEC-011` requires. A corpus the build invents is
a corpus that changes underneath you, and the one thing a regression corpus must
not do is change.

Which is why `.gitattributes` marks `desktop/tests/fuzz/corpus/**` as `-text`.
The repository normalises line endings on commit, and 23 of the 49 seeds here
contain no NUL byte, so Git's heuristic reads them as text and would rewrite a
CRLF to an LF. None of today's seeds contain one, so this changes nothing right
now — it is here for the seeds that come next. An ICY header is CRLF-delimited by
protocol; CUE sheets and `.lrc` files usually are. A normalised seed is a
different input than the one that found the bug, and it would fail `--check` in a
fresh clone while passing in the tree where it was written.

## Running them

```bash
# The corpus, under ASan+UBSan, as part of the ordinary suite:
cd desktop && ctest --preset linux-asan -R '^fuzz_corpus\.'

# The real thing, where clang is available:
CC=clang CXX=clang++ cmake --preset linux-fuzz
cmake --build --preset linux-fuzz --parallel
./../build/linux-fuzz/tests/fuzz/fuzz_xinglame \
    desktop/tests/fuzz/corpus/fuzz_xinglame -max_total_time=60
```

`linux-fuzz` sets `ECLIPSE_BUILD_TESTS=OFF`, so the harnesses build without
GoogleTest — and the domain sources are recompiled into `eclipse-domain-fuzz`
with `-fsanitize=fuzzer-no-link`. Linking the ordinary `eclipse-domain` would
still find crashes, but only by luck: libFuzzer would get no coverage feedback
from the parsers, which are the entire attack surface.

## Writing a harness

1. One invariant per `fail()` call, each naming the property that broke rather
   than the line that broke. A fuzz failure is read by whoever is on call.
2. Assert properties, not outputs. `parse()` returning an error is not a bug; an
   error with an empty user message is, because `REQ-GEN-060` promises the user a
   message.
3. Leave limits at their defaults so the depth, element and byte guards are
   themselves under test.
4. Add seeds to `make-seeds.py` for every branch the harness can reach, and say
   in a comment what each seed is *for*. A seed nobody can explain is a seed
   nobody will maintain.

## Findings so far

| Found by | Defect | Fix |
|---|---|---|
| `fuzz_text` | `normalize_relative_path()` returned `true` for `/absolute/path` (quietly relativising it) and for a name containing a NUL, both of which `REQ-THM-018` requires be rejected and which `is_unsafe_relative_path()` did reject | The normaliser now refuses what the security check refuses, and asserts that postcondition on its own output. Pinned by `Normalize.RefusesWhatTheSecurityCheckRefuses` and `Normalize.AcceptanceImpliesSafety`. |
| `fuzz_gapless` | `gapless_from_granule()` computed `-initial_granule` on an `int64_t` read out of a file. For `INT64_MIN` that negation is undefined behaviour — the negative range is one wider than the positive one — and UBSan said so: *negation of -9223372036854775808 cannot be represented in type 'long int'* | Negate in the unsigned domain, which is exact for every value including that one, then apply the same 32-bit bound. Pinned by `GranuleGapless.RejectsMostNegativeInitialGranuleWithoutOverflowing`. |

Both were found by the **first** run of a new corpus, before any mutation — which
is the argument for the replay case existing at all. Neither needed libFuzzer;
what they needed was a harness that states an invariant and a driver that feeds it
bytes somebody thought about.

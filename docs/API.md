# API Reference

§27 requires this document to cover *every public module API, with thread- and
RT-safety noted per function*. `REQ-GEN-054` is what makes it load-bearing: every
module exposes **exactly one** public header that defines its surface, everything
else in that directory is internal, and this document is where the surface is
written down. See
[`docs/ARCHITECTURE.md`](ARCHITECTURE.md#module-boundary-contract) for the rule;
this is its inventory.

Two things to state before the tables, because both are places where the document
could quietly claim more than it delivers.

**§27 says this document is *generated from doc-comments*. It is hand-written.**
The doc-comments in the headers are still the source of truth — everything below
was transcribed from them, not paraphrased from memory — but nothing mechanically
guarantees that. The deviation and its proposed fix are recorded as
[OQ-033](OPEN-QUESTIONS.md).

**The RT column states permission, not a measurement.** `REQ-AUD-017` is a rule
about what may be *called* from the audio callback, and the only functions that
may be called are the ones that carry `/// RT-SAFE:`. A function that happens to
allocate nothing is still not callable if it is unannotated, so that is what the
column reports. The vocabulary is below.

- [How to read the tables](#how-to-read-the-tables)
- [`core/error.hpp` — errors as values](#coreerrorhpp--errors-as-values)
- [`core/text.hpp` — UTF-8, sort keys, path safety](#coretexthpp--utf-8-sort-keys-path-safety)
- [`core/json/json.hpp` — the hardened parser](#corejsonjsonhpp--the-hardened-parser)
- [`audio/dsp/biquad.hpp` — one filter section](#audiodspbiquadhpp--one-filter-section)
- [`audio/dsp/equalizer.hpp` — the EQ stage](#audiodspequalizerhpp--the-eq-stage)
- [`audio/decode/gapless_info.hpp` — trim metadata](#audiodecodegapless_infohpp--trim-metadata)
- [`eclipse/version.hpp` — generated](#eclipseversionhpp--generated)
- [What is not here yet](#what-is-not-here-yet)

## How to read the tables

### Thread-safety vocabulary

Three terms, used with exactly these meanings and nothing implied beyond them.

| Term | Meaning |
|---|---|
| **pure** | A free function over its arguments with no shared mutable state. Any number of threads may call it concurrently, on the same or different inputs. |
| **const-shared** | A `const` member function. Concurrent calls on one object are safe **provided no thread is calling a mutating member on that object**. There is no internal lock; the caller owns the exclusion. |
| **exclusive** | Mutates the object. One caller at a time, and no concurrent `const-shared` readers of the same object. |

None of the types below is internally synchronised, and that is deliberate.
`REQ-GEN-053` requires every real-time boundary to be a lock-free hand-off of
pre-allocated state, so a mutex hidden inside a domain type would be a lock on the
audio thread that no reviewer can see. Where a value has to cross into the RT
thread, the crossing is the parameter snapshot of `REQ-AUD-016`, described in
[`docs/AUDIO-ENGINE.md`](AUDIO-ENGINE.md#threads-and-the-real-time-rules) — not a
lock around the object.

### RT-safety vocabulary

| Token | Meaning |
|---|---|
| **RT-SAFE** | The header carries `/// RT-SAFE:` and [`tools/check-rt-safety.py`](../tools/check-rt-safety.py) has checked the body against the §8.2.3 forbidden list. Callable from the audio callback. |
| **unannotated** | Carries no annotation, so per `REQ-AUD-017` it **must not** be called from the callback. This says nothing about whether it would qualify — an unannotated function is out of bounds either way. |
| **never** | Allocates, throws, or does unbounded work. It could not be annotated truthfully, and naming that here is more useful than leaving a reader to infer it. |

The gate verifies claims in one direction only: it fails a function that *claims*
RT safety while containing `new`, a lock, a `throw`, container growth,
`std::to_string`, `std::shared_ptr`, or a log call. It cannot flag a call from the
RT path into a function that lacks the annotation, which is the grep
`REQ-AUD-017` actually names. That direction is recorded as
[OQ-034](OPEN-QUESTIONS.md); it is implementable once `audio/graph/rt_*` exists to
be the call site.

Today the count is small enough to state outright. The gate reports **seven**
`/// RT-SAFE:` annotations, all in `audio/dsp/`, and no other file in the tree
carries one. An eighth line in `biquad.hpp` reads *"RT-SAFE once `resize()` has
been called"* — a statement about the `BiquadCascade` class, not a function
annotation, and the gate does not count it, because its marker requires the colon.
That is the correct outcome: the promise there is conditional on a call the class
cannot enforce, so it is prose for a reader rather than a claim for a checker.

## `core/error.hpp` — errors as values

`REQ-GEN-060`: errors are values, not exceptions, across every module boundary.
Exceptions may be used inside a module for genuinely exceptional conditions but
must not cross a port boundary and must never cross the RT thread. This header is
that contract in code, and no function in it throws by design — the allocating
ones can still fail to allocate, which is the one exception the standard library
reserves to itself.

`REQ-GEN-063` is the other rule the header encodes: a message shown to the user
never consists of a code. Every `Error` therefore carries **both** a translated
user message and a technical detail, and the numeric code exists for logs, tests
and bug reports only.

### Enumerations

| Type | Values | Contract |
|---|---|---|
| `Severity` | `Trace`, `Debug`, `Info`, `Notice`, `Warning`, `Error`, `Critical` | `REQ-GEN-062` binds each level to a required UI behaviour — `Notice` is a non-blocking auto-dismissing inline notice, `Warning` a persistent dismissible banner naming the degradation, `Error` an inline error with a retry or fix action, `Critical` a modal with a clear next step. The presentation layer must not invent its own mapping. |
| `RecoveryAction` | `None`, `Retry`, `SkipTrack`, `ChooseAnotherDevice`, `ReopenDevice`, `IncreaseBuffer`, `Rescan`, `GrantPermission`, `OpenSettings`, `RestoreDefaults`, `ViewLog` | An enum, never a callback. That keeps an `Error` copyable, loggable, and safe to move across a thread boundary; a callback would make it none of those. |
| `ErrorCode` | `Ok = 0`; generic from 1; filesystem from 100; decode/audio from 200; device from 300; parsing from 400; theme/skin from 500; database from 600; network from 700 | Numeric values are **stable**. The gaps between blocks are the point: a new device error is added at the end of the 300 block without renumbering anything a log or a test already refers to. |

The blocks in full, because a stable code is only useful if it is written down
somewhere other than the enum:

| Block | Codes |
|---|---|
| generic (1) | `Unknown`, `NotImplemented`, `InvalidArgument`, `OutOfRange`, `Cancelled`, `Timeout`, `ResourceExhausted` |
| filesystem (100) | `FileNotFound`, `PermissionDenied`, `PathTooLong`, `PathTraversal`, `DiskFull`, `IoError`, `NotADirectory` |
| decode / audio (200) | `UnsupportedFormat`, `CorruptStream`, `DecoderInitFailed`, `SeekFailed`, `NoAudioStream` — the set `REQ-AUD-027` requires a decoder to distinguish |
| device (300) | `DeviceNotFound`, `DeviceInUse`, `DeviceFormatUnsupported`, `DeviceLost`, `ExclusiveModeUnavailable`, `BitPerfectUnavailable`, `BufferUnderrun` |
| parsing (400) | `ParseError`, `UnexpectedToken`, `UnexpectedEnd`, `MalformedTimestamp`, `MalformedHeader`, `ChecksumMismatch`, `InputTooLarge`, `NestingTooDeep`, `OutputCapExceeded` (`REQ-EFS-009`) |
| theme / skin (500) | `SchemaViolation`, `SchemaVersionTooNew`, `AppVersionTooOld`, `UnknownComponent`, `UnknownBinding`, `UnknownAction`, `ContrastBelowFloor` (`REQ-THM-041`), `ResourceBudgetExceeded` (`REQ-THM-033`), `ZipSlipDetected` (`REQ-THM-018`), `ZipBombDetected` (`REQ-THM-017`), `UnsafeSvg` (`REQ-THM-042`), `MissingRequiredFile` |
| database (600) | `DatabaseCorrupt`, `MigrationFailed`, `ConstraintViolation`, `QueryFailed` |
| network (700) | `NetworkDisabled` (the `REQ-NET-001` global switch is off), `NetworkUnreachable`, `TlsError`, `HttpError`, `RateLimited` |

### `class Error`

| Function | Thread | RT | Notes |
|---|---|---|---|
| `Error()` | pure | unannotated | Code defaults to `Unknown`, severity to `Error`. |
| `Error(ErrorCode, std::string user_message)` | pure | never | Takes the message by value; allocates. |
| `Error(ErrorCode, std::string user_message, std::string technical_detail)` | pure | never | |
| `Error(ErrorCode, std::string, std::string, Severity, RecoveryAction = None)` | pure | never | The full form. Everything else is a shorthand for it. |
| `code() const noexcept` → `ErrorCode` | const-shared | unannotated | |
| `severity() const noexcept` → `Severity` | const-shared | unannotated | |
| `recovery() const noexcept` → `RecoveryAction` | const-shared | unannotated | |
| `user_message() const noexcept` → `const std::string&` | const-shared | unannotated | Translated, actionable, jargon-free, and never contains a numeric code (`REQ-GEN-063`). |
| `technical_detail() const noexcept` → `const std::string&` | const-shared | unannotated | **For logs only.** May contain paths, so it is subject to the `REQ-SET-013` redaction rules before being written at info level or above. |
| `with_severity(Severity) noexcept` → `Error&` | exclusive | unannotated | Returns `*this` for chaining. |
| `with_recovery(RecoveryAction) noexcept` → `Error&` | exclusive | unannotated | |
| `with_detail(std::string) → Error&` | exclusive | never | Allocates. |
| `at(offset, line = 0, column = 0) noexcept` → `Error&` | exclusive | unannotated | Parser position, so an editor can point at the offending character instead of saying "invalid input". |
| `offset() / line() / column() const noexcept` → `std::size_t` | const-shared | unannotated | Zero when unset. |
| `to_log_string() const` → `std::string` | const-shared | never | Single-line rendering for logs and test failure output. |

### `Result<T>`, `Status`, and the free functions

`Result<T>` is `[[nodiscard]]` over `std::variant<T, Error>`. `Unit` is the tag
type that lets `Result<void>` exist without a special case, and `Status` is
`Result<Unit>`.

| Function | Thread | RT | Notes |
|---|---|---|---|
| `Result(T)` / `Result(Error)` | pure | never for allocating `T` | Implicit from either alternative, so `return err(...)` and `return value` both read naturally. |
| `has_value() const noexcept` → `bool` | const-shared | unannotated | |
| `explicit operator bool() const noexcept` | const-shared | unannotated | |
| `value()` — `&`, `const&`, `&&` | const-shared for `const&`, otherwise exclusive | unannotated | **Precondition: `has_value()`.** Calling it on an error is undefined; `std::get` will terminate rather than return garbage. |
| `error()` — `const&`, `&&` | const-shared / exclusive | unannotated | **Precondition: `!has_value()`.** |
| `value_or(T fallback) const&` → `T` | const-shared | never for allocating `T` | Takes the fallback by value, so the copy happens whether or not it is used. |
| `operator->` / `operator*` | const-shared / exclusive | unannotated | Same precondition as `value()`. |
| `to_string(ErrorCode) noexcept` → `std::string_view` | pure | unannotated | Returns a static literal — a switch over the enum, no allocation. Stable strings: they appear in logs and tests. |
| `to_string(Severity) noexcept` → `std::string_view` | pure | unannotated | |
| `ok() noexcept` → `Status` | pure | unannotated | |
| `err(ErrorCode, std::string)` → `Error` | pure | never | |
| `err(ErrorCode, std::string, std::string)` → `Error` | pure | never | |

The `value()`/`error()` preconditions are the one sharp edge in this header. They
are checked by `std::get`, which throws on the wrong alternative — an exception
that would cross a port boundary, which `REQ-GEN-060` forbids. So the discipline
is that a caller tests before it reads: `if (!r) return r.error();`. The
`[[nodiscard]]` on the class is what makes the compiler insist the test happens at
all.

## `core/text.hpp` — UTF-8, sort keys, path safety

Everything here is a free function over `std::string_view`, so every entry is
**pure**: there is no shared state to protect. Anything returning `std::string` or
`std::vector` allocates and is therefore **never** RT-callable; the `string_view`
and scalar returns are allocation-free but unannotated, and the module is not on
the audio path in any case.

The functions that return `std::string_view` return views **into their argument**.
The caller owns the lifetime; a view outliving its string is a dangling read, and
nothing in the type system stops it.

### UTF-8

| Function | Thread | RT | Notes |
|---|---|---|---|
| `decode_utf8(std::string_view, std::size_t& pos) noexcept` → `char32_t` | pure | unannotated | Advances `pos` past the sequence. Malformed input yields U+FFFD and still advances, so a decode loop over hostile bytes always terminates. |
| `is_valid_utf8(std::string_view) noexcept` → `bool` | pure | unannotated | Rejects overlongs, surrogates and truncation, not just bad lead bytes. |
| `encode_utf8(char32_t, std::string& out)` | exclusive on `out` | never | Appends; may grow the string. |
| `utf8_length(std::string_view) noexcept` → `std::size_t` | pure | unannotated | Codepoints, not bytes. |
| `utf8_offset_of(std::string_view, std::size_t n) noexcept` → `std::size_t` | pure | unannotated | Byte offset of codepoint `n`, or `size()` when beyond the end — never out of range. |
| `utf8_substr(std::string_view, start, count = npos)` → `std::string` | pure | never | By codepoint index and count, never splitting a sequence. |
| `sanitize_utf8(std::string_view)` → `std::string` | pure | never | Replaces malformed sequences with U+FFFD. The boundary function for anything read off disk or a network: a tag, a filename, a lyric file. |

### Case, diacritics, trimming, classification

| Function | Thread | RT | Notes |
|---|---|---|---|
| `to_lower(char32_t) noexcept` / `to_upper(char32_t) noexcept` → `char32_t` | pure | unannotated | Single-codepoint mapping. No 1→many cases (no ß → SS). |
| `to_lower(std::string_view)` / `to_upper(std::string_view)` → `std::string` | pure | never | |
| `to_title(std::string_view)` → `std::string` | pure | never | Word-initial capitals. |
| `strip_diacritic(char32_t) noexcept` → `char32_t` | pure | unannotated | Maps a precomposed accented letter to its base. Feeds `sort_key()`; not a general normaliser. |
| `trim` / `trim_left` / `trim_right(std::string_view) noexcept` → `std::string_view` | pure | unannotated | A view into the argument. |
| `collapse_whitespace(std::string_view)` → `std::string` | pure | never | |
| `is_space` / `is_digit` / `is_alpha` / `is_alnum(char32_t) noexcept` → `bool` | pure | unannotated | |

### Case-insensitive comparison

| Function | Thread | RT | Notes |
|---|---|---|---|
| `iequals(a, b) noexcept` → `bool` | pure | unannotated | |
| `istarts_with(s, prefix) noexcept` → `bool` | pure | unannotated | |
| `iends_with(s, suffix) noexcept` → `bool` | pure | unannotated | |
| `icontains(s, needle) noexcept` → `bool` | pure | unannotated | |

These fold ASCII only. That is a deliberate limit, not an oversight: they exist for
matching keys, extensions and tag field names, where the input alphabet is known.
User-visible search goes through `sort_key()`, which folds case *and* strips
diacritics, so a Turkish dotless ı is handled where it matters and not pretended at
elsewhere.

### Sort keys — `REQ-LIB-029`

| Function | Thread | RT | Notes |
|---|---|---|---|
| `default_articles(std::string_view locale)` → `std::vector<std::string>` | pure | never | `"en"` yields *the / a / an*. `REQ-LIB-029` makes the list configurable, defaulting to the UI language. |
| `sort_key(display, const std::vector<std::string>& articles)` → `std::string` | pure | never | The normalisation is exactly: casefold → strip diacritics → strip leading article → collapse spaces. |
| `sort_key(display, std::string_view locale = "en")` → `std::string` | pure | never | Convenience over `default_articles(locale)`. |

The invariant that matters more than the algorithm: **sorting uses the key, display
always uses the original string.** "The Beatles" sorts under B and is shown as "The
Beatles". A UI that renders the sort key has thrown away the artist's name.

### Splitting, joining, replacing

| Function | Thread | RT | Notes |
|---|---|---|---|
| `split(s, char delim, bool keep_empty = true) noexcept` → `std::vector<std::string_view>` | pure | never | The vector allocates; the elements are views into `s`. |
| `split_multi(s, const std::vector<std::string>& separators)` → `std::vector<std::string>` | pure | never | Multi-character separators, **longest match first**. `REQ-LIB-028`: this is the multi-valued artist/genre splitter, and `,` is deliberately **not** a default separator — "Earth, Wind & Fire" is one artist. |
| `join(const std::vector<std::string>&, glue)` → `std::string` | pure | never | |
| `replace_all(s, find, repl)` → `std::string` | pure | never | Single pass; the replacement is never rescanned. |

### Numbers

| Function | Thread | RT | Notes |
|---|---|---|---|
| `parse_int(std::string_view, std::int64_t& out) noexcept` → `bool` | pure | unannotated | Strict: the whole trimmed input must be consumed. **Returns false on overflow**, so hostile input cannot wrap into a plausible value. |
| `parse_double(std::string_view, double& out) noexcept` → `bool` | pure | unannotated | |
| `parse_hex(std::string_view, std::uint64_t& out) noexcept` → `bool` | pure | unannotated | |

`out` is untouched on failure, so `bool ok = parse_int(s, n)` cannot leave a caller
reading a half-written value.

### Matching

| Function | Thread | RT | Notes |
|---|---|---|---|
| `glob_match(pattern, subject, case_sensitive = false) noexcept` → `bool` | pure | unannotated | Supports `*`, `?`, `[chars]` and `[!negation]`. |
| `edit_distance(a, b, max_distance) noexcept` → `std::size_t` | pure | unannotated | Damerau-Levenshtein, **bounded**: returns `max_distance + 1` rather than the true distance once the bound is exceeded (§9.5 fuzzy stage). |

`glob_match` is deliberately **not** regex. `REQ-PLS-013` excludes regex from smart
playlists because catastrophic backtracking is a denial-of-service surface, and a
smart playlist is a shareable file. A glob has no backtracking blow-up to exploit,
and `edit_distance`'s bound is what keeps the fuzzy stage's cost proportional to
the bound rather than to the length of whatever a stranger put in a tag.

### Paths — `REQ-THM-018` / `REQ-SEC-008`

| Function | Thread | RT | Notes |
|---|---|---|---|
| `is_unsafe_relative_path(std::string_view) noexcept` → `bool` | pure | unannotated | True if the path contains a traversal segment, an absolute prefix, a drive letter, a NUL, or a control character. |
| `normalize_relative_path(std::string_view, std::string& out)` → `bool` | pure (writes `out`) | never | Normalises separators to `/` and resolves `.` and `..` textually. **Returns false if the result would escape the root.** |

These two are **security controls, not conveniences**. Every entry name in a
`.eclipseskin` archive passes through them before anything is written, which is
what makes zip-slip (`ErrorCode::ZipSlipDetected`) a rejection rather than a
file written outside the extraction directory. `normalize_relative_path` resolves
textually and never touches the filesystem, so it cannot be defeated by a symlink
that appears between the check and the write.

## `core/json/json.hpp` — the hardened parser

Why there is a parser here at all: the domain layer links against nothing but the
standard library (`REQ-GEN-050`), and every JSON document this application reads is
untrusted — a downloaded skin, an imported settings bundle. That combination makes
the limits the feature. `tests/fuzz/fuzz_json.cpp` fuzzes it today, replaying ten
committed seeds on every build. `fuzz_theme`, the target `REQ-SEC-011` names,
validates a theme document against the schema rather than parsing arbitrary JSON,
so it arrives with the theme engine in Phase 5.

A parsed `Value` is **immutable after parse**. There are no mutating accessors, and
every lookup returns `const Value*` or a copy. That is what makes a parsed document
safe to hand to several readers at once without a lock.

### `Limits`

| Field | Default | Why |
|---|---|---|
| `max_bytes` | 8 MiB | `REQ-THM-017` — the skin-package size limit. `ErrorCode::InputTooLarge`. |
| `max_depth` | 64 | Nesting guard. A recursive-descent parser without one is a stack overflow waiting for a `[[[[[…` — `ErrorCode::NestingTooDeep`. |
| `max_elements` | 200 000 | Total nodes. Bounds a document that is shallow and enormous rather than deep. |
| `allow_comments` | `true` | `//` and `/* */`, so the schema files can be JSONC and carry their reasoning. |
| `allow_trailing_commas` | `true` | Hand-edited skins. |

The defaults are the values the spec mandates for skin packages. A caller handling
a larger *trusted* document raises them explicitly — the point being that raising a
limit is a visible act at the call site, not a default nobody chose.

### `Value`

`using Array = std::vector<Value>` and
`using Object = std::map<std::string, Value, std::less<>>`. The transparent
comparator is what lets `find()` take a `std::string_view` without allocating a
`std::string` to look up.

| Function | Thread | RT | Notes |
|---|---|---|---|
| `Value()` / `Value(bool)` / `Value(double)` `noexcept` | pure | unannotated | |
| `Value(std::string)` / `Value(Array)` / `Value(Object)` | pure | never | Array and object storage is `unique_ptr`-held, so a `Value` stays a fixed, small size regardless of what it contains. |
| copy constructor and copy assignment | pure | never | Deep copy — `copy_from` walks the tree. |
| move constructor and move assignment `noexcept` | exclusive | unannotated | |
| `type() const noexcept` → `Type` | const-shared | unannotated | `Null`, `Bool`, `Number`, `String`, `Array`, `Object`. |
| `is_null` / `is_bool` / `is_number` / `is_string` / `is_array` / `is_object() const noexcept` | const-shared | unannotated | |
| `is_integer() const noexcept` → `bool` | const-shared | unannotated | JSON has one number type. This asks whether the stored `double` holds an exact integer, which is the question a schema's `"type": "integer"` actually poses. |
| `as_bool(fallback = false)` / `as_double(0.0)` / `as_int(0) const noexcept` | const-shared | unannotated | **Never throws on a type mismatch** — returns the fallback. A validator wants to report every problem in one pass, not abort on the first. |
| `as_string(fallback = {}) const noexcept` → `std::string_view` | const-shared | unannotated | A view into the `Value`. Outliving the document is a dangling read. |
| `as_array() const` → `const Array&` | const-shared | unannotated | Returns a reference to a **shared static empty `Array`** on a type mismatch — it does not throw. So `for (const auto& e : v.as_array())` over a non-array iterates zero times instead of being undefined, and a walker needs no guard. |
| `as_object() const` → `const Object&` | const-shared | unannotated | Same, with a static empty `Object`. |
| `find(std::string_view key) const noexcept` → `const Value*` | const-shared | unannotated | `nullptr` when absent **or when this is not an object** — one branch handles both, which is what a schema walker wants. |
| `at(std::size_t index) const noexcept` → `const Value*` | const-shared | unannotated | `nullptr` when out of range or not an array. Never undefined. |
| `pointer(std::string_view) const noexcept` → `const Value*` | const-shared | unannotated | RFC 6901 JSON Pointer, e.g. `/color/text/primary`. `REQ-THM-060` requires a validation error to name the exact offending node, and this is how it does. |
| `size() const noexcept` → `std::size_t` | const-shared | unannotated | Elements, members, or 0 for a scalar. |
| `dump(int indent = 0) const` → `std::string` | const-shared | never | `indent == 0` is compact. |

### Parsing

| Function | Thread | RT | Notes |
|---|---|---|---|
| `parse(std::string_view, const Limits& = {})` → `Result<Value>` | pure | never | On failure the `Error` carries line and column through `Error::at()`, so a skin editor can point at the node. |
| `escape(std::string_view)` → `std::string` | pure | never | Emits a complete JSON string literal, **including the surrounding quotes**. |

Two behaviours worth stating because they are the ones a caller is most likely to
assume wrongly. A **duplicate key is reported, not silently overwritten** — a skin
that sets `"primary"` twice is a skin whose author does not know which value wins,
and last-one-wins would hide that. And **every error carries a position**, which is
the difference between a validator and a rejection.

## `audio/dsp/biquad.hpp` — one filter section

This is the only header in the tree with `/// RT-SAFE:` annotations, and the
reason the module exists as its own file is that a filter section is the smallest
thing on the audio path that has *state*. Everything below is per-channel: a
`Biquad` holds one channel's delay line, so **sharing one instance across two
channels or two threads corrupts both**. That is not a caveat, it is the ownership
model.

Direct Form I with `double` state, deliberately, for two reasons given in the
header: the state is the raw input/output history, which keeps coefficient
cross-fading (`REQ-AUD-085`) well-behaved because the state stays meaningful when
coefficients change; and `REQ-AUD-082` requires `double` state because `float`
state accumulates audible error in low-frequency, high-Q sections, while the cost
of double state is negligible next to the memory traffic of the sample buffers
themselves.

### Design-time functions

| Function | Thread | RT | Notes |
|---|---|---|---|
| `q_for_bandwidth_octaves(double octaves) noexcept` → `double` | pure | unannotated | `REQ-AUD-083`: Q = √(2^N) / (2^N − 1). N = 1.0 (octave, 10-band) → 1.4142; N = 0.5 (half-octave, 18-band) → 2.8710. |
| `design(FilterType, f0_hz, sample_rate_hz, q, gain_db) noexcept` → `BiquadCoeffs` | pure | unannotated | RBJ cookbook formulas. **Returns `identity()`** when the filter would be a no-op, when `q <= 0`, or when `f0` exceeds `kMaxNyquistFraction × sample_rate / 2`. |
| `magnitude_db(const BiquadCoeffs&, freq_hz, sample_rate_hz) noexcept` → `double` | pure | unannotated | Evaluates \|H(e^jω)\| in dB. `REQ-AUD-088` requires the UI to plot *this*, not a cosmetic spline, and §8.11 test 6 checks it against measurement. |

`kMaxNyquistFraction = 0.95`. `REQ-AUD-084` requires a band above that to be
**bypassed rather than clamped**, because the bilinear transform's coefficients go
numerically unstable approaching Nyquist. Clamping the frequency would leave a
filter running with coefficients nobody designed; returning the identity leaves the
signal untouched, which is the honest answer to "this band cannot exist at this
sample rate".

### `BiquadCoeffs`

| Member | Thread | RT | Notes |
|---|---|---|---|
| `b0`, `b1`, `b2`, `a1`, `a2` — five `double`s | — | — | H(z) = (b0 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²). `a0` is already divided out. |
| `static constexpr identity() noexcept` | pure | unannotated | Output equals input **bit-exactly**, not approximately. |
| `is_identity() const noexcept` → `bool` | const-shared | unannotated | Exact comparison against the identity, so callers can skip the section outright. `REQ-AUD-005` requires bypass to be a *true* bypass, not a 0 dB run through the arithmetic. |

### `Biquad` and `BiquadCascade`

| Function | Thread | RT | Notes |
|---|---|---|---|
| `Biquad()` / `Biquad(const BiquadCoeffs&) noexcept` | pure | unannotated | |
| `set_coeffs(const BiquadCoeffs&) noexcept` | exclusive | unannotated | Replaces coefficients without touching the state — which is the whole reason for Direct Form I, and what a cross-ramp needs. |
| `coeffs() const noexcept` → `const BiquadCoeffs&` | const-shared | unannotated | |
| `reset() noexcept` | exclusive | unannotated | Clears the delay line. Call on seek and track change, or the previous track's tail is dragged across the boundary. |
| `process_one(float) noexcept` → `float` | exclusive | **RT-SAFE** | One sample. |
| `process_in_place(std::span<float>) noexcept` | exclusive | **RT-SAFE** | |
| `process(std::span<const float> in, std::span<float> out) noexcept` | exclusive | **RT-SAFE** | Sizes must match. |
| `BiquadCascade::resize(std::size_t sections)` | exclusive | never | Allocates the section storage. Call from the UI thread; after this the cascade is RT-callable. |
| `BiquadCascade::size() const noexcept` → `std::size_t` | const-shared | unannotated | |
| `BiquadCascade::set_coeffs(index, const BiquadCoeffs&) noexcept` | exclusive | unannotated | Out-of-range index is ignored rather than undefined. |
| `BiquadCascade::has_section(index) const noexcept` → `bool` | const-shared | unannotated | |
| `BiquadCascade::coeffs(index) const noexcept` → `BiquadCoeffs` | const-shared | unannotated | **By value**, returning the identity when out of range. A pointer-returning accessor would force every caller to prove non-null, and five doubles are cheaper than the branch. |
| `BiquadCascade::reset() noexcept` | exclusive | unannotated | Every section. |
| `BiquadCascade::process_in_place(std::span<float>) noexcept` | exclusive | **RT-SAFE** | Runs every non-identity section in order; identity sections are skipped outright (`REQ-AUD-005`). |
| `BiquadCascade::magnitude_db(freq_hz, sample_rate_hz) const noexcept` → `double` | const-shared | unannotated | The whole cascade's combined response. |

## `audio/dsp/equalizer.hpp` — the EQ stage

Stage 5 of the signal chain. The module's threading contract is the one worked
example of `REQ-AUD-016` in the tree, and it is stated in the header itself:
`configure()` is **not** RT-safe because it allocates and computes transcendental
functions; `process()` **is**; the UI thread calls the first and publishes the
result for the second to pick up. Nothing about that is enforced by the type system,
which is why it is written down here and in
[`docs/AUDIO-ENGINE.md`](AUDIO-ENGINE.md#threads-and-the-real-time-rules).

### Ranges and constants

Every one of these is a spec value, not a taste decision, so they are listed with
the requirement that fixes them.

| Constant | Value | Requirement |
|---|---|---|
| `kBands10` | 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz | `REQ-AUD-082` — the ISO one-octave centres |
| `kBands18` | 18 half-octave centres | `REQ-AUD-082` |
| `kGainMinDb` / `kGainMaxDb` / `kGainStepDb` | −12 / +12 dB, step 0.1 dB | `REQ-AUD-082` |
| `kPreampMinDb` / `kPreampMaxDb` | −12 / +12 dB | `REQ-AUD-082` |
| `kMaxParametricBands` | 10 | `REQ-AUD-086` |
| `kParametricGainMinDb` / `kParametricGainMaxDb` | −24 / +24 dB | `REQ-AUD-086` |
| `kParametricQMin` / `kParametricQMax` | 0.1 / 18.0 | `REQ-AUD-086` |
| `kParametricFreqMinHz` / `kParametricFreqMaxHz` | 20 / 20 000 Hz | `REQ-AUD-086` |
| `kCoeffRampMs` | 32.0 | `REQ-AUD-085` — the coefficient cross-ramp length. **The constant exists; the ramp does not.** It belongs to the RT graph, which is not written yet. |

### Settings and presets

| Function | Thread | RT | Notes |
|---|---|---|---|
| `EqSettings::clamp_to_valid_ranges()` → `bool` | exclusive | never | Clamps every field into range. **Returns `false` if anything had to be changed**, so a caller loading an untrusted preset can report the fact instead of silently accepting a file that asked for +40 dB. |
| `EqSettings::is_neutral() const noexcept` → `bool` | const-shared | unannotated | True when the configuration is audibly a no-op, so the whole stage is skipped (`REQ-AUD-005`). |
| `bands_for_mode(EqMode) noexcept` → `std::span<const double>` | pure | unannotated | Empty for `Parametric`. The span views a `constexpr` array with static storage, so it never dangles. |
| `bandwidth_octaves_for_mode(EqMode) noexcept` → `double` | pure | unannotated | Feeds `q_for_bandwidth_octaves()`: 1.0 for `Graphic10`, 0.5 for `Graphic18`. |
| `builtin_presets()` → `const std::vector<EqPreset>&` | const-shared | never on first call | `REQ-AUD-087`. A function-local static, so the first call constructs it; concurrent first calls are safe under the C++ magic-static rule, and later calls are a plain read. |
| `find_builtin_preset(std::string_view name)` → `const EqPreset*` | const-shared | never on first call | Case-insensitive; `nullptr` when unknown. |

`EqSettings` is what a preset file stores **and** what the `REQ-AUD-016` snapshot
hands to the RT thread. Keeping those the same type is deliberate: a settings
struct that has to be translated into a "runtime" struct is a translation step that
can disagree with the file format, and a preset that plays differently from the way
it was saved is the bug that produces.

### `Equalizer`

| Function | Thread | RT | Notes |
|---|---|---|---|
| `configure(const EqSettings&, std::size_t channels, double sample_rate_hz)` → `Status` | exclusive | **never** | Allocates per-channel cascades and computes coefficients. `channels` and `sample_rate_hz` must be > 0; anything else is an `Error`, not an assertion. |
| `reset() noexcept` | exclusive | unannotated | Clears every delay line. Required on seek and track change — and see the note below, because *who* is allowed to call it is an unresolved question. |
| `process(std::span<std::span<float>> planes) noexcept` | exclusive | **RT-SAFE** | Planar float32, in place. `planes.size()` must equal the configured channel count; extra planes are ignored. |
| `process_channel(std::size_t channel, std::span<float>) noexcept` | exclusive | **RT-SAFE** | Single-channel convenience overload. |
| `is_bypassed() const noexcept` → `bool` | const-shared | unannotated | True when `configure()` decided the settings were neutral. |
| `channels()` / `band_count()` / `sample_rate() const noexcept` | const-shared | unannotated | What the last successful `configure()` established. |
| `magnitude_db(double freq_hz) const noexcept` → `double` | const-shared | unannotated | Pre-amp plus every band, in dB. `REQ-AUD-088`: the real cascaded transfer function, which is what the UI must plot and what §8.11 test 6 verifies against measurement. |
| `response_curve_db(from_hz, to_hz, points) const` → `std::vector<double>` | const-shared | never | Log-spaced sampling of the same function, for plotting. |
| `has_band(std::size_t) const noexcept` → `bool` | const-shared | unannotated | |
| `band_coeffs(std::size_t) const noexcept` → `BiquadCoeffs` | const-shared | unannotated | By value; the identity when the index names no band. |

**`reset()` has no annotation, and a seek happens on the audio thread.** Under
`REQ-AUD-017` that means the callback may not call it, yet the delay lines must be
cleared at a seek or the previous position's tail bleeds into the new one. The
function would qualify for the annotation trivially — it writes zeroes into doubles
— and the reason it does not carry one is that no caller exists yet to need it.
Which of the two resolutions is right (annotate it, or clear state through the
snapshot mechanism) is a decision that belongs to the commit that writes the seek
path, and it is recorded as [OQ-035](OPEN-QUESTIONS.md) rather than settled here by
a document that has no code to settle it against.

## `audio/decode/gapless_info.hpp` — trim metadata

Every function here is **pure**: metadata in, `GaplessInfo` out, no state, no I/O.
The parsers return `Result<T>`, and an `Error` carries `std::string` messages, so
every fallible function is **never** RT-callable — which is correct anyway, since
trim metadata is computed when a track is opened, not in the callback.

The formulas themselves, with their derivations and the precedence order between
sources, are in
[`docs/AUDIO-ENGINE.md`](AUDIO-ENGINE.md#gapless--the-formulas-per-format). This
section is the surface, not the arithmetic.

### Common types

| Name | Thread | RT | Notes |
|---|---|---|---|
| `kUnknownFrames` | — | — | `~std::uint64_t{0}`. The sentinel for "the exact frame count is not known". Not zero, because zero is a legitimate answer. |
| `kMp3DecoderDelay` | — | — | **529**. `REQ-AUD-037` fixes it: the group delay of the polyphase/MDCT filterbank that every compliant MPEG-1 Layer III decoder introduces, added to the encoder delay. |
| `kAacDefaultPriming` | — | — | **1024**. `REQ-AUD-041`'s AAC-LC fallback when no `iTunSMPB` tag exists. |
| `GaplessSource` | — | — | `None`, `Native`, `XingLame`, `ITunSMPB`, `OpusHead`, `Granule`. Surfaced in the track technical-info panel (`REQ-UIX-017`) so the user can be told **why** a boundary is not gapless — `REQ-AUD-038` requires that honesty rather than a silent fade. |
| `to_string(GaplessSource) noexcept` → `std::string_view` | pure | unannotated | Static literal. |
| `GaplessInfo` | — | — | `skip_start_frames`, `skip_end_frames`, `valid_frames`, `source`. Defaulted `operator==`, so tests compare whole structs. |
| `GaplessInfo::supports_sample_exact_splice() const noexcept` → `bool` | const-shared | unannotated | True when `source != None`, i.e. real metadata rather than a fallback guess. |
| `GaplessInfo::playable_frames() const noexcept` → `std::uint64_t` | const-shared | unannotated | Frames after trimming, or `kUnknownFrames`. |
| `can_splice_sample_exactly(outgoing, incoming) noexcept` → `bool` | pure | unannotated | `REQ-AUD-046`. **Both** sides need real metadata; a `None` on either side means the boundary falls back to the `REQ-AUD-049` fade. |

### MP3 — Xing/Info and LAME

| Function | Thread | RT | Notes |
|---|---|---|---|
| `parse_mpeg_frame_header(std::span<const std::uint8_t>)` → `Result<MpegFrameHeader>` | pure | never | Rejects short input, bad sync, reserved version, reserved layer, free or reserved bitrate index, reserved sample-rate index. |
| `parse_xing_lame(std::span<const std::uint8_t>)` → `Result<XingLameTag>` | pure | never | `data` must begin at the frame's sync word: the tag sits after the header **plus** the Layer III side information, whose size depends on version and channel mode, which is why this needs the header first. |
| `gapless_from_xing_lame(const XingLameTag&, samples_per_frame)` → `GaplessInfo` | pure | unannotated | Applies `REQ-AUD-037`. A tag whose LAME CRC failed is treated as **absent** (`REQ-AUD-039`), not as approximately right. |
| `mp3_gapless_info(std::span<const std::uint8_t> first_frame)` → `GaplessInfo` | pure | unannotated | Parse-and-derive in one step. **Never fails**: any parse problem yields the `REQ-AUD-038` fallback (`skip_start = 529`, `source = None`), because an unparseable Xing tag must not stop the file from playing. |

### MP4/M4A, Opus, native, Ogg

| Function | Thread | RT | Notes |
|---|---|---|---|
| `parse_itunsmpb(std::string_view value, total_frames_hint = kUnknownFrames)` → `Result<GaplessInfo>` | pure | never | Space-separated hex fields; `REQ-AUD-040` uses field 2 (priming) → `skip_start`, field 3 (remainder) → `skip_end`, field 4 (original sample count) → `valid_frames`. `REQ-AUD-042` governs failure: wrong field count, non-hex characters, or values exceeding the frame count cause **outright rejection**, never a negative or overflowing skip. Pass `kUnknownFrames` to skip the length cross-check. |
| `aac_fallback_gapless_info(priming = kAacDefaultPriming, total_frames = kUnknownFrames)` → `GaplessInfo` | pure | unannotated | `REQ-AUD-041`: no `iTunSMPB`, so use the decoder-reported priming and record that it is not authoritative. |
| `parse_opus_head(std::span<const std::uint8_t>)` → `Result<OpusHead>` | pure | never | RFC 7845 §5.1. Rejects wrong magic, unsupported version, truncation, zero channels. |
| `OpusHead::output_gain_db() const noexcept` → `double` | const-shared | unannotated | The Q7.8 `output_gain` field in dB. RFC 7845 requires it be applied **independently of, and in addition to, ReplayGain** (`REQ-AUD-043`) — the two are different quantities and treating one as the other is a loudness bug. |
| `gapless_from_opus_head(const OpusHead&, total_frames = kUnknownFrames, output_rate_hz = 48000)` → `GaplessInfo` | pure | unannotated | `pre_skip` is defined in **48 kHz** samples; `output_rate_hz` rescales it when the decoder emits at another rate. Pass 48000 for the native case. |
| `native_gapless_info(std::uint64_t total_frames) noexcept` → `GaplessInfo` | pure | unannotated | `REQ-AUD-044`: FLAC, WavPack, APE, WAV, ALAC carry an exact frame count, so there is nothing to trim. |
| `gapless_from_granule(final_granule, initial_granule = 0)` → `Result<GaplessInfo>` | pure | never | `REQ-AUD-045`. A **negative** `initial_granule` means the encoder trimmed the start, and the requirement is that we honour it as a head skip rather than ignore the sign. |

## `eclipse/version.hpp` — generated

`REQ-BLD-007`. Generated by CMake from `desktop/include/eclipse/version.hpp.in`
into the build tree, which is why it is not in `desktop/src/`. It is a public
surface all the same, and it is what the About dialog must display for Phase 0 exit
gate 7.

| Constant | Type | Notes |
|---|---|---|
| `version::kMajor` / `kMinor` / `kPatch` | `int` | From `PROJECT_VERSION_*`. |
| `version::kString` | `std::string_view` | The full version string. |
| `version::kGitSha` | `std::string_view` | The commit the binary was built from. |
| `version::kGitDirty` | `int` | Non-zero when the working tree had uncommitted changes at configure time. A build that cannot be reproduced from a commit says so about itself. |
| `version::kName` | `std::string_view` | `"Eclipse Player"`. |

Everything is `inline constexpr`, so reading any of it is free and nothing here
touches a thread or the audio path.

## What is not here yet

The six modules above plus the generated version header are the entire public
surface of the tree today. Everything the specification describes above layer 3 has
no header to document:

| Surface | Requirement | Status |
|---|---|---|
| `IDecoder` port | §8.3, `REQ-AUD-025`…`030` | Not written |
| `IAudioSink` port | §8.7, `REQ-AUD-060`…`073` | Not written |
| SPSC ring buffer, RT thread, gapless scheduler | §8.2, §8.4 | Not written |
| Library, database, tag layer | §9 | Not written |
| Theme and skin loader | §11 | Not written |
| Application objects, Qt shell | §7.1 layers 4 and 5 | Not written |

`docs/ROADMAP.md` has the sequencing, [ADR
0011](adr/0011-desktop-first-sequencing.md) the original scope decision, and [ADR
0012](adr/0012-restore-android.md) the reversal of it. When each module lands, its
public header is added here in the same commit — which is the discipline
[OQ-032](OPEN-QUESTIONS.md) exists to make mechanical rather than remembered.

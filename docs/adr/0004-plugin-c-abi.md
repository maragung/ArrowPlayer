# ADR 0004 — A C ABI for the plugin SDK, not C++

- **Status:** Accepted
- **Date:** 2026-08-25
- **Requirements:** REQ-PLG-005, REQ-PLG-006, REQ-PLG-007, REQ-PLG-010, REQ-PLG-011

## Context

The plugin SDK (§16) is `[v1.x]` — it does not ship in 1.0.0. The ABI decision is
made now anyway, because §0.3 requires the v1 architecture to leave a seam for
`[v1.x]` work, and an ABI is the single hardest thing to retrofit. Every host-side
interface that a plugin will eventually touch is shaped by this choice, so making
it late means rewriting the shape.

The host is C++20. The obvious thing is to expose C++ interfaces: abstract classes
with virtual functions, `std::string`, `std::vector`, `std::span`. It reads
better, it needs no glue, and it is what the rest of the codebase already uses.

It also does not work across a shared-library boundary that the project does not
control both sides of.

The concrete failure modes, all of them things that happen rather than things that
could theoretically happen:

1. **There is no stable C++ ABI.** MSVC and the Itanium ABI (GCC/Clang) disagree
   about name mangling, vtable layout, exception propagation, and how objects are
   returned. A plugin built with MinGW cannot be loaded by an MSVC-built host.
   Since Windows is Tier 1 (§3.1) and contributors will use whatever compiler they
   have, this is a first-week problem, not an edge case.
2. **Standard-library types cross the boundary.** `std::string` differs in layout
   between libstdc++ and MSVC's STL, and between debug and release MSVC builds.
   Passing one through a DLL boundary and letting the other side free it is
   undefined behaviour that presents as a heap corruption crash, minutes later, in
   unrelated code.
3. **Exceptions do not cross reliably.** Throwing across a module boundary built by
   a different toolchain is undefined. On the RT path (§8.2.3) exceptions are
   forbidden anyway, but a Metadata plugin parsing a hostile file is exactly where
   one gets thrown.
4. **Virtual-table layout is a versioning trap.** Adding a virtual function to the
   middle of an interface silently reorders every plugin's dispatch. Nothing warns;
   the plugin calls the wrong function with the wrong arguments.
5. **Only C++ could write plugins.** Rust, Zig, Go, Pascal and plain C can all
   produce and consume a C ABI. None can consume a C++ one without hand-written
   shims per compiler.

Winamp's plugin ecosystem — the thing §2.3 explicitly wants to learn from, and the
reason that player outlived its owner — was a C ABI. Plugins compiled in 2001 kept
loading for two decades. That is not nostalgia; it is the property being bought.

## Decision

**The plugin SDK is a pure C ABI.** `desktop/include/arrow/plugin/` contains C
headers, compilable by a C99 compiler, with `extern "C"` guards for C++ callers.

Concretely:

- **Three exported symbols**, and nothing else:
  `arrow_plugin_query`, `arrow_plugin_create`, `arrow_plugin_destroy`.
  `arrow_plugin_query` returns a static `ArrowPluginInfo` and must not
  allocate, block, or touch the filesystem (`REQ-PLG-007`), so the host can
  enumerate a directory of untrusted plugins cheaply and without side effects.
- **Function-pointer vtables**, not virtual classes: `ArrowPluginVTable` is what
  the plugin offers the host, `ArrowHostApi` is what the host offers the plugin.
  Both carry an `abi_version` as their first member so a mismatch is detectable
  before any other field is read.
- **Only C types cross the boundary:** fixed-width integers, `const char *` for
  UTF-8 (null-terminated, owned by whoever created it, documented per function),
  explicit `(pointer, length)` pairs for buffers, and opaque handles for host
  objects. No standard-library types, no ownership transfer of allocations, no
  templates, no inline functions with linkage.
- **No exceptions cross the boundary.** Every entry point returns an `int` status
  code. The host wraps plugin calls; the plugin is documented as required to
  contain its own.
- **Growth is append-only within an ABI version** (`REQ-PLG-006`): members are
  added at the end of a struct, never reordered or removed, and a `size` field
  distinguishes an older caller's struct from a newer one. Breaking the layout
  requires incrementing `ARROW_PLUGIN_ABI_VERSION` and a major release.
- **The host refuses to load an `abi_version` it does not know**, rather than
  attempting a best-effort load. Refusing is a message the user can act on; a
  best-effort load is a crash they cannot.
- **Capabilities are enforced at the call site in the host** (`REQ-PLG-010`), not
  by trusting the manifest. The declaration drives the consent UI; the check is
  in the implementation of every host API function.

## Consequences

**Positive.** A plugin built with any toolchain on a platform loads into any host
build on that platform. Non-C++ languages can write plugins with no shim. The ABI
becomes a small, reviewable surface — a header a person can read in full — instead
of an emergent property of the C++ type system. Append-only growth with an
explicit version is a rule that can be checked mechanically, and will be: an ABI
dump comparison in CI when the SDK lands.

**Positive, and the reason this matters beyond ergonomics.** A pointer-free,
buffer-explicit interface is a prerequisite for `REQ-PLG-011` item 5:
out-of-process hosting for Decoder and Metadata plugins, the two categories that
parse untrusted data. Those are the plugins most likely to be exploited, and
moving them out of process later is only possible if the interface never assumed
a shared address space. A C++ interface passing `std::span` into plugin code
quietly forecloses that. This ADR is what keeps the `[v2]` option open.

**Negative.** The host side needs a C++ wrapper layer so that application code is
not writing C by hand — roughly one thin RAII adapter per plugin category. That is
real work and real code to maintain.

**Negative.** C strings and manual buffer handling are more error-prone than the
C++ equivalents, in exactly the code that touches untrusted plugins. Mitigated by
keeping the surface small, documenting ownership per function, and fuzzing the
host-side wrapper.

**Negative.** Losing type safety at the boundary means an ABI mistake becomes a
runtime crash rather than a compile error. Mitigated by the `abi_version` check,
the size fields, and a conformance test plugin exercising every entry point.

**Honest limitation.** A C ABI does not make plugins safe. A native in-process
plugin cannot be sandboxed, and `REQ-PLG-011` says so in the specification rather
than in a footnote. The ABI buys compatibility and the *possibility* of future
isolation; consent (`REQ-PLG-009`), crash quarantine, the RT watchdog and safe
mode are what buy stability today.

## Alternatives considered

**C++ interfaces with a pinned compiler per platform** — "everyone uses MSVC on
Windows and GCC on Linux". Rejected: it makes the plugin ecosystem hostage to the
host's exact toolchain and standard-library version, including the debug/release
STL split on MSVC, and it forecloses non-C++ plugin authors entirely. The
ecosystem is the point of having plugins at all.

**COM, or a COM-like `IUnknown` scheme** — rejected: Windows-shaped, heavier than
needed, and it still relies on C++ vtable layout being stable across compilers,
which is precisely the thing that is not.

**Out-of-process only, from v1.x, for every category** — the safest option, and
rejected only for now: a DSP plugin on the RT path cannot afford an IPC hop
inside a buffer period (§8.2.4 latency budget), and a Visualizer plugin needs the
GPU context. It stays on the table for Decoder and Metadata as `[v2]`, which this
decision deliberately preserves.

**Scripting instead of native plugins (Lua, WASM)** — rejected for v1.x, but not
dismissed. WASM in particular is the right answer for the sandboxing problem and
is worth revisiting for the categories that do not need native performance or GPU
access. It does not remove the need for a native ABI for DSP and output plugins,
so it would be an addition, not a replacement.

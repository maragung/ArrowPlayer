#!/usr/bin/env python3
"""Hardening verification gate — spec §25.4, §25.6 (REQ-SEC-018).

REQ-SEC-018 lists the flags a release build must enable and then adds the clause
that makes it a gate rather than a preference:

    CI MUST verify the flags are present in the produced binaries, not merely in
    the build files.

That sentence is the whole reason this script reads ELF and PE headers instead of
grepping the CMake. `cmake/ArrowWarnings.cmake` defined `arrow_set_hardening`
for several commits and called it from nowhere: every flag was present in the
build files and absent from every binary. Reading the build files would have
reported success.

What is checked, and what each check actually observes:

  ELF (Linux)
    PIE           e_type is ET_DYN *and* DT_FLAGS_1 carries DF_1_PIE, which is
                  what distinguishes a position-independent executable from a
                  shared library — both are ET_DYN.
    RELRO         a PT_GNU_RELRO segment exists.
    BIND_NOW      DT_BIND_NOW, or DF_BIND_NOW in DT_FLAGS, or DF_1_NOW in
                  DT_FLAGS_1. RELRO without it is partial RELRO: the GOT stays
                  writable, which is the half an attacker wants.
    noexecstack   PT_GNU_STACK exists and does not carry PF_X. A *missing*
                  PT_GNU_STACK is a failure, not a pass — the kernel then falls
                  back to the ABI default, which on some architectures is
                  executable.
    stack guard   __stack_chk_fail appears in a symbol table.
    fortify       a fortified glibc entry point (`__*_chk`) appears.

  PE (Windows)
    DYNAMICBASE, NXCOMPAT, HIGHENTROPYVA, GUARD:CF   bits in DllCharacteristics.
    CETCOMPAT     the extended-DLL-characteristics debug directory entry
                  (IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS), bit 0x1.
    /GS           the load-config directory carries a non-zero SecurityCookie.
                  There is no header bit for /GS; the cookie is the artifact it
                  leaves behind, and its absence means the option did not apply.

Two checks are deliberately *set-wide* rather than per-binary, and saying why
matters more than the code:

  fortify   `-D_FORTIFY_SOURCE=2` only emits a `__*_chk` call where the compiler
            cannot prove the size at the call site. A binary that never makes a
            fortifiable call therefore shows no evidence either way, and failing
            it would be reporting a fact about the source, not about hardening.
            So it is advisory per binary and mandatory across the set: at least
            one binary must show fortified entry points, which is what proves the
            definition reached the compiler and had an effect.
  /GS       the same shape on Windows, for the same reason.

Every other check is per-binary and unconditional.

Static archives (.a, .lib) and object files are not scanned: PIE, RELRO,
BIND_NOW and stack permissions are properties a linker decides, and an archive
has not been linked. Skipping them is not leniency — the executables that link
them are checked, and that is where the properties become real.

Standard library only: no pip, no venv, and no readelf or dumpbin either. A gate
that needs a binutils build on the runner is a gate that reports the runner's
packaging rather than the artifact.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------- #
#  ELF constants
# --------------------------------------------------------------------------- #

ET_EXEC = 2
ET_DYN = 3

PT_DYNAMIC = 2
PT_INTERP = 3
PT_GNU_STACK = 0x6474E551
PT_GNU_RELRO = 0x6474E552

PF_X = 0x1

SHT_SYMTAB = 2
SHT_DYNSYM = 11

# A sanitizer or libFuzzer build is an analysis artifact, never a shipped one, and
# arrow_set_hardening deliberately no-ops under those presets: ASan's own
# allocator and redzones are what make the suite meaningful, and stacking release
# hardening on top would weaken the analysis to satisfy a flag check. So the gate
# recognises them and puts them out of scope instead of failing them — while
# refusing to report success if *every* binary it found was out of scope, because
# then it verified nothing. Presence of any of these symbols is the giveaway.
SANITIZER_SYMBOL_PREFIXES = ("__asan_", "__tsan_", "__msan_", "__ubsan_",
                             "__sanitizer_", "__lsan_", "__hwasan_",
                             "_ZN6fuzzer")

DT_NULL = 0
DT_BIND_NOW = 24
DT_FLAGS = 30
DT_FLAGS_1 = 0x6FFFFFFB

DF_BIND_NOW = 0x8
DF_1_NOW = 0x1
DF_1_PIE = 0x08000000

# --------------------------------------------------------------------------- #
#  PE constants
# --------------------------------------------------------------------------- #

PE32 = 0x10B
PE32PLUS = 0x20B

DLLCHAR_HIGH_ENTROPY_VA = 0x0020
DLLCHAR_DYNAMIC_BASE = 0x0040
DLLCHAR_NX_COMPAT = 0x0100
DLLCHAR_GUARD_CF = 0x4000

DEBUG_TYPE_EX_DLLCHARACTERISTICS = 20
DLLCHAR_EX_CET_COMPAT = 0x1

DIR_DEBUG = 6
DIR_LOAD_CONFIG = 10


# --------------------------------------------------------------------------- #
#  Result model
# --------------------------------------------------------------------------- #

OK = "ok"
MISSING = "missing"
UNOBSERVABLE = "unobservable"


@dataclass
class Report:
    path: Path
    kind: str                                    # 'elf' | 'pe'
    checks: dict[str, str] = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)
    analysis: bool = False                       # sanitizer or fuzzer build

    def failures(self, advisory: set[str]) -> list[str]:
        return [name for name, verdict in self.checks.items()
                if verdict == MISSING and name not in advisory]


# --------------------------------------------------------------------------- #
#  ELF
# --------------------------------------------------------------------------- #

def _u(data: bytes, offset: int, size: int, little: bool) -> int:
    fmt = {1: "B", 2: "H", 4: "I", 8: "Q"}[size]
    return struct.unpack_from(("<" if little else ">") + fmt, data, offset)[0]


def read_elf(blob: bytes, path: Path) -> Report | None:
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        return None

    is64 = blob[4] == 2
    little = blob[5] == 1
    rep = Report(path=path, kind="elf")

    def u(off: int, size: int) -> int:
        return _u(blob, off, size, little)

    e_type = u(16, 2)
    if is64:
        e_phoff, e_shoff = u(32, 8), u(40, 8)
        e_phentsize, e_phnum = u(54, 2), u(56, 2)
        e_shentsize, e_shnum = u(58, 2), u(60, 2)
    else:
        e_phoff, e_shoff = u(28, 4), u(32, 4)
        e_phentsize, e_phnum = u(42, 2), u(44, 2)
        e_shentsize, e_shnum = u(46, 2), u(48, 2)

    # ---- program headers -------------------------------------------------- #
    relro = False
    stack_seg: int | None = None
    dynamic: tuple[int, int] | None = None
    has_interp = False

    for i in range(e_phnum):
        base = e_phoff + i * e_phentsize
        if base + e_phentsize > len(blob):
            break
        p_type = u(base, 4)
        if is64:
            p_flags = u(base + 4, 4)
            p_offset, p_filesz = u(base + 8, 8), u(base + 32, 8)
        else:
            p_offset, p_filesz = u(base + 4, 4), u(base + 16, 4)
            p_flags = u(base + 24, 4)

        if p_type == PT_GNU_RELRO:
            relro = True
        elif p_type == PT_GNU_STACK:
            stack_seg = p_flags
        elif p_type == PT_DYNAMIC:
            dynamic = (p_offset, p_filesz)
        elif p_type == PT_INTERP:
            has_interp = True

    # ---- dynamic section -------------------------------------------------- #
    dt_flags = 0
    dt_flags_1 = 0
    dt_bind_now = False
    if dynamic is not None:
        step = 16 if is64 else 8
        off, size = dynamic
        for at in range(off, min(off + size, len(blob) - step + 1), step):
            tag = u(at, 8 if is64 else 4)
            val = u(at + (8 if is64 else 4), 8 if is64 else 4)
            if tag == DT_NULL:
                break
            if tag == DT_FLAGS:
                dt_flags = val
            elif tag == DT_FLAGS_1:
                dt_flags_1 = val
            elif tag == DT_BIND_NOW:
                dt_bind_now = True

    # ---- symbol names ----------------------------------------------------- #
    names = _elf_symbol_names(blob, is64, little, e_shoff, e_shentsize, e_shnum)

    # ---- verdicts --------------------------------------------------------- #
    #
    # A shared library is ET_DYN without DF_1_PIE and without a PT_INTERP. It is
    # position-independent by construction, so the PIE question does not apply to
    # it; asking it anyway would produce a failure with no remedy.
    is_shared_lib = e_type == ET_DYN and not has_interp and not (dt_flags_1 & DF_1_PIE)

    if is_shared_lib:
        rep.checks["PIE"] = OK
        rep.notes.append("shared object: position-independent by construction")
    elif e_type == ET_DYN and (dt_flags_1 & DF_1_PIE or has_interp):
        rep.checks["PIE"] = OK
    else:
        rep.checks["PIE"] = MISSING

    rep.checks["RELRO"] = OK if relro else MISSING
    rep.checks["BIND_NOW"] = (
        OK if (dt_bind_now or dt_flags & DF_BIND_NOW or dt_flags_1 & DF_1_NOW) else MISSING
    )

    if stack_seg is None:
        rep.checks["noexecstack"] = MISSING
        rep.notes.append("no PT_GNU_STACK: the kernel falls back to the ABI default")
    else:
        rep.checks["noexecstack"] = MISSING if stack_seg & PF_X else OK

    if names is None:
        rep.checks["stack-protector"] = UNOBSERVABLE
        rep.checks["fortify"] = UNOBSERVABLE
        rep.notes.append("stripped of every symbol table")
    else:
        rep.analysis = any(n.startswith(SANITIZER_SYMBOL_PREFIXES) for n in names)
        rep.checks["stack-protector"] = OK if "__stack_chk_fail" in names else MISSING

        fortified = sorted(n for n in names if n.startswith("__") and n.endswith("_chk")
                           and n != "__stack_chk_fail")
        rep.checks["fortify"] = OK if fortified else UNOBSERVABLE
        if fortified:
            rep.notes.append("fortified: " + ", ".join(fortified[:4])
                             + (" …" if len(fortified) > 4 else ""))
    return rep


def _elf_symbol_names(blob: bytes, is64: bool, little: bool,
                      e_shoff: int, e_shentsize: int, e_shnum: int) -> set[str] | None:
    """Names from every SHT_SYMTAB / SHT_DYNSYM section, or None if there is none.

    The symbol table is parsed rather than the string table searched. Searching
    .dynstr for a token would report a name that no symbol references — a
    difference that matters when the question is whether the linker wired
    __stack_chk_fail in, not whether the string appears somewhere in the file.
    """
    if e_shoff == 0 or e_shnum == 0:
        return None

    def u(off: int, size: int) -> int:
        return _u(blob, off, size, little)

    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        if base + e_shentsize > len(blob):
            return None
        if is64:
            sections.append({
                "type": u(base + 4, 4), "offset": u(base + 24, 8),
                "size": u(base + 32, 8), "link": u(base + 40, 4),
                "entsize": u(base + 56, 8),
            })
        else:
            sections.append({
                "type": u(base + 4, 4), "offset": u(base + 16, 4),
                "size": u(base + 20, 4), "link": u(base + 24, 4),
                "entsize": u(base + 36, 4),
            })

    names: set[str] = set()
    found_table = False
    for sec in sections:
        if sec["type"] not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        if sec["link"] >= len(sections):
            continue
        entsize = sec["entsize"] or (24 if is64 else 16)
        strtab = sections[sec["link"]]
        strbase, strsize = strtab["offset"], strtab["size"]
        if strbase + strsize > len(blob):
            continue
        found_table = True
        count = sec["size"] // entsize if entsize else 0
        for i in range(count):
            at = sec["offset"] + i * entsize
            if at + entsize > len(blob):
                break
            st_name = u(at, 4)
            if st_name == 0 or st_name >= strsize:
                continue
            end = blob.find(b"\x00", strbase + st_name, strbase + strsize)
            if end < 0:
                continue
            names.add(blob[strbase + st_name:end].decode("utf-8", "replace"))
    return names if found_table else None


# --------------------------------------------------------------------------- #
#  PE
# --------------------------------------------------------------------------- #

def read_pe(blob: bytes, path: Path) -> Report | None:
    if len(blob) < 0x40 or blob[:2] != b"MZ":
        return None
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if pe_off + 24 > len(blob) or blob[pe_off:pe_off + 4] != b"PE\0\0":
        return None

    coff = pe_off + 4
    nsections = struct.unpack_from("<H", blob, coff + 2)[0]
    opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
    opt = coff + 20
    if opt + 2 > len(blob):
        return None
    magic = struct.unpack_from("<H", blob, opt)[0]
    if magic == PE32PLUS:
        dllchar_off, dirs_off, cookie_off, wide = 70, 112, 88, True
    elif magic == PE32:
        dllchar_off, dirs_off, cookie_off, wide = 68, 96, 60, False
    else:
        return None

    rep = Report(path=path, kind="pe")
    dllchar = struct.unpack_from("<H", blob, opt + dllchar_off)[0]

    for name, bit in (("DYNAMICBASE", DLLCHAR_DYNAMIC_BASE),
                      ("NXCOMPAT", DLLCHAR_NX_COMPAT),
                      ("HIGHENTROPYVA", DLLCHAR_HIGH_ENTROPY_VA),
                      ("GUARD:CF", DLLCHAR_GUARD_CF)):
        rep.checks[name] = OK if dllchar & bit else MISSING

    # Sections, for RVA → file offset.
    sec_base = opt + opt_size
    sections = []
    for i in range(nsections):
        at = sec_base + i * 40
        if at + 40 > len(blob):
            break
        va, vsize = struct.unpack_from("<II", blob, at + 12)[0], struct.unpack_from("<I", blob, at + 8)[0]
        raw_size, raw_ptr = struct.unpack_from("<II", blob, at + 16)
        sections.append((va, max(vsize, raw_size), raw_ptr))

    def rva_to_off(rva: int) -> int | None:
        for va, size, ptr in sections:
            if va <= rva < va + size:
                return ptr + (rva - va)
        return None

    def data_dir(index: int) -> tuple[int, int]:
        at = opt + dirs_off + index * 8
        if at + 8 > len(blob):
            return (0, 0)
        return struct.unpack_from("<II", blob, at)

    # /GS — the security cookie in the load-config directory.
    lc_rva, lc_size = data_dir(DIR_LOAD_CONFIG)
    cookie = 0
    if lc_rva and lc_size > cookie_off:
        off = rva_to_off(lc_rva)
        if off is not None and off + cookie_off + (8 if wide else 4) <= len(blob):
            declared = struct.unpack_from("<I", blob, off)[0]
            if declared > cookie_off:
                cookie = struct.unpack_from("<Q" if wide else "<I", blob, off + cookie_off)[0]
    if cookie:
        rep.checks["/GS"] = OK
    elif lc_rva:
        rep.checks["/GS"] = MISSING
    else:
        rep.checks["/GS"] = UNOBSERVABLE
        rep.notes.append("no load-config directory, so no security cookie to read")

    # /CETCOMPAT — extended DLL characteristics, in the debug directory.
    dbg_rva, dbg_size = data_dir(DIR_DEBUG)
    cet = None
    if dbg_rva and dbg_size:
        off = rva_to_off(dbg_rva)
        if off is not None:
            for i in range(dbg_size // 28):
                at = off + i * 28
                if at + 28 > len(blob):
                    break
                entry_type = struct.unpack_from("<I", blob, at + 12)[0]
                size_of_data = struct.unpack_from("<I", blob, at + 16)[0]
                ptr = struct.unpack_from("<I", blob, at + 24)[0]
                if entry_type == DEBUG_TYPE_EX_DLLCHARACTERISTICS and size_of_data >= 4 \
                        and ptr + 4 <= len(blob):
                    cet = bool(struct.unpack_from("<I", blob, ptr)[0] & DLLCHAR_EX_CET_COMPAT)
                    break
    if cet is None:
        rep.checks["CETCOMPAT"] = MISSING
        rep.notes.append("no extended-DLL-characteristics debug entry")
    else:
        rep.checks["CETCOMPAT"] = OK if cet else MISSING
    return rep


# --------------------------------------------------------------------------- #
#  Collection
# --------------------------------------------------------------------------- #

SKIP_SUFFIXES = {".a", ".lib", ".o", ".obj", ".pdb", ".ilk", ".exp",
                 ".cmake", ".txt", ".json", ".ninja", ".py", ".cpp", ".hpp", ".h"}

# Vendored third-party binaries are not ours to harden and not what REQ-SEC-018
# governs. Named as path components so the rule reads the same on both platforms.
SKIP_COMPONENTS = {"_deps", "CMakeFiles", "Testing", "vcpkg_installed"}


def collect(roots: list[Path]) -> list[Path]:
    out: list[Path] = []
    for root in roots:
        if root.is_file():
            out.append(root)
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_COMPONENTS]
            for name in sorted(filenames):
                p = Path(dirpath) / name
                if p.suffix.lower() in SKIP_SUFFIXES:
                    continue
                if p.is_symlink() or not p.is_file():
                    continue
                out.append(p)
    return out


def inspect(path: Path) -> Report | None:
    try:
        with open(path, "rb") as fh:
            blob = fh.read()
    except OSError:
        return None
    return read_elf(blob, path) or read_pe(blob, path)


# --------------------------------------------------------------------------- #
#  Self-test — synthetic binaries, both directions
# --------------------------------------------------------------------------- #

def _elf64(*, e_type: int, relro: bool, stack: int | None, dt_flags: int,
           dt_flags_1: int, bind_now: bool, interp: bool,
           symbols: list[str] | None) -> bytes:
    """A minimal but structurally valid ELF64, built to order.

    Only what the reader looks at is real: the header, the program headers, the
    dynamic array, and one symbol table with its string table. Enough to drive
    every branch above, and small enough to read.
    """
    phdrs: list[bytes] = []

    def phdr(p_type: int, p_flags: int, p_offset: int, p_filesz: int) -> bytes:
        return struct.pack("<IIQQQQQQ", p_type, p_flags, p_offset, 0, 0,
                           p_filesz, p_filesz, 8)

    dyn = b""
    for tag, val in ([(DT_BIND_NOW, 0)] if bind_now else []) + \
                    [(DT_FLAGS, dt_flags), (DT_FLAGS_1, dt_flags_1), (DT_NULL, 0)]:
        dyn += struct.pack("<QQ", tag, val)

    # A genuinely stripped binary has no section header table, not an empty
    # symbol table — and the difference decides whether the reader answers
    # "unobservable" or "missing", so the builder has to reproduce it.
    strtab = b"\x00"
    symtab = b"\x00" * 24                      # index 0 is always the null symbol
    for name in (symbols or []):
        symtab += struct.pack("<IBBHQQ", len(strtab), 0x12, 0, 0, 0, 0)
        strtab += name.encode() + b"\x00"

    # Layout: header, phdrs, dynamic, symtab, strtab, shdrs.
    e_phnum = 1 + (1 if relro else 0) + (1 if stack is not None else 0) + (1 if interp else 0)
    e_phoff = 64
    dyn_off = e_phoff + e_phnum * 56
    sym_off = dyn_off + len(dyn)
    str_off = sym_off + len(symtab)
    sh_off = str_off + len(strtab)
    sections = symbols is not None

    if interp:
        phdrs.append(phdr(PT_INTERP, 0x4, 0, 0))
    if relro:
        phdrs.append(phdr(PT_GNU_RELRO, 0x4, 0, 0))
    if stack is not None:
        phdrs.append(phdr(PT_GNU_STACK, stack, 0, 0))
    phdrs.append(phdr(PT_DYNAMIC, 0x6, dyn_off, len(dyn)))

    shdrs = b""
    if sections:
        shdrs += b"\x00" * 64                   # SHN_UNDEF
        shdrs += struct.pack("<IIQQQQIIQQ", 1, SHT_DYNSYM, 0, 0, sym_off,
                             len(symtab), 2, 0, 8, 24)
        shdrs += struct.pack("<IIQQQQIIQQ", 9, 3, 0, 0, str_off, len(strtab),
                             0, 0, 1, 0)

    header = b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8
    header += struct.pack("<HHIQQQIHHHHHH", e_type, 0x3E, 1, 0, e_phoff,
                          sh_off if sections else 0, 0, 64, 56, len(phdrs),
                          64, 3 if sections else 0, 0)
    return header + b"".join(phdrs) + dyn + symtab + strtab + shdrs


def _pe64(*, dllchar: int, cookie: int | None, cet: bool | None) -> bytes:
    """A minimal PE32+ with one section, a load-config, and a debug directory."""
    # PointerToRawData has to be where the body actually lands in the file, and
    # the first draft put it at 0x400 while the headers were 0x200 long — every
    # RVA resolved 0x200 bytes early, which read as "no security cookie". A
    # synthetic binary that is malformed in the same direction as the failure it
    # is meant to detect proves nothing.
    sec_rva, sec_off = 0x1000, 0x400
    header_size = 0x400
    lc_rva, dbg_rva, cet_rva = sec_rva, sec_rva + 0x100, sec_rva + 0x200

    body = bytearray(0x400)
    # IMAGE_LOAD_CONFIG_DIRECTORY64: Size, then the cookie at offset 88.
    struct.pack_into("<I", body, 0, 0x140)
    if cookie is not None:
        struct.pack_into("<Q", body, 88, cookie)
    if cet is not None:
        # One debug directory entry of type 20, pointing at the flags word.
        struct.pack_into("<IIHHIII", body, 0x100,
                         0, 0, 0, 0, DEBUG_TYPE_EX_DLLCHARACTERISTICS, 4,
                         cet_rva)
        struct.pack_into("<I", body, 0x100 + 24, sec_off + 0x200)
        struct.pack_into("<I", body, 0x200, DLLCHAR_EX_CET_COMPAT if cet else 0)

    opt = bytearray(240)
    struct.pack_into("<H", opt, 0, PE32PLUS)
    struct.pack_into("<H", opt, 70, dllchar)
    struct.pack_into("<I", opt, 108, 16)                       # NumberOfRvaAndSizes
    struct.pack_into("<II", opt, 112 + DIR_LOAD_CONFIG * 8, lc_rva, 0x140)
    if cet is not None:
        struct.pack_into("<II", opt, 112 + DIR_DEBUG * 8, dbg_rva, 28)

    section = struct.pack("<8sIIIIIIHHI", b".rdata\0\0", 0x400, sec_rva,
                          0x400, sec_off, 0, 0, 0, 0, 0x40000040)
    coff = struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, len(opt), 0x22)

    head = bytearray(header_size)
    head[0:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, 0x80)
    head[0x80:0x84] = b"PE\0\0"
    head[0x84:0x84 + len(coff)] = coff
    head[0x84 + len(coff):0x84 + len(coff) + len(opt)] = opt
    at = 0x84 + len(coff) + len(opt)
    head[at:at + len(section)] = section
    return bytes(head) + bytes(body)


def self_test() -> int:
    failures: list[str] = []
    ran = 0
    negatives = 0

    def check(label: str, got, want, *, negative: bool = False) -> None:
        """`negative` marks an assertion that a planted defect *is* detected.

        Counted separately and reported, because "31 assertions pass" says
        nothing about whether any of them could ever fail — which is the exact
        criticism OQ-045 recorded against the four gates that then had no
        self-test at all.
        """
        nonlocal ran, negatives
        ran += 1
        if negative:
            negatives += 1
        if got != want:
            failures.append(f"{label}: expected {want!r}, got {got!r}")

    good = _elf64(e_type=ET_DYN, relro=True, stack=0x6, dt_flags=DF_BIND_NOW,
                  dt_flags_1=DF_1_NOW | DF_1_PIE, bind_now=False, interp=True,
                  symbols=["__stack_chk_fail", "__memcpy_chk"])
    rep = read_elf(good, Path("synthetic-good"))
    check("elf/good", rep and rep.checks, {
        "PIE": OK, "RELRO": OK, "BIND_NOW": OK, "noexecstack": OK,
        "stack-protector": OK, "fortify": OK})
    check("elf/good has no failures", rep.failures(set()), [])

    # Every property, denied one at a time. A gate is only as good as its
    # willingness to fail, and each of these has failed for a real binary
    # somewhere: partial RELRO is the default without -z now, and a missing
    # PT_GNU_STACK is what a hand-written assembly object produces.
    cases = [
        ("no PIE", dict(e_type=ET_EXEC, interp=True, dt_flags_1=DF_1_NOW), "PIE"),
        ("no RELRO", dict(relro=False), "RELRO"),
        ("partial RELRO", dict(dt_flags=0, dt_flags_1=DF_1_PIE, bind_now=False), "BIND_NOW"),
        ("exec stack", dict(stack=0x7), "noexecstack"),
        ("no GNU_STACK", dict(stack=None), "noexecstack"),
        ("no stack guard", dict(symbols=["memcpy"]), "stack-protector"),
    ]
    for label, override, expected in cases:
        kwargs = dict(e_type=ET_DYN, relro=True, stack=0x6, dt_flags=DF_BIND_NOW,
                      dt_flags_1=DF_1_NOW | DF_1_PIE, bind_now=False, interp=True,
                      symbols=["__stack_chk_fail", "__memcpy_chk"])
        kwargs.update(override)
        rep = read_elf(_elf64(**kwargs), Path(label))
        check(f"elf/{label}", rep and rep.checks.get(expected), MISSING, negative=True)
        check(f"elf/{label} fails the gate", expected in rep.failures(set()), True,
              negative=True)

    # A shared object is ET_DYN with no PT_INTERP and no DF_1_PIE. Asking it for
    # PIE would fail a file that cannot be anything else.
    so = read_elf(_elf64(e_type=ET_DYN, relro=True, stack=0x6, dt_flags=DF_BIND_NOW,
                         dt_flags_1=DF_1_NOW, bind_now=False, interp=False,
                         symbols=["__stack_chk_fail"]), Path("libx.so"))
    check("elf/shared library PIE", so.checks["PIE"], OK)

    stripped = read_elf(_elf64(e_type=ET_DYN, relro=True, stack=0x6,
                               dt_flags=DF_BIND_NOW, dt_flags_1=DF_1_NOW | DF_1_PIE,
                               bind_now=False, interp=True, symbols=None),
                        Path("stripped"))
    check("elf/stripped", stripped.checks["stack-protector"], UNOBSERVABLE)
    check("elf/stripped fortify", stripped.checks["fortify"], UNOBSERVABLE)

    # DT_BIND_NOW alone, with neither flag word set — the older spelling.
    old = read_elf(_elf64(e_type=ET_DYN, relro=True, stack=0x6, dt_flags=0,
                          dt_flags_1=DF_1_PIE, bind_now=True, interp=True,
                          symbols=["__stack_chk_fail"]), Path("old-bind-now"))
    check("elf/DT_BIND_NOW", old.checks["BIND_NOW"], OK)

    # A binary making no fortifiable call is unobservable, not failing.
    plain = read_elf(_elf64(e_type=ET_DYN, relro=True, stack=0x6, dt_flags=DF_BIND_NOW,
                            dt_flags_1=DF_1_NOW | DF_1_PIE, bind_now=False,
                            interp=True, symbols=["__stack_chk_fail"]), Path("plain"))
    check("elf/no fortifiable call", plain.checks["fortify"], UNOBSERVABLE)
    check("elf/unobservable is not a failure", plain.failures(set()), [])

    # ---- PE ---------------------------------------------------------------- #
    all_bits = (DLLCHAR_DYNAMIC_BASE | DLLCHAR_NX_COMPAT
                | DLLCHAR_HIGH_ENTROPY_VA | DLLCHAR_GUARD_CF)
    pe = read_pe(_pe64(dllchar=all_bits, cookie=0x140000000, cet=True), Path("good.exe"))
    check("pe/good", pe and pe.checks, {
        "DYNAMICBASE": OK, "NXCOMPAT": OK, "HIGHENTROPYVA": OK, "GUARD:CF": OK,
        "/GS": OK, "CETCOMPAT": OK})

    for label, bit, name in (("no DYNAMICBASE", DLLCHAR_DYNAMIC_BASE, "DYNAMICBASE"),
                             ("no NXCOMPAT", DLLCHAR_NX_COMPAT, "NXCOMPAT"),
                             ("no HIGHENTROPYVA", DLLCHAR_HIGH_ENTROPY_VA, "HIGHENTROPYVA"),
                             ("no GUARD:CF", DLLCHAR_GUARD_CF, "GUARD:CF")):
        rep = read_pe(_pe64(dllchar=all_bits & ~bit, cookie=1, cet=True), Path(label))
        check(f"pe/{label}", rep.checks[name], MISSING, negative=True)

    no_cookie = read_pe(_pe64(dllchar=all_bits, cookie=0, cet=True), Path("no-gs.exe"))
    check("pe/no security cookie", no_cookie.checks["/GS"], MISSING, negative=True)

    no_cet = read_pe(_pe64(dllchar=all_bits, cookie=1, cet=False), Path("no-cet.exe"))
    check("pe/CETCOMPAT clear", no_cet.checks["CETCOMPAT"], MISSING, negative=True)

    absent_cet = read_pe(_pe64(dllchar=all_bits, cookie=1, cet=None), Path("old.exe"))
    check("pe/no debug entry", absent_cet.checks["CETCOMPAT"], MISSING, negative=True)

    # Neither reader may claim a file of the other format, or a file of neither.
    check("elf reader rejects PE", read_elf(_pe64(dllchar=0, cookie=1, cet=True),
                                            Path("x")), None, negative=True)
    check("pe reader rejects ELF", read_pe(good, Path("x")), None, negative=True)
    check("readers reject a text file", inspect(Path(__file__)), None, negative=True)

    if failures:
        print(f"check-hardening self-test: {len(failures)} failure(s)", file=sys.stderr)
        for f in failures:
            print(f"  · {f}", file=sys.stderr)
        return 1
    # Counted, not asserted: a hand-written total is a number that goes stale the
    # first time a scenario is added, and this file exists because of a claim that
    # was true in the build files and false in the artifact.
    print(f"check-hardening self-test: ELF and PE readers behave as committed "
          f"({ran} assertions over synthetic binaries, {negatives} of them "
          f"planted defects that must be caught)")
    return 0


# --------------------------------------------------------------------------- #
#  Main
# --------------------------------------------------------------------------- #

# Per-binary advisory, set-wide mandatory. See the module docstring.
ADVISORY = {"fortify", "/GS"}
SET_WIDE = {"fortify", "/GS"}


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Verify REQ-SEC-018 hardening in the produced binaries.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="exit 0 every binary hardened · 1 a check failed · 2 nothing to check")
    ap.add_argument("paths", nargs="*", type=Path,
                    help="binaries or directories (default: build/linux-release)")
    ap.add_argument("--self-test", action="store_true",
                    help="check both readers against synthetic binaries and exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    roots = args.paths or [Path("build/linux-release")]
    missing_roots = [r for r in roots if not r.exists()]
    if missing_roots:
        print("no such path: " + ", ".join(str(r) for r in missing_roots), file=sys.stderr)
        return 2

    every = [r for r in (inspect(p) for p in collect(roots)) if r is not None]
    out_of_scope = [r for r in every if r.analysis]
    reports = [r for r in every if not r.analysis]

    for rep in sorted(out_of_scope, key=lambda r: str(r.path)):
        print(f"  --   {rep.path}  sanitizer/fuzzer build, out of scope for "
              f"REQ-SEC-018")

    if not reports:
        sys.stdout.flush()
        # The failure mode this exit code exists for: a wrong build directory
        # reports "nothing to check" as success, and the gate never runs again.
        # REQ-GEN-015's licence assertion had that bug for its whole life.
        if out_of_scope:
            print(f"::error::all {len(out_of_scope)} binaries under "
                  + ", ".join(str(r) for r in roots)
                  + " are sanitizer or fuzzer builds, which REQ-SEC-018 does not"
                  " govern.", file=sys.stderr)
            print("Point this at a release build directory — the artifacts that "
                  "ship are the\nones the requirement is about.", file=sys.stderr)
        else:
            print("::error::no ELF or PE binary was found under: "
                  + ", ".join(str(r) for r in roots), file=sys.stderr)
        print("A hardening gate that inspected nothing is not a passing gate.",
              file=sys.stderr)
        return 2

    width = max(len(str(r.path)) for r in reports)
    failed: list[tuple[Report, list[str]]] = []
    observed_set_wide: set[str] = set()

    for rep in sorted(reports, key=lambda r: str(r.path)):
        for name in SET_WIDE:
            if rep.checks.get(name) == OK:
                observed_set_wide.add(name)
        bad = rep.failures(ADVISORY)
        mark = "FAIL" if bad else "ok  "
        summary = " ".join(
            f"{name}={verdict}" for name, verdict in rep.checks.items()
            if verdict != OK) or "all checks pass"
        print(f"  {mark} {str(rep.path):<{width}}  {rep.kind}  {summary}")
        for note in rep.notes:
            print(f"       · {note}")
        if bad:
            failed.append((rep, bad))

    print()
    kinds = {r.kind for r in reports}
    for name in sorted(SET_WIDE):
        # A set-wide check only applies to a platform that is present. Demanding
        # /GS from a run that saw only ELF would fail Linux for a Windows flag.
        applies = ("pe" if name == "/GS" else "elf") in kinds
        if not applies:
            continue
        if name in observed_set_wide:
            print(f"  {name}: observed in at least one binary — the flag reached "
                  f"the compiler")
        else:
            print(f"::error::REQ-SEC-018: no binary in this set shows {name}.")
            print(f"  {name} is advisory per binary because a binary that makes no")
            print("  fortifiable call shows no evidence either way. Across a whole")
            print("  build it is not advisory: seeing it nowhere means the flag")
            print("  never reached the compiler.")
            failed.append((Report(path=Path("<set>"), kind="set"), [name]))

    if failed:
        print()
        sys.stdout.flush()
        print(f"::error::REQ-SEC-018: {len(failed)} binary/binaries missing "
              f"hardening the build claims to apply.", file=sys.stderr)
        for rep, bad in failed:
            print(f"  {rep.path}: {', '.join(bad)}", file=sys.stderr)
        print("\nThe flags live in cmake/ArrowWarnings.cmake "
              "(arrow_set_hardening). If a target is new, it needs the call —"
              "\ndo not relax this gate to match a target that does not have it.",
              file=sys.stderr)
        return 1

    print(f"hardening: {len(reports)} binary/binaries verified, "
          f"REQ-SEC-018 satisfied in the produced artifacts")
    return 0


if __name__ == "__main__":
    sys.exit(main())

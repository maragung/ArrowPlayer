#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate docs/THIRD-PARTY.md — spec §4.2 / §27 (REQ-GEN-012, REQ-GEN-075).

REQ-GEN-012 is the binding requirement: "docs/THIRD-PARTY.md MUST contain this
table, kept accurate, and CI MUST fail if a dependency appears in the build that
is absent from it." §27 (REQ-GEN-075) marks the document "Generated". The word
is normative, and it is why this is a generator and not a hand-written file: a
hand-written register drifts the moment a dependency changes, and "kept accurate"
only means something if a machine reproduces the document from data and a gate
fails when the two disagree.

The register lives as DATA in register.json (transcribed from §4.2); this script
emits the document from it. Nothing here is authored twice.

What this script does, and the exit code for each outcome:

  (default)          regenerate docs/THIRD-PARTY.md from register.json.
  --check            regenerate in memory and compare to the file on disk;
                     exit 1 if it is stale. This is the CI freshness gate.
  --resolved-graph F include the TRANSITIVE port set by parsing the output of
                     `vcpkg install --dry-run` in F. Without it, the document
                     states in its own text that only direct dependencies are
                     covered and how to regenerate with the graph (OQ-025).
  --self-test        exercise the graph parser and classifier against a committed
                     fixture, and confirm the manifest cross-check passes.

  exit 0  success (generated, or --check found the document fresh).
  exit 1  a gate failed: the document was stale (--check); or a dependency in
          desktop/vcpkg.json has no register entry (REQ-GEN-012); or a resolved
          graph contained a component described nowhere (REQ-GEN-012 / OQ-025);
          or a self-test assertion failed.
  exit 2  a usage or I/O error: a missing input file, or malformed register.json.

THE REQ-GEN-012 CROSS-CHECK. Before writing anything, every dependency named in
desktop/vcpkg.json (direct and per-feature) must map to a register entry. If one
does not, the script fails loudly and names REQ-GEN-012 rather than emitting a
document that misrepresents the build.

This is NOT tools/check-dependency-denylist.py and does not duplicate it. That
script asks a different question — whether a FORBIDDEN component (telemetry,
crash-reporting, attribution, advertising SDKs; REQ-SET-010) has entered the
graph. This script asks whether every component that HAS entered is described in
the register. Complementary gates: one guards against the unwanted, the other
against the unaccounted-for. Both take --resolved-graph, and for the same reason
(OQ-025): the manifest is the direct set, and only a resolved graph sees past it.

LICENCE TEXTS — the choice, and why. This generator emits POINTERS, not embedded
licence texts. The build machine has no network access, so the canonical texts
cannot be fetched; and embedding a dozen full texts (LGPL-3.0 alone is ~7,600
words) would bury the register they annotate. The verbatim texts already exist in
two mechanised places at package time: vcpkg materialises each port's exact text
at vcpkg_installed/<triplet>/share/<port>/copyright, and Qt's LGPL-3.0 text ships
to licenses/LGPL-3.0.txt per REQ-GEN-013(3). So every entry carries the SPDX id
(the canonical, machine-checkable licence identity), the SPDX text URL, and the
corresponding source URL, and the generated document says plainly that the full
texts are assembled at package time and fed to the Help -> Third-Party Licences
screen (REQ-GEN-019).

Standard library only, Python 3.11+: no pip, no venv, no network.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[1]
DEFAULT_REGISTER = SCRIPT_DIR / "register.json"
DEFAULT_OUTPUT = REPO / "docs" / "THIRD-PARTY.md"
DEFAULT_FIXTURE = SCRIPT_DIR / "testdata" / "vcpkg-dry-run.sample.txt"
VCPKG_MANIFEST = REPO / "desktop" / "vcpkg.json"


# ===========================================================================
#  Anchors — GitHub's heading-to-slug algorithm.
#
#  Reproduced verbatim from tools/check-doc-links.py so the table of contents
#  this generator emits and the fragments that gate validates agree by
#  construction. If that algorithm changes there, it changes here.
# ===========================================================================
def slug(heading: str) -> str:
    text = re.sub(r"<[^>]+>", "", heading)
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\[[^\]]*\]", r"\1", text)
    text = text.replace("`", "")
    text = re.sub(r"[*~]+", "", text)
    # Underscore emphasis is not intra-word in GFM (`gapless_info` keeps its
    # underscore); strip only leading/trailing runs, matching check-doc-links.py.
    text = re.sub(r"(?<!\w)_+|_+(?!\w)", "", text)
    out: list[str] = []
    for ch in text.strip().lower():
        if ch in " \t":
            out.append("-")
        elif ch in "-_":
            out.append(ch)
        elif unicodedata.category(ch)[0] in ("L", "N"):
            out.append(ch)
    return "".join(out)


# ===========================================================================
#  Loading and light validation of register.json
# ===========================================================================
class RegisterError(Exception):
    """register.json is missing or structurally wrong — an exit-2 condition."""


def load_register(path: Path) -> dict:
    if not path.exists():
        raise RegisterError(f"{rel(path)} does not exist")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RegisterError(f"{rel(path)} is not valid JSON: {exc}") from exc

    for key in ("platforms", "transitive_reference", "build_only_ports"):
        if key not in data:
            raise RegisterError(f"{rel(path)} is missing the '{key}' key")
    for platform in ("desktop", "android"):
        if platform not in data["platforms"]:
            raise RegisterError(f"{rel(path)} platforms is missing '{platform}'")
    for entry in data["platforms"]["desktop"]["entries"]:
        for field in ("name", "spdx", "version", "linkage", "obligation", "source_url", "vcpkg"):
            if field not in entry:
                raise RegisterError(
                    f"desktop entry {entry.get('name', '?')!r} is missing '{field}'"
                )
    return data


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return str(path)


# ===========================================================================
#  Input 1 — the vcpkg manifest (the direct dependency set)
# ===========================================================================
def manifest_ports(path: Path) -> list[str]:
    """Every port named in desktop/vcpkg.json, direct and per-feature.

    Mirrors the collection in check-dependency-denylist.py so the two gates read
    the manifest identically. Qt is intentionally absent from the manifest
    (aqtinstall, ADR 0005), so it is never expected here.
    """
    if not path.exists():
        raise RegisterError(f"{rel(path)} does not exist")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    names: list[str] = []

    def add(dep: object) -> None:
        if isinstance(dep, str):
            names.append(dep)
        elif isinstance(dep, dict) and "name" in dep:
            names.append(str(dep["name"]))

    for dep in manifest.get("dependencies", []):
        add(dep)
    for feature in manifest.get("features", {}).values():
        if isinstance(feature, dict):
            for dep in feature.get("dependencies", []):
                add(dep)
    return sorted(set(names))


# ===========================================================================
#  Input 2 — the resolved graph (the transitive set), OQ-025
# ===========================================================================
#  A `vcpkg install --dry-run` package line, in the two forms the tool has used:
#      name[feat,feat]:triplet@version#portversion      (current)
#      name[feat,feat]:triplet -> version#portversion    (older)
#  The leading `*` marks a package pulled in to complete the operation. We do NOT
#  rely on it to tell direct from transitive — that is decided against the
#  register, which is more reliable than trusting the marker — but we accept it.
#  Header and footer lines ("The following packages will be built and installed:",
#  "Additional packages (*) will be modified …", "Detecting compiler hash …") do
#  not have the `name:triplet` shape and so are ignored without special-casing.
GRAPH_LINE = re.compile(
    r"""^\s*\*?\s*
        (?P<name>[A-Za-z0-9][A-Za-z0-9._+-]*)
        (?:\[(?P<features>[^\]]*)\])?
        :(?P<triplet>[A-Za-z0-9._+-]+)
        (?:\s*(?:@|->)\s*(?P<version>[^\s#]+)(?:\#(?P<portver>[0-9]+))?)?
        (?:\s.*)?$
    """,
    re.VERBOSE,
)


class ResolvedPort:
    __slots__ = ("name", "features", "triplet", "version", "port_version")

    def __init__(self, name, features, triplet, version, port_version):
        self.name = name
        self.features = features
        self.triplet = triplet
        self.version = version
        self.port_version = port_version

    def version_display(self) -> str:
        if not self.version:
            return "(unversioned)"
        return f"{self.version}#{self.port_version}" if self.port_version else self.version


def parse_resolved_graph(text: str) -> list[ResolvedPort]:
    """Parse dry-run text into ports, de-duplicated by name (first spelling wins).

    NOTE ON VERIFICATION: this parser is written against the documented dry-run
    format and is exercised by --self-test against a hand-constructed fixture. It
    has NOT been run against real `vcpkg install --dry-run` output, because vcpkg
    is not installed on the build machine. Treat the format as documented-but-
    unconfirmed until a CI run pipes a real graph through it.
    """
    seen: dict[str, ResolvedPort] = {}
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        m = GRAPH_LINE.match(raw)
        if not m:
            continue
        name = m.group("name")
        if name in seen:
            continue
        seen[name] = ResolvedPort(
            name=name,
            features=[f.strip() for f in (m.group("features") or "").split(",") if f.strip()],
            triplet=m.group("triplet"),
            version=m.group("version"),
            port_version=m.group("portver"),
        )
    return [seen[k] for k in sorted(seen)]


# ===========================================================================
#  Classification and the REQ-GEN-012 cross-checks
# ===========================================================================
def register_port_index(register: dict) -> dict[str, dict]:
    """vcpkg port name -> desktop register entry, for every entry that has one."""
    index: dict[str, dict] = {}
    for entry in register["platforms"]["desktop"]["entries"]:
        for port in entry.get("vcpkg", []):
            index[port] = entry
    return index


def crosscheck_manifest(register: dict, ports: list[str]) -> list[str]:
    """REQ-GEN-012: every manifest port must map to a register entry.

    Returns a list of human-readable failures; empty means the gate passes.
    """
    index = register_port_index(register)
    return [
        f"desktop/vcpkg.json requires port '{port}', which no §4.2 register entry "
        f"in {rel(DEFAULT_REGISTER)} describes"
        for port in ports
        if port not in index
    ]


def classify_graph(register: dict, ports: list[ResolvedPort]) -> dict[str, list]:
    """Split resolved ports into direct / transitive / build-only / unknown.

    An 'unknown' port is one described by no register entry, no transitive
    reference, and no build-only entry: a component that entered the build that
    nobody has looked at. That is exactly the REQ-GEN-012 failure, extended to
    the transitive set per OQ-025.
    """
    direct_index = register_port_index(register)
    transitive_index = {p["name"]: p for p in register["transitive_reference"]["ports"]}
    buildonly_index = {p["name"]: p for p in register["build_only_ports"]["ports"]}

    buckets: dict[str, list] = {"direct": [], "transitive": [], "build_only": [], "unknown": []}
    for port in ports:
        if port.name in direct_index:
            buckets["direct"].append((port, direct_index[port.name]))
        elif port.name in transitive_index:
            buckets["transitive"].append((port, transitive_index[port.name]))
        elif port.name in buildonly_index:
            buckets["build_only"].append((port, buildonly_index[port.name]))
        else:
            buckets["unknown"].append(port)
    return buckets


# ===========================================================================
#  Rendering — deterministic Markdown (no timestamps: --check must be stable)
# ===========================================================================
def esc(cell: str) -> str:
    return cell.replace("|", "\\|")


def table(headers: list[str], rows: list[list[str]]) -> list[str]:
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        out.append("| " + " | ".join(esc(c) for c in row) + " |")
    return out


def link(url: str) -> str:
    """A visible, clickable source URL. The URL is external (https), so
    tools/check-doc-links.py skips it; showing it in full keeps the register
    auditable rather than hiding the source location behind link text."""
    return f"[{url}]({url})"


def render(register: dict, mode: str, graph: dict | None, graph_source: str | None) -> str:
    spdx_tpl = register.get("spdx_url_template", "https://spdx.org/licenses/{id}.html")
    desktop = register["platforms"]["desktop"]["entries"]

    # -- section headings, in order. The TOC is built from these, so anchors match.
    H_GEN = "How this document is generated"
    H_DESK = "Desktop dependencies — the §4.2 register"
    H_QT = "Qt — exact version, configuration, and source"
    H_FFMPEG = "FFmpeg — the LGPL configuration"
    H_TRANS = "Transitive dependencies"
    H_PATENT = "Codec patent notes"
    H_TEXTS = "Licence texts and the source offer"
    H_ANDROID = "Android dependencies — listed for completeness, not in this build"
    H_TRADE = "Trademark and asset hygiene"
    sections = [H_GEN, H_DESK, H_QT, H_FFMPEG, H_TRANS, H_PATENT, H_TEXTS, H_ANDROID, H_TRADE]

    L: list[str] = []
    L.append("# Third-Party Licences — Eclipse Player")
    L.append("")
    L.append(
        "`eclipse-player.md` §27 requires this document: *Generated; the §4.2 "
        "register with SPDX ids, versions, licence texts, and source URLs*. This is "
        "that document. It is **generated** by `tools/gen-third-party/gen-third-party.py` "
        "from `tools/gen-third-party/register.json` and MUST NOT be edited by hand — "
        "fix the generator or the register, never the output (`REQ-GEN-012`)."
    )
    L.append("")
    L.append(
        "Scope: the **desktop** build. The Android half of the §4.2 register is listed "
        "at the end for completeness and is **not in the current build** — the Android "
        "app is deferred (see [ADR 0011](adr/0011-desktop-first-sequencing.md)). §0.1 "
        "rule 2 forbids silently downgrading a requirement, so those entries are kept "
        "and marked, not dropped."
    )
    L.append("")

    # -- generation-mode banner (honest about what produced this document)
    if mode == "resolved-graph":
        L.append(
            f"> **Generation mode: resolved-graph.** The transitive dependency set below "
            f"was produced from a resolved graph (`{graph_source}`), so this document "
            f"covers both the direct §4.2 register and the transitive ports the build "
            f"pulls in (`REQ-GEN-012` as narrowed by OQ-025)."
        )
    else:
        L.append(
            "> **Generation mode: direct-only.** This document was generated **without** "
            "a resolved dependency graph, so it covers the **direct** dependencies of "
            "§4.2 and does **not** enumerate the transitive set. This is the honest "
            "degraded mode described in the open-questions log under OQ-025 — see the "
            "*Transitive dependencies* section for how to regenerate with the full graph."
        )
    L.append("")

    # -- table of contents (same-file anchors, validated by check-doc-links.py)
    for heading in sections:
        L.append(f"- [{heading}](#{slug(heading)})")
    L.append("")

    # ---------------------------------------------------------------- §gen
    L.append(f"## {H_GEN}")
    L.append("")
    L.append(
        "The §4.2 register is held as data in `tools/gen-third-party/register.json`, "
        "transcribed from the specification. This document is emitted from it. The "
        "reasoning is `REQ-GEN-012`'s own: a register *“kept accurate”* is only "
        "meaningful if a machine can reproduce it and a gate fails when the two diverge."
    )
    L.append("")
    L.append("Three checks run in the generator, each naming the requirement it enforces:")
    L.append("")
    L.append(
        "1. **Cross-check against `desktop/vcpkg.json` (`REQ-GEN-012`).** Every port the "
        "manifest asks for — direct and per-feature — must map to a register entry. If "
        "one does not, the generator refuses to write and fails, rather than emit a "
        "document that misrepresents the build."
    )
    L.append(
        "2. **Freshness (`--check`).** CI regenerates the document in memory and compares "
        "it to the committed file; a stale document fails the build. §25.5 step 8 and "
        "§25.6 both require the release to regenerate this document and fail if it is "
        "stale."
    )
    L.append(
        "3. **Transitive coverage (`--resolved-graph`, OQ-025).** Given the output of "
        "`vcpkg install --dry-run`, every resolved port must be described by the register, "
        "the transitive reference, or the build-only list; a port described nowhere is a "
        "component nobody has looked at, and fails the gate."
    )
    L.append("")
    L.append(
        "This is a different question from the one `tools/check-dependency-denylist.py` "
        "answers. That gate looks for components that must be **absent** (telemetry, "
        "crash-reporting, attribution, advertising; `REQ-SET-010`); this one checks that "
        "every component that is **present** is accounted for. The two are complementary "
        "and are not duplicated."
    )
    L.append("")
    L.append(
        "**Licence texts are referenced, not embedded.** Each entry carries its SPDX "
        "identifier and a link to the canonical SPDX text, plus the corresponding source "
        "URL. The verbatim texts are materialised at package time — vcpkg writes each "
        "port's exact text to `vcpkg_installed/<triplet>/share/<port>/copyright`, and Qt's "
        "full LGPL-3.0 text ships to `licenses/LGPL-3.0.txt` (`REQ-GEN-013`(3)). The "
        "Help → Third-Party Licences screen is generated from this register at build "
        "time and shows the full texts in the application (`REQ-GEN-019`); it is never "
        "hand-maintained in a second place."
    )
    L.append("")

    # ---------------------------------------------------------------- §desktop table
    L.append(f"## {H_DESK}")
    L.append("")
    L.append(
        "The register of **direct** dependencies, transcribed from §4.2 and kept in "
        "`register.json`. “Linkage” and “Obligation” are the "
        "specification's own columns; “SPDX id” and the exact “Version” "
        "are what §27 additionally requires here."
    )
    L.append("")
    rows = []
    for e in desktop:
        spdx = e["spdx"]
        spdx_cell = f"`{spdx}`"
        rows.append([
            e["name"],
            spdx_cell,
            e["version"],
            e["linkage"],
            e["obligation"],
            link(e["source_url"]),
        ])
    L.extend(table(
        ["Component", "SPDX id", "Version", "Linkage", "Obligation", "Source"],
        rows,
    ))
    L.append("")
    L.append(
        "SPDX texts: each identifier above resolves at "
        f"`{spdx_tpl.replace('{id}', '<id>')}`. Where the displayed licence differs "
        "from a bare SPDX id, the reason is in the notes below."
    )
    L.append("")
    L.append("### Per-dependency notes")
    L.append("")
    for e in desktop:
        display = e.get("licence_display", e["spdx"])
        head = f"- **{e['name']}** — {display}."
        if e.get("acquisition"):
            head += f" {e['acquisition']}"
        L.append(head)
        for note in e.get("notes", []):
            L.append(f"  - {note}")
    L.append("")

    # ---------------------------------------------------------------- §Qt
    qt = next((e for e in desktop if e["name"].startswith("Qt")), None)
    L.append(f"## {H_QT}")
    L.append("")
    if qt:
        L.append(
            f"`REQ-GEN-013`(4) requires this document to state the exact Qt version and "
            f"the configure flags used, and to link the corresponding source archive."
        )
        L.append("")
        L.append(f"- **Exact version:** {qt['version']} (pinned in `desktop/qt-version.txt`; "
                 f"the register names the {qt.get('register_version', '')} series).")
        L.append(
            "- **Configuration:** official prebuilt **shared** libraries obtained via "
            "`aqtinstall`, not built from source and not from vcpkg (ADR 0005 / "
            "`REQ-BLD-001`). `desktop/vcpkg.json` MUST NOT list Qt. Qt is therefore "
            "**dynamically linked, always** — static Qt is forbidden in every shipped "
            "artifact (`REQ-GEN-013`(1))."
        )
        L.append(
            "- **Relinking duty (`REQ-GEN-013`(2),(5)):** the user MUST be able to replace "
            "a Qt shared library with a compatible build and still run Eclipse Player. No "
            "checksums or signature checks are applied over Qt binaries, and the "
            "application is not shipped in a form that prevents installing a modified Qt."
        )
        L.append(
            "- **Licence text (`REQ-GEN-013`(3)):** the full LGPL-3.0 text ships in the "
            "installed tree at `licenses/LGPL-3.0.txt` and is reachable from "
            "Help → Licences."
        )
        L.append(f"- **Corresponding source:** {link(qt['source_url'])}")
        L.append("")
        L.append(
            "The precise per-release source archive, keyed by release tag, is recorded in "
            "the LGPL source-offer document `docs/LGPL-SOURCE-OFFER.md` (`REQ-GEN-020`)."
        )
        L.append("")

    # ---------------------------------------------------------------- §FFmpeg
    ff = next((e for e in desktop if e["name"].startswith("FFmpeg")), None)
    L.append(f"## {H_FFMPEG}")
    L.append("")
    if ff:
        L.append(
            "`REQ-GEN-014` fixes the FFmpeg configuration for shipped artifacts, and "
            "`REQ-GEN-015` asserts it mechanically at build time. FFmpeg can be built LGPL "
            "or GPL depending on flags; a single missing flag would be a licence violation "
            "invisible in the source tree (ADR 0006). The shipped configuration is:"
        )
        L.append("")
        L.append("```text")
        L.extend(ff.get("configure_flags", []))
        L.append("```")
        L.append("")
        L.append(
            "`REQ-GEN-015`: CI verifies at build time that the linked FFmpeg reports "
            "`LGPL` and neither `GPL version` nor `nonfree` via `avutil_license()`, and "
            "fails the build otherwise. This is a mechanical guard, not a review step — a "
            "configure flag recorded in a document is a flag that eventually goes wrong; a "
            "test that fails the build is a flag that stays right."
        )
        L.append("")
        L.append(f"- **Pinned version:** {ff['version']} (override in `desktop/vcpkg.json`).")
        L.append(f"- **Corresponding source:** {link(ff['source_url'])}")
        L.append(
            "- **v1.0 decodes only.** Encoders are enabled selectively and only for "
            "LGPL-clean codecs when the converter lands (`REQ-GEN-016`); `libfdk_aac` is "
            "permanently excluded as non-free."
        )
        L.append("")

    # ---------------------------------------------------------------- §transitive
    L.append(f"## {H_TRANS}")
    L.append("")
    if mode == "resolved-graph" and graph is not None:
        L.append(
            f"Resolved from `{graph_source}`. Every port below is classified against the "
            f"register: **direct** ports are §4.2 entries; **transitive** ports are pulled "
            f"in by a direct one; **build-only** ports are vcpkg's own host tooling and are "
            f"never shipped. A port that matched none of these would fail the gate "
            f"(`REQ-GEN-012` / OQ-025)."
        )
        L.append("")

        direct_rows = [
            [p.name, f"`{entry['spdx']}`", p.version_display(), "direct (§4.2)"]
            for p, entry in graph["direct"]
        ]
        trans_rows = [
            [p.name, f"`{ref['spdx']}`", p.version_display(),
             f"transitive, via `{ref.get('pulled_in_by', '?')}`"]
            for p, ref in graph["transitive"]
        ]
        build_rows = [
            [p.name, f"`{ref['spdx']}`", p.version_display(), "build-only, not shipped"]
            for p, ref in graph["build_only"]
        ]
        allrows = sorted(direct_rows + trans_rows + build_rows, key=lambda r: r[0])
        L.extend(table(["Port", "SPDX id", "Version", "Role"], allrows))
        L.append("")
        L.append(
            f"Counts: {len(direct_rows)} direct, {len(trans_rows)} transitive, "
            f"{len(build_rows)} build-only. Transitive licence identifiers are recorded "
            f"from **{register['transitive_reference'].get('source', 'the transitive reference')}** "
            f"and are not re-verified by this generation."
        )
        L.append("")
    else:
        L.append(
            "This document was generated **without** a resolved graph, so the transitive "
            "set is **not enumerated here**. This is deliberate and honest: `vcpkg` is not "
            "installed on the machine that produced this document, so no real "
            "`vcpkg install --dry-run` output was available, and inventing one would be "
            "worse than omitting it."
        )
        L.append("")
        L.append("To regenerate with the transitive set covered, once vcpkg is available:")
        L.append("")
        L.append("```sh")
        L.append("vcpkg install --dry-run --triplet x64-linux-eclipse > /tmp/graph.txt")
        L.append("python3 tools/gen-third-party/gen-third-party.py --resolved-graph /tmp/graph.txt")
        L.append("```")
        L.append("")
        src = register["transitive_reference"].get("source", "the open-questions log")
        L.append(
            f"OQ-025 in the open-questions log ([{rel_link('OPEN-QUESTIONS.md')}]"
            f"(OPEN-QUESTIONS.md)) records the design: §4.2 is the register of **direct** "
            f"dependencies, and this document additionally carries the transitive set when "
            f"a graph is supplied, so the CI gate compares the resolved graph against this "
            f"document rather than against §4.2 alone. {src} also records that one resolved "
            f"graph for `x64-linux-eclipse` pulled in six transitive components "
            f"(`utfcpp`, `glm`, `projectm-eval`, and the OpenGL/EGL registries), all "
            f"licence-compatible, and that `libzip`'s bzip2/OpenSSL default features were "
            f"removed rather than registered."
        )
        L.append("")

    # ---------------------------------------------------------------- §patent
    L.append(f"## {H_PATENT}")
    L.append("")
    patent = register.get("patent_notes", {})
    L.append(
        f"`{patent.get('requirement', 'REQ-GEN-017')}` requires this document to record "
        f"the following."
    )
    L.append("")
    for item in patent.get("items", []):
        L.append(f"- {item}")
    L.append("")

    # ---------------------------------------------------------------- §texts
    L.append(f"## {H_TEXTS}")
    L.append("")
    L.append(
        "As stated above, licence texts are **referenced, not embedded** in this "
        "document. Each SPDX identifier resolves to its canonical text at "
        f"`{spdx_tpl.replace('{id}', '<id>')}`, and the exact per-port text is written by "
        "vcpkg to `vcpkg_installed/<triplet>/share/<port>/copyright` at install time. The "
        "in-application Help → Third-Party Licences screen is generated from this "
        "register and carries the full texts (`REQ-GEN-019`)."
    )
    L.append("")
    lgpl = sorted(
        e["name"] for e in desktop
        if "LGPL" in e["spdx"] and e["linkage"] != "Test-only"
    )
    L.append(
        "**Source offer.** The components under an LGPL arm carry a source-offer "
        "obligation: " + ", ".join(f"`{n.split(' (')[0]}`" for n in lgpl) + ". The "
        "written source offer — the precise source archive for every LGPL component in "
        "every release, keyed by release tag — is published in the companion document "
        "`docs/LGPL-SOURCE-OFFER.md` (`REQ-GEN-020`), and each release regenerates it "
        "(§25.5 step 8)."
    )
    L.append("")

    # ---------------------------------------------------------------- §android
    android = register["platforms"]["android"]
    L.append(f"## {H_ANDROID}")
    L.append("")
    L.append(f"> {android.get('shipped_note', '')}")
    L.append("")
    arows = [
        [e["name"], f"`{e['spdx']}`", e.get("note", ""), link(e["source_url"])]
        for e in android["entries"]
    ]
    L.extend(table(["Dependency", "SPDX id", "Note", "Source"], arows))
    L.append("")

    # ---------------------------------------------------------------- §trademark
    trade = register.get("trademark_notes", {})
    L.append(f"## {H_TRADE}")
    L.append("")
    for item in trade.get("items", []):
        L.append(f"- {item}")
    L.append("")

    # ---------------------------------------------------------------- footer
    L.append("---")
    L.append("")
    L.append(
        "Generated by `tools/gen-third-party/gen-third-party.py` from "
        "`tools/gen-third-party/register.json`. To change the register, edit that file "
        "and regenerate; to verify freshness in CI, run the generator with `--check`. No "
        "timestamp is written, so the output is deterministic and `--check` is stable."
    )
    L.append("")
    return "\n".join(L)


def rel_link(name: str) -> str:
    """Human label for an internal link — just the filename, kept out of slug math."""
    return name


# ===========================================================================
#  Self-test — the parser and classifier against a committed fixture
# ===========================================================================
ARROW_SAMPLE = """\
The following packages will be built and installed:
    taglib:x64-linux-eclipse -> 2.0.2
  * utfcpp:x64-linux-eclipse -> 4.0.6#1
    zlib:x64-linux-eclipse
"""


def self_test(register: dict) -> int:
    failures: list[str] = []

    # 1. the fixture must exist and parse to the expected NAME set (versions in the
    #    fixture are hand-invented, so they are deliberately not asserted).
    if not DEFAULT_FIXTURE.exists():
        print(f"FATAL: fixture {rel(DEFAULT_FIXTURE)} is missing", file=sys.stderr)
        return 2
    ports = parse_resolved_graph(DEFAULT_FIXTURE.read_text(encoding="utf-8"))
    names = {p.name for p in ports}
    expected = {
        "chromaprint", "ffmpeg", "gtest", "libsamplerate", "libzip", "projectm",
        "soundtouch", "sqlite3", "taglib", "zlib",           # 10 direct
        "egl-registry", "glm", "opengl", "opengl-registry",
        "projectm-eval", "utfcpp",                            # 6 transitive
        "pkgconf", "vcpkg-cmake", "vcpkg-cmake-config",       # 3 build-only
    }
    if names != expected:
        failures.append(f"fixture parse: unexpected port set\n    missing={sorted(expected - names)}"
                        f"\n    extra={sorted(names - expected)}")

    # 2. feature and version parsing on a couple of specific lines.
    by_name = {p.name: p for p in ports}
    if "ffmpeg" in by_name:
        ff = by_name["ffmpeg"]
        if set(ff.features) != {"avcodec", "avformat", "core", "swresample", "zlib"}:
            failures.append(f"fixture parse: ffmpeg features wrong: {ff.features}")
        if ff.version != "7.1.2" or ff.port_version != "5":
            failures.append(f"fixture parse: ffmpeg version wrong: {ff.version_display()}")

    # 3. the older `->` arrow form, and a line with no version, must also parse.
    arrow = {p.name: p for p in parse_resolved_graph(ARROW_SAMPLE)}
    if set(arrow) != {"taglib", "utfcpp", "zlib"}:
        failures.append(f"arrow-form parse: got {sorted(arrow)}")
    elif arrow["taglib"].version != "2.0.2" or arrow["utfcpp"].port_version != "1":
        failures.append("arrow-form parse: version/port-version not extracted")
    elif arrow["zlib"].version is not None:
        failures.append("arrow-form parse: unversioned line should have version None")

    # 4. classification of the fixture: all known, nothing unknown.
    buckets = classify_graph(register, ports)
    got = {k: len(v) for k, v in buckets.items()}
    if got != {"direct": 10, "transitive": 6, "build_only": 3, "unknown": 0}:
        failures.append(f"classification counts wrong: {got}")

    # 5. the gate must FIRE: an unregistered component is flagged as unknown. OpenSSL
    #    is the concrete example from OQ-025 (no §4.2 row; refused, not registered).
    injected = parse_resolved_graph(
        DEFAULT_FIXTURE.read_text(encoding="utf-8")
        + "\n    openssl:x64-linux-eclipse@3.3.2\n"
    )
    unknown = [p.name for p in classify_graph(register, injected)["unknown"]]
    if unknown != ["openssl"]:
        failures.append(f"gate did not flag the injected unknown component: unknown={unknown}")

    # 6. the manifest cross-check must PASS on the real desktop/vcpkg.json.
    try:
        problems = crosscheck_manifest(register, manifest_ports(VCPKG_MANIFEST))
    except RegisterError as exc:
        problems = [str(exc)]
    if problems:
        failures.append("manifest cross-check failed on the real tree:\n    "
                        + "\n    ".join(problems))

    if failures:
        print(f"{len(failures)} self-test failure(s):\n", file=sys.stderr)
        for f in failures:
            print(f"  ✗ {f}", file=sys.stderr)
        return 1
    print(
        "gen-third-party self-test: fixture parses to 19 ports "
        "(10 direct, 6 transitive, 3 build-only); arrow-form and unversioned lines "
        "parse; the unknown-component gate fires on an injected port; and the "
        "manifest cross-check passes on desktop/vcpkg.json."
    )
    print(
        "  NOTE: the dry-run parser is tested only against this hand-built fixture; "
        "it has not been run against real vcpkg output (vcpkg is not installed here)."
    )
    return 0


# ===========================================================================
#  main
# ===========================================================================
def run_gates_and_render(register, args) -> tuple[str | None, int]:
    """Return (rendered_document, exit_code). document is None on a gate failure."""
    # -- REQ-GEN-012 cross-check first, in every mode.
    try:
        ports = manifest_ports(VCPKG_MANIFEST)
    except RegisterError as exc:
        print(f"FATAL: {exc}", file=sys.stderr)
        return None, 2
    problems = crosscheck_manifest(register, ports)
    if problems:
        print(f"{len(problems)} unregistered dependenc(ies) — REQ-GEN-012:\n", file=sys.stderr)
        for p in problems:
            print(f"  ✗ {p}", file=sys.stderr)
        print(
            "\nREQ-GEN-012: docs/THIRD-PARTY.md must contain every dependency in the "
            "build.\nAdd the missing port to tools/gen-third-party/register.json (with "
            "its SPDX id,\nversion, linkage, obligation and source URL) and regenerate.",
            file=sys.stderr,
        )
        return None, 1

    # -- resolved graph, if supplied.
    mode = "direct-only"
    graph = None
    graph_source = None
    if args.resolved_graph:
        if not args.resolved_graph.exists():
            print(f"FATAL: {args.resolved_graph} does not exist", file=sys.stderr)
            return None, 2
        parsed = parse_resolved_graph(args.resolved_graph.read_text(encoding="utf-8"))
        graph = classify_graph(register, parsed)
        graph_source = args.resolved_graph.name
        mode = "resolved-graph"
        if graph["unknown"]:
            names = ", ".join(sorted(p.name for p in graph["unknown"]))
            print(
                f"{len(graph['unknown'])} unaccounted-for component(s) in the resolved "
                f"graph — REQ-GEN-012 / OQ-025:\n", file=sys.stderr,
            )
            print(f"  ✗ {names}", file=sys.stderr)
            print(
                "\nThese ports are in the resolved graph but described by no register "
                "entry,\nno transitive reference, and no build-only entry. Add each to "
                "register.json\n(as a direct entry, a transitive_reference port, or a "
                "build_only_port) and\nregenerate. A component nobody has looked at is "
                "exactly what REQ-GEN-012 forbids.",
                file=sys.stderr,
            )
            return None, 1

    return render(register, mode, graph, graph_source), 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate docs/THIRD-PARTY.md from the §4.2 register (REQ-GEN-012).",
    )
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and fail (exit 1) if the file on disk is stale")
    parser.add_argument("--resolved-graph", type=Path, metavar="FILE",
                        help="output of `vcpkg install --dry-run`, to cover the transitive set (OQ-025)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"document to write/check (default: {rel(DEFAULT_OUTPUT)})")
    parser.add_argument("--register", type=Path, default=DEFAULT_REGISTER,
                        help=f"register data (default: {rel(DEFAULT_REGISTER)})")
    parser.add_argument("--self-test", action="store_true",
                        help="test the graph parser and classifier against the committed fixture")
    args = parser.parse_args()

    try:
        register = load_register(args.register)
    except RegisterError as exc:
        print(f"FATAL: {exc}", file=sys.stderr)
        return 2

    if args.self_test:
        return self_test(register)

    content, code = run_gates_and_render(register, args)
    if content is None:
        return code

    if args.check:
        if not args.output.exists():
            print(f"{rel(args.output)} is MISSING — run the generator to create it "
                  f"(REQ-GEN-012, §27).", file=sys.stderr)
            return 1
        current = args.output.read_text(encoding="utf-8")
        if current != content:
            print(
                f"{rel(args.output)} is STALE — REQ-GEN-012.\n\n"
                f"It does not match what the generator produces from "
                f"{rel(args.register)}.\nRegenerate and commit it:\n"
                f"  python3 tools/gen-third-party/gen-third-party.py"
                + (f" --resolved-graph {args.resolved_graph}" if args.resolved_graph else ""),
                file=sys.stderr,
            )
            return 1
        print(f"docs/THIRD-PARTY.md is up to date ({len(content.splitlines())} lines, "
              f"mode: {'resolved-graph' if args.resolved_graph else 'direct-only'}).")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    mode = "resolved-graph" if args.resolved_graph else "direct-only"
    print(f"wrote {rel(args.output)} ({len(content.splitlines())} lines, mode: {mode}).")
    if mode == "direct-only":
        print("  NOT covered here: the transitive port set. Pass --resolved-graph with "
              "the\n  output of `vcpkg install --dry-run` to include it (OQ-025).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

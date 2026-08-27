#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate the two licence documents — spec §4.2 / §27 (REQ-GEN-012, REQ-GEN-020).

REQ-GEN-012 is the binding requirement: "docs/THIRD-PARTY.md MUST contain this
table, kept accurate, and CI MUST fail if a dependency appears in the build that
is absent from it." §27 (REQ-GEN-075) marks the document "Generated". The word
is normative, and it is why this is a generator and not a hand-written file: a
hand-written register drifts the moment a dependency changes, and "kept accurate"
only means something if a machine reproduces the document from data and a gate
fails when the two disagree.

The register lives as DATA in register.json (transcribed from §4.2); this script
emits the document from it. Nothing here is authored twice.

TWO DOCUMENTS, ONE REGISTER. REQ-GEN-020 asks for a second page — the written LGPL
source offer, "linking the precise source archive for every LGPL component in
every release, keyed by release tag" — and §25.5 step 8 makes the release pipeline
regenerate it and fail if it is stale. That is the same verb and the same failure
mode as REQ-GEN-012, over a subset of the same facts, so it is the same generator:

  --document third-party   docs/THIRD-PARTY.md      (default)
  --document source-offer  docs/LGPL-SOURCE-OFFER.md

Versions, SPDX ids and source URLs would otherwise be written down twice and drift
apart, and a source offer that disagrees with the register about which version
shipped is worse than no offer at all. The offer adds one input of its own:
releases.json, holding the offer's contact facts and the per-tag ledger.

THE REQ-GEN-020 LEDGER GATE. Every tag in releases.json must account for every
LGPL component in the register — either with an upstream archive and its SHA-256,
or with `"shipped": false` and a reason, because a component behind a disabled
build feature was never distributed and carries no obligation for that release.
Silence about a component is refused. So is a tag that is not vX.Y.Z, a SHA-256
that is not 64 hex digits, a component that matches no register entry, and any
recorded release at all while offer.postal_address is null: at that point the
written offer names no channel that outlives the platform hosting it (OQ-013).

What this script does, and the exit code for each outcome:

  (default)          regenerate the selected document from register.json (and, for
                     the source offer, releases.json).
  --check            regenerate in memory and compare to the file on disk;
                     exit 1 if it is stale. This is the CI freshness gate.
  --resolved-graph F include the TRANSITIVE port set by parsing the output of
                     `vcpkg install --dry-run` in F. Without it, the document
                     states in its own text that only direct dependencies are
                     covered and how to regenerate with the graph (OQ-025).
                     Third-party document only.
  --self-test        exercise the graph parser and classifier against a committed
                     fixture, confirm the manifest cross-check passes, and confirm
                     the ledger gate fires on every malformed release row.

  exit 0  success (generated, or --check found the document fresh).
  exit 1  a gate failed: the document was stale (--check); or a dependency in
          desktop/vcpkg.json has no register entry (REQ-GEN-012); or a resolved
          graph contained a component described nowhere (REQ-GEN-012 / OQ-025);
          or a release row does not account for an LGPL component (REQ-GEN-020);
          or a self-test assertion failed.
  exit 2  a usage or I/O error: a missing input file, or malformed register.json
          or releases.json.

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
DEFAULT_OFFER_OUTPUT = REPO / "docs" / "LGPL-SOURCE-OFFER.md"
DEFAULT_RELEASES = SCRIPT_DIR / "releases.json"
DEFAULT_FIXTURE = SCRIPT_DIR / "testdata" / "vcpkg-dry-run.sample.txt"
VCPKG_MANIFEST = REPO / "desktop" / "vcpkg.json"

# The two documents this generator owns, and where each is written. Both come off
# the same register, which is the point: REQ-GEN-012's table and REQ-GEN-020's
# source offer must never disagree about a version or a source URL, and the only
# way to guarantee that is to stop writing the facts down twice.
DOCUMENTS = {
    "third-party": DEFAULT_OUTPUT,
    "source-offer": DEFAULT_OFFER_OUTPUT,
}


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


# ===========================================================================
#  Input 2 — the per-release LGPL source ledger (releases.json)
#
#  REQ-GEN-020 wants "the precise source archive for every LGPL component in
#  every release, keyed by release tag". That is per-tag data, not register data:
#  the register says what the current tree pins, the ledger says what a shipped
#  binary was actually built against. Keeping them in separate files keeps each
#  one answerable to its own gate.
# ===========================================================================
TAG_RE = re.compile(r"^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-[0-9A-Za-z.-]+)?$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

OFFER_FIELDS = (
    "repository_url", "releases_url", "maintainer_handle",
    "maintainer_profile_url", "validity_years",
)


def lgpl_entries(register: dict) -> list[dict]:
    """Register entries with an LGPL arm — exactly the source-offer obligation set.

    TagLib is `LGPL-2.1-or-later OR MPL-1.1`, so a substring test includes it. That
    is correct: taking the LGPL arm is what creates the duty, and §4.2's own
    obligation column says "Source offer if the LGPL arm is chosen".
    """
    return [
        entry for entry in register["platforms"]["desktop"]["entries"]
        if "LGPL" in entry["spdx"]
    ]


def load_releases(path: Path) -> dict:
    if not path.exists():
        raise RegisterError(f"{rel(path)} does not exist")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RegisterError(f"{rel(path)} is not valid JSON: {exc}") from exc

    for key in ("offer", "releases"):
        if key not in data:
            raise RegisterError(f"{rel(path)} is missing the '{key}' key")
    if not isinstance(data["releases"], list):
        raise RegisterError(f"{rel(path)} 'releases' must be a list")
    for field in OFFER_FIELDS:
        if not data["offer"].get(field):
            raise RegisterError(f"{rel(path)} offer is missing '{field}'")
    return data


def ledger_summary(path: Path) -> list[dict]:
    """The releases the ledger records — for the CLI's own count line only.

    The gate loads and validates the ledger properly before anything is rendered;
    this exists so the success message can say how many tags are covered without
    threading the parsed ledger back out to main().
    """
    try:
        return load_releases(path)["releases"]
    except RegisterError:
        return []


def check_ledger(register: dict, ledger: dict) -> list[str]:
    """Every published tag must account for every LGPL component. Returns problems.

    "Account for" is deliberately not the same as "link an archive": a component
    behind a build feature that a release did not enable was never distributed and
    carries no obligation for that release. Such a row says so with
    `"shipped": false` and a reason. What is refused is silence — a component that
    the ledger neither links nor explains.
    """
    problems: list[str] = []
    obliged = {entry["name"] for entry in lgpl_entries(register)}
    known = {entry["name"] for entry in register["platforms"]["desktop"]["entries"]}
    seen_tags: set[str] = set()

    for index, release in enumerate(ledger["releases"]):
        where = f"releases[{index}]"
        tag = release.get("tag")
        if not isinstance(tag, str) or not TAG_RE.match(tag):
            problems.append(f"{where}: tag {tag!r} is not a vX.Y.Z release tag")
        elif tag in seen_tags:
            problems.append(f"{where}: duplicate tag {tag}")
        else:
            seen_tags.add(tag)
            where = f"{where} ({tag})"

        if not DATE_RE.match(str(release.get("date", ""))):
            problems.append(f"{where}: date {release.get('date')!r} is not YYYY-MM-DD")

        components = release.get("components")
        if not isinstance(components, list) or not components:
            problems.append(f"{where}: 'components' must be a non-empty list")
            continue

        named: set[str] = set()
        for component in components:
            name = component.get("name")
            if name not in known:
                problems.append(
                    f"{where}: component {name!r} matches no register entry "
                    f"(REQ-GEN-012)"
                )
                continue
            named.add(name)
            if component.get("shipped") is False:
                if not component.get("reason"):
                    problems.append(
                        f"{where}: {name} is marked not shipped with no 'reason'"
                    )
                continue
            for field in ("spdx", "version", "upstream_url", "sha256"):
                if not component.get(field):
                    problems.append(f"{where}: {name} is missing '{field}'")
            sha = component.get("sha256")
            if sha and not SHA256_RE.match(str(sha)):
                problems.append(
                    f"{where}: {name} sha256 {sha!r} is not 64 lowercase hex digits"
                )

        missing = sorted(obliged - named)
        if missing:
            problems.append(
                f"{where}: no source archive and no not-shipped reason for "
                f"{', '.join(missing)} (REQ-GEN-020)"
            )

    if ledger["releases"] and not ledger["offer"].get("postal_address"):
        problems.append(
            "offer.postal_address is null while the ledger records published "
            "releases: a written offer that names no reachable recipient channel is "
            "not an offer (REQ-GEN-020, OQ-013)"
        )
    return problems


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
        "at the end for completeness and is **not in the current Android build** — the "
        "app exists as a Phase 0 scaffold, but the NDK-level components below are not "
        "in it yet, and the Gradle version catalog is reconciled with this half of the "
        "register when they arrive (ADR 0012, OQ-018). §0.1 rule 2 forbids silently "
        "downgrading a requirement, so those entries are kept and marked, not dropped."
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


# ===========================================================================
#  Rendering — docs/LGPL-SOURCE-OFFER.md (REQ-GEN-020)
# ===========================================================================
def manifest_pins(path: Path) -> dict:
    """The two facts that make a vcpkg build reproducible: baseline and overrides.

    A source offer that says "get it from vcpkg" without the baseline commit is
    not an offer for *this* source; the baseline is what makes the recipe exact.
    """
    data = json.loads(path.read_text(encoding="utf-8"))
    return {
        "baseline": data.get("builtin-baseline"),
        "overrides": data.get("overrides", []),
        "triplet": "x64-linux-eclipse",
    }


# Small numbers read as words in prose. The licence says "three years", not "3
# years", and a document quoting a licence should not paraphrase its arithmetic.
NUMBER_WORDS = {
    1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven",
    8: "eight", 9: "nine", 10: "ten",
}


def spell(number: int) -> str:
    return NUMBER_WORDS.get(number, str(number))


def render_source_offer(register: dict, ledger: dict, pins: dict) -> str:
    spdx_tpl = register.get("spdx_url_template", "https://spdx.org/licenses/{id}.html")
    offer = ledger["offer"]
    releases = ledger["releases"]
    obliged = lgpl_entries(register)

    H_OFFER = "The offer"
    H_WHO = "Which components carry the obligation"
    H_CLAUSE = "Which clause each obligation is satisfied under"
    H_QT = "Qt 6 — the right to relink, not merely the source"
    H_FFMPEG = "FFmpeg — the configuration is part of the corresponding source"
    H_TAGLIB = "TagLib — a dual licence, and which arm we take"
    H_LEDGER = "Per-release ledger, keyed by tag"
    H_HOW = "How to obtain the corresponding source today"
    H_NOT = "What is not yet in place"
    H_GEN = "How this document is generated"
    H_SEE = "See also"
    sections = [H_OFFER, H_WHO, H_CLAUSE, H_QT, H_FFMPEG, H_TAGLIB, H_LEDGER,
                H_HOW, H_NOT, H_GEN, H_SEE]

    L: list[str] = []
    L.append("# LGPL Source Offer — Eclipse Player")
    L.append("")
    L.append(
        "`eclipse-player.md` §27 requires this document, and `REQ-GEN-020` states it "
        "verbatim:"
    )
    L.append("")
    L.append(
        "> `REQ-GEN-020` `[v1.0]` The project MUST publish a **written source offer** "
        "page (`docs/LGPL-SOURCE-OFFER.md`, mirrored on the website) linking the "
        "precise source archive for every LGPL component in every release, keyed by "
        "release tag. This mirrors AIMP's published practice and is the cheapest way "
        "to be unambiguously compliant."
    )
    L.append("")
    L.append(
        "This is that page. It is **generated** by "
        "`tools/gen-third-party/gen-third-party.py` from "
        "`tools/gen-third-party/register.json` (the §4.2 licence register) and "
        "`tools/gen-third-party/releases.json` (the per-tag ledger), and MUST NOT be "
        "edited by hand — fix the data or the generator, never the output. The "
        "companion document [THIRD-PARTY.md](THIRD-PARTY.md) is generated from the "
        "same register, so the two cannot disagree about a version or a source URL."
    )
    L.append("")

    if releases:
        L.append(
            f"> **Status: {len(releases)} published release(s)** — the per-tag ledger "
            f"below is the operative record."
        )
    else:
        L.append(
            "> **Status: no release has been published.** The per-tag ledger below is "
            "therefore empty, and deliberately so: an entry in it asserts that a binary "
            "was distributed and that the listed archives correspond to that binary. "
            "The offer text, the component list, and the clause mapping are all in "
            "force *now* and apply to the first release the moment it exists."
        )
    L.append("")

    for heading in sections:
        L.append(f"- [{heading}](#{slug(heading)})")
    L.append("")

    # ------------------------------------------------------------------ offer
    L.append(f"## {H_OFFER}")
    L.append("")
    L.append("For every binary release of Eclipse Player published at")
    L.append(f"{link(offer['releases_url'])}:")
    L.append("")
    L.append(
        "1. **The complete corresponding source code for every LGPL component that "
        "release ships is available from the same place as the binary, at no charge.** "
        "Each release lists, per component, the exact upstream archive it was built "
        "from and that archive's SHA-256; the ledger below records them permanently, "
        "keyed by tag."
    )
    L.append(
        f"2. **In addition, and as a written offer valid for at least "
        f"{spell(offer['validity_years'])} years from the date of that release**, the "
        f"same source is available on request to the maintainer, {offer['charge']}."
    )
    L.append(
        "3. **Nothing in a shipped artifact prevents replacing an LGPL library with "
        "your own build of it.** No checksum, signature, or integrity check is applied "
        "to any LGPL shared library, and none will be (`REQ-GEN-013`(2),(5))."
    )
    L.append("")
    L.append("**The request channel, stated exactly as it is.** Today the only channel "
             "the project can honestly promise to read is the maintainer's GitHub "
             f"profile, {offer['maintainer_handle']} "
             f"({link(offer['maintainer_profile_url'])}), and the repository's issue "
             f"tracker at {link(offer['repository_url'])}.")
    L.append("")
    gap = offer.get("postal_address_gap", "OQ-013")
    if not offer.get("postal_address"):
        L.append(
            f"There is **no published postal address, no monitored mailbox, and no "
            f"published PGP key** ({gap} in [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md)). "
            f"That is a real limitation of routes 2 and 3 above and is recorded rather "
            f"than papered over: a written offer whose only contact path is a platform "
            f"account is an offer that a platform can revoke. It is why route 1 — "
            f"equivalent access from the same place as the binary — is the primary "
            f"compliance route here, and why {gap} is a 1.0.0 release blocker."
        )
    else:
        L.append(
            f"Written requests: {offer['postal_address']}. Electronic requests: "
            f"{offer.get('mailbox') or offer['maintainer_profile_url']}."
        )
    L.append("")
    L.append(
        "This document is a compliance record, not legal advice. Where it names a "
        "licence clause it names the clause the project relies on, so that a reader "
        "can check the reasoning against the licence text rather than take it on trust."
    )
    L.append("")

    # -------------------------------------------------------------------- who
    L.append(f"## {H_WHO}")
    L.append("")
    L.append(
        f"{spell(len(obliged)).capitalize()} of the components in the §4.2 register "
        f"are distributed under an LGPL arm and therefore carry a source obligation. "
        f"Versions below are what "
        f"**this tree pins today**; what a *release* shipped is in the ledger."
    )
    L.append("")
    rows = []
    for entry in obliged:
        # A dual-licensed id is shown unlinked: pointing `A OR B` at the text of A
        # alone would quietly assert the choice in a table cell. TagLib's arm is
        # chosen explicitly in its own section below.
        spdx = entry["spdx"]
        spdx_cell = (
            f"`{spdx}`" if " OR " in spdx
            else f"[`{spdx}`]({spdx_tpl.format(id=spdx)})"
        )
        rows.append([
            entry["name"], spdx_cell, entry["linkage"], entry["version"],
            link(entry["source_url"]),
        ])
    L.extend(table(
        ["Component", "SPDX id", "Linkage", "Pinned version", "Upstream source"], rows,
    ))
    L.append("")
    L.append(
        "Every one of them is **dynamically linked**. That is not incidental: dynamic "
        "linking is itself one of the two ways the LGPL lets a combined work be "
        "distributed (LGPL-2.1 §6(b), LGPL-3.0 §4(d)(1)), and it is what makes the "
        "relinking promise in route 3 of the offer true in practice rather than in "
        "principle."
    )
    L.append("")
    L.append(
        "Three of them sit behind a build feature — SoundTouch (`tempo`), Chromaprint "
        "(`fingerprint`), projectM (`visualizer`) — all on by default. A release built "
        "with one of those features off does not distribute that component, and its "
        "ledger row says so explicitly with a reason, rather than linking an archive "
        "for something nobody received."
    )
    L.append("")
    L.append(
        "The remaining register entries (BSD, MIT, zlib, public-domain, and the "
        "test-only ones) carry attribution duties, not source duties, and are covered "
        "in [THIRD-PARTY.md](THIRD-PARTY.md)."
    )
    L.append("")

    # ----------------------------------------------------------------- clause
    L.append(f"## {H_CLAUSE}")
    L.append("")
    lgpl21 = [e["name"] for e in obliged if e["spdx"].startswith("LGPL-2.1")]
    lgpl3 = [e["name"] for e in obliged if e["spdx"].startswith("LGPL-3.0")]
    L.extend(table(
        ["Licence", "Components", "How the combined work is distributed",
         "How the source is provided"],
        [
            [
                "`LGPL-2.1-or-later`",
                ", ".join(lgpl21),
                "§6(b) — a suitable shared library mechanism: the library is loaded at "
                "run time from a copy on the user's system and works with a modified, "
                "interface-compatible build.",
                "§6(d) — equivalent access from the same designated place as the "
                "binary; §6(c) — a written offer valid for at least three years, as "
                "the fallback.",
            ],
            [
                "`LGPL-3.0-only`",
                ", ".join(lgpl3),
                "§4(d)(1) — the same shared library mechanism, plus §4(b),(c) notices "
                "and the full licence text at `licenses/LGPL-3.0.txt`.",
                "GPL-3.0 §6(d) as incorporated — source from the same place; "
                "GPL-3.0 §6(b) — a written offer valid for at least three years, as "
                "the fallback.",
            ],
        ],
    ))
    L.append("")
    L.append(
        "Two consequences of that table worth stating outright. First, because every "
        "LGPL component is a shared library and the source sits beside the binary, the "
        "project does not need the written offer to be compliant — it publishes one "
        "anyway, which is exactly what `REQ-GEN-020` calls the cheapest way to be "
        "unambiguously compliant. Second, the shared-library route imposes a duty on "
        "the *build*, not on this document: ship the libraries dynamically, keep them "
        "replaceable, and never gate them behind an integrity check. A source offer "
        "cannot rescue a statically linked artifact."
    )
    L.append("")

    # --------------------------------------------------------------------- Qt
    qt = next((e for e in obliged if e["name"].startswith("Qt")), None)
    L.append(f"## {H_QT}")
    L.append("")
    if qt:
        L.append(
            f"Qt is `{qt['spdx']}` — the strictest obligation in the tree, and the one "
            f"most easily broken by a packaging shortcut. `REQ-GEN-013` fixes five "
            f"rules, all five of which are conditions on the shipped artifact:"
        )
        L.append("")
        L.append("1. Qt libraries are **dynamically linked**. Static Qt is forbidden in "
                 "every shipped artifact.")
        L.append("2. The user can **replace a Qt shared library** with a compatible "
                 "build and Eclipse Player still runs. No hard-coded checksums or "
                 "signature checks over Qt binaries.")
        L.append("3. The full LGPL-3.0 text ships at `licenses/LGPL-3.0.txt` in the "
                 "installed tree and is reachable from **Help → Licences**.")
        L.append("4. This document and [THIRD-PARTY.md](THIRD-PARTY.md) state the exact "
                 "Qt version and configuration and link the corresponding source.")
        L.append("5. **No anti-tivoization conflict:** the application is not shipped in "
                 "a form that prevents installing a modified Qt.")
        L.append("")
        L.append(
            f"- **Exact version:** {qt['version']} (pinned in `desktop/qt-version.txt`; "
            f"the register names the {qt.get('register_version', qt['version'])} series)."
        )
        L.append(
            "- **Configuration:** the official prebuilt **shared** libraries obtained "
            "via `aqtinstall`, not built from source and not from vcpkg "
            "([ADR 0005](adr/0005-qt-acquisition.md) / `REQ-BLD-001`). No Qt configure "
            "flags of ours are involved, because we do not configure Qt — which is "
            "itself the fact `REQ-GEN-013`(4) needs recorded."
        )
        L.append(f"- **Corresponding source:** {link(qt['source_url'])}")
        L.append("")
        L.append(
            "Relinking is testable, and that is the point of stating it here: unpack a "
            "release, replace the bundled `libQt6Core` with your own interface-"
            "compatible build, and the player must start. If it ever does not, that is "
            "a licence defect, not a packaging preference, and it should be reported "
            "the same way a crash would be."
        )
    L.append("")

    # ----------------------------------------------------------------- FFmpeg
    ff = next((e for e in obliged if e["name"].startswith("FFmpeg")), None)
    L.append(f"## {H_FFMPEG}")
    L.append("")
    if ff:
        L.append(
            "FFmpeg can be built LGPL or GPL from the same tree — the licence is a "
            "function of the configure line. So the corresponding source for FFmpeg is "
            "not just the tarball: it is the tarball **plus** the configuration, and "
            "both are recorded here "
            "([ADR 0006](adr/0006-ffmpeg-lgpl.md), `REQ-GEN-014`)."
        )
        L.append("")
        L.append("```text")
        for flag in ff.get("configure_flags", []):
            L.append(flag)
        L.append("```")
        L.append("")
        L.append(f"- **Pinned version:** {ff['version']}.")
        L.append(f"- **Corresponding source:** {link(ff['source_url'])}")
        L.append(
            "- **Assertion, not assurance:** `REQ-GEN-015` requires CI to verify at "
            "build time that the linked FFmpeg reports `LGPL` and neither `GPL version` "
            "nor `nonfree` via `avutil_license()`, and to fail the build otherwise. A "
            "configure flag recorded in a document is a flag that eventually goes wrong; "
            "a test that fails the build is a flag that stays right. Stated exactly, "
            "because the difference matters for an offer: the step is wired in "
            "`desktop-ci.yml`, and no build has yet linked FFmpeg for it to assert "
            "against — the adapter arrives in Phase 1. Until then CI fails if an FFmpeg "
            "adapter appears without a registered licence case, which is the state this "
            "document would otherwise be describing optimistically (OQ-042)."
        )
        L.append(
            "- **Decode only in 1.0.** Encoders are enabled selectively and only for "
            "LGPL-clean codecs when the converter lands (`REQ-GEN-016`); `libfdk_aac` "
            "is permanently excluded as non-free."
        )
    L.append("")

    # ----------------------------------------------------------------- TagLib
    tl = next((e for e in obliged if e["name"] == "TagLib"), None)
    L.append(f"## {H_TAGLIB}")
    L.append("")
    if tl:
        L.append(
            f"TagLib is offered under `{tl['spdx']}` — the recipient of *TagLib* may "
            f"choose either arm. A distributor of a work that links it must say which "
            f"arm it relies on, because the two impose different duties, and §4.2's own "
            f"obligation column makes the offer conditional: “{tl['obligation']}”"
        )
        L.append("")
        L.append(
            "**Eclipse Player takes the `LGPL-2.1-or-later` arm.** The source-offer "
            "obligation therefore applies to TagLib exactly as it does to FFmpeg, and "
            "TagLib appears in the component table above and in every ledger row."
        )
        L.append("")
        L.append(
            "The reason is uniformity rather than preference. Four other components "
            "(FFmpeg, SoundTouch, Chromaprint, projectM) are already LGPL-2.1-or-later, "
            "so taking that arm for TagLib means the project has **one** obligation "
            "model to satisfy and audit — dynamic linking plus source availability — "
            "instead of two. MPL-1.1 would add a second, older set of terms with its "
            "own source-disclosure and jurisdiction language, for no gain: we ship "
            "TagLib unmodified and dynamically linked, which the LGPL arm already "
            "permits outright."
        )
    L.append("")

    # ----------------------------------------------------------------- ledger
    L.append(f"## {H_LEDGER}")
    L.append("")
    L.append(
        "`REQ-GEN-020` asks for the precise archive **per release tag**, not merely per "
        "component: a user holding a 1.2.0 binary needs the source that binary was "
        "built from, not whatever the tree pins later. This is that record."
    )
    L.append("")
    if releases:
        for release in releases:
            L.append(f"### {release['tag']} — {release['date']}")
            L.append("")
            if release.get("notes"):
                L.append(release["notes"])
                L.append("")
            rows = []
            for component in release["components"]:
                if component.get("shipped") is False:
                    rows.append([
                        component["name"], "—", "not shipped",
                        component.get("reason", ""), "—",
                    ])
                    continue
                rows.append([
                    component["name"],
                    f"`{component['spdx']}`",
                    component["version"],
                    link(component["upstream_url"]),
                    f"`{component['sha256']}`",
                ])
            L.extend(table(
                ["Component", "SPDX id", "Version", "Source archive", "SHA-256"], rows,
            ))
            L.append("")
    else:
        L.append(
            "**No release has been published, so there is no row here yet.** The "
            "current version in `desktop/version.txt` is a pre-release working version; "
            "no tag exists, no binary has been distributed, and therefore no source "
            "obligation has yet attached to anything. Inventing a row would be the one "
            "failure mode this document exists to prevent — a link that looks "
            "authoritative and corresponds to nothing."
        )
        L.append("")
        L.append(
            "One entry looks like this, and is appended by the release pipeline "
            "(§25.5 step 8) rather than by hand:"
        )
        L.append("")
        L.append("```json")
        L.append("{")
        L.append('  "tag": "v1.0.0",')
        L.append('  "date": "2026-01-31",')
        L.append('  "components": [')
        L.append("    {")
        L.append('      "name": "FFmpeg (libavformat, libavcodec, libavutil, '
                 'libswresample)",')
        L.append('      "spdx": "LGPL-2.1-or-later",')
        L.append('      "version": "7.1.2",')
        L.append('      "upstream_url": "https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz",')
        L.append('      "sha256": "<64 hex digits of the archive actually used>"')
        L.append("    },")
        L.append("    {")
        L.append('      "name": "projectM",')
        L.append('      "shipped": false,')
        L.append('      "reason": "built without the vcpkg \'visualizer\' feature; '
                 'the library is not in the artifact"')
        L.append("    }")
        L.append("  ]")
        L.append("}")
        L.append("```")
        L.append("")
        L.append(
            "The generator refuses to emit this document if a release row omits an "
            "LGPL component without giving a reason, if a recorded SHA-256 is not 64 "
            "hex digits, if a tag is not `vX.Y.Z`, or if a component names no register "
            "entry (`REQ-GEN-012`). It also refuses if any release is recorded while "
            "`offer.postal_address` is still null, because at that point the written "
            "offer names no channel that survives the platform hosting it."
        )
    L.append("")

    # -------------------------------------------------------------------- how
    L.append(f"## {H_HOW}")
    L.append("")
    L.append(
        "Until a release exists, the components above can be obtained exactly as the "
        "build obtains them. Two routes, both exact:"
    )
    L.append("")
    L.append("**1. The upstream archives** — the URLs in the component table. These are "
             "the unmodified upstream releases; Qt and FFmpeg are pinned to a specific "
             "archive, the rest to a version series that the vcpkg baseline resolves "
             "precisely.")
    L.append("")
    L.append("**2. The vcpkg recipe** — for every component except Qt, this is the "
             "authoritative one, because it names the patches as well as the source:")
    L.append("")
    L.append("```sh")
    L.append('git -C "$VCPKG_ROOT" fetch --all')
    L.append(f'git -C "$VCPKG_ROOT" checkout {pins["baseline"]}')
    L.append("cd desktop")
    L.append(f'vcpkg install --triplet {pins["triplet"]} '
             '--x-install-root=vcpkg_installed')
    L.append("```")
    L.append("")
    L.append(
        f"- The baseline commit `{pins['baseline']}` in `desktop/vcpkg.json` is what "
        f"fixes every port version; without it \"from vcpkg\" would name a moving "
        f"target rather than this source."
    )
    if pins["overrides"]:
        pretty = ", ".join(
            "`{} {}#{}`".format(
                override["name"], override.get("version", ""),
                override.get("port-version", 0),
            )
            for override in pins["overrides"]
        )
        L.append(f"- Version overrides in force: {pretty}.")
    L.append(
        "- After the install, `buildtrees/<port>/src/` holds the extracted upstream "
        "source with the port's patches applied — the complete corresponding source in "
        "the licence's sense — and "
        "`vcpkg_installed/<triplet>/share/<port>/copyright` holds that port's verbatim "
        "licence text."
    )
    L.append("")
    L.append(
        "**Qt is not in vcpkg here.** It comes from the official prebuilt shared "
        "libraries via `aqtinstall` ([ADR 0005](adr/0005-qt-acquisition.md)), so its "
        "corresponding source is the Qt archive linked above, unmodified."
    )
    L.append("")

    # -------------------------------------------------------------------- not
    L.append(f"## {H_NOT}")
    L.append("")
    L.append(
        "§0.1 rule 2 forbids silently downgrading a requirement, so the parts of "
        "`REQ-GEN-020` and its neighbours that are **not** yet satisfied are listed "
        "here rather than left to be discovered:"
    )
    L.append("")
    L.extend(table(
        ["Not in place", "Requirement", "Consequence, stated plainly"],
        [
            [
                "No published release, so the per-tag ledger is empty",
                "`REQ-GEN-020`",
                "The per-tag half of the requirement is structurally ready and "
                "completely unexercised. Its validator has never seen a real row.",
            ],
            [
                "No website, so this page is not mirrored anywhere",
                "`REQ-GEN-020`",
                "The repository is the only publication point (OQ-041). The compliance "
                "substance — source beside the binary — does not depend on the mirror; "
                "the stated MUST does.",
            ],
            [
                "No postal address, monitored mailbox, or published PGP key",
                "`REQ-GEN-020`, OQ-013",
                "The written-offer route rests on a platform account (OQ-013). A 1.0.0 "
                "release blocker.",
            ],
            [
                "No `release.yml`, so §25.5 step 8 is not automated",
                "`REQ-BLD-025`",
                "Nothing yet appends a ledger row or fails a stale document at tag "
                "time. Until it exists, the ledger would be updated by hand — which is "
                "why the freshness gate below runs on every push instead.",
            ],
            [
                "No artifact signing and no checksums",
                "`REQ-SEC-016`, `REQ-SEC-017`",
                "A recipient cannot yet verify that a downloaded binary is the one "
                "these sources correspond to. The SBOM half of this row is now "
                "closed — see the row below.",
            ],
            [
                "The SBOM exists but has never been attached to a release",
                "`REQ-GEN-021`, `REQ-SEC-014`",
                "`docs/sbom/eclipse-player.cdx.json` is generated from this same "
                "register by `tools/gen-sbom.py`, which `repo-lint.yml` runs "
                "`--check` on for every push and pull request. `REQ-SEC-014` asks "
                "for one *per release artifact*, which needs §25.5 step 6 and "
                "therefore a release that has not happened yet.",
            ],
            [
                "The Android components (projectM, Chromaprint NDK) are in no build",
                "ADR 0011",
                "They stay in the register and out of the ledger: they have never been "
                "distributed, so no obligation has attached "
                "([ADR 0011](adr/0011-desktop-first-sequencing.md)).",
            ],
        ],
    ))
    L.append("")

    # -------------------------------------------------------------------- gen
    L.append(f"## {H_GEN}")
    L.append("")
    L.append(
        "Both this document and [THIRD-PARTY.md](THIRD-PARTY.md) are emitted by "
        "`tools/gen-third-party/gen-third-party.py`. Regenerate and check them with:"
    )
    L.append("")
    L.append("```sh")
    L.append("python3 tools/gen-third-party/gen-third-party.py --document source-offer")
    L.append("python3 tools/gen-third-party/gen-third-party.py --document source-offer "
             "--check")
    L.append("python3 tools/gen-third-party/gen-third-party.py --self-test")
    L.append("```")
    L.append("")
    L.append(
        "`--check` regenerates in memory and fails if the committed file differs, which "
        "is what makes “kept accurate” mechanical rather than aspirational — the same "
        "gate §25.5 step 8 and §25.6 require at release time. The data are "
        "`register.json` (versions, SPDX ids, source URLs, linkage — shared with the "
        "third-party document) and `releases.json` (the offer's contact facts and the "
        "per-tag ledger). Fix those, never this file."
    )
    L.append("")

    # -------------------------------------------------------------------- see
    L.append(f"## {H_SEE}")
    L.append("")
    L.append("- [THIRD-PARTY.md](THIRD-PARTY.md) — the full §4.2 register, SPDX ids, "
             "patent notes, and the transitive set.")
    L.append("- [ADR 0001](adr/0001-project-license.md) — MPL-2.0 for the core, and why "
             "LGPL-only dependencies.")
    L.append("- [ADR 0005](adr/0005-qt-acquisition.md) — Qt via `aqtinstall`, never "
             "vcpkg, never static.")
    L.append("- [ADR 0006](adr/0006-ffmpeg-lgpl.md) — the FFmpeg configuration and the "
             "build-time licence assertion.")
    L.append("- [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) — OQ-013 (no mailbox or key) and "
             "OQ-041 (no website mirror).")
    L.append("")
    L.append("---")
    L.append("")
    L.append(
        "Generated by `tools/gen-third-party/gen-third-party.py` from "
        "`register.json` and `releases.json`. No timestamp is written, so the output "
        "is deterministic and `--check` is stable."
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

    # 7. the committed ledger must load and validate clean. If it does not, the
    #    document on disk was generated from data that no longer passes its own gate.
    try:
        ledger = load_releases(DEFAULT_RELEASES)
    except RegisterError as exc:
        failures.append(f"ledger: {exc}")
        ledger = None
    if ledger is not None:
        problems = check_ledger(register, ledger)
        if problems:
            failures.append("committed ledger does not validate:\n    "
                            + "\n    ".join(problems))

    # 8. the ledger gate must FIRE on each way a release row can be wrong. A
    #    validator nobody has watched reject something is a validator that passes
    #    everything, and this one guards a licence obligation.
    obliged = [entry["name"] for entry in lgpl_entries(register)]
    good_sha = "a" * 64

    def row(name: str, **overrides) -> dict:
        base = {
            "name": name, "spdx": "LGPL-2.1-or-later", "version": "1.0",
            "upstream_url": "https://example.invalid/src.tar.xz", "sha256": good_sha,
        }
        base.update(overrides)
        return base

    def ledger_with(release: dict, **offer_overrides) -> dict:
        offer = dict(ledger["offer"]) if ledger else {}
        # The committed offer HAS postal_address, set to null (OQ-013), so this must
        # overwrite rather than default it — otherwise every case below trips the
        # no-address gate and the interesting assertions are never reached.
        offer["postal_address"] = "a postal address"
        offer.update(offer_overrides)
        return {"offer": offer, "releases": [release]}

    complete = [row(name) for name in obliged]
    cases: list[tuple[str, dict, str]] = [
        (
            "a complete release row",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31", "components": complete}),
            "",                                              # must produce no problem
        ),
        (
            "an LGPL component with no row at all",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31",
                         "components": complete[:-1]}),
            "REQ-GEN-020",
        ),
        (
            "a not-shipped row with no reason",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31",
                         "components": complete[:-1]
                         + [{"name": obliged[-1], "shipped": False}]}),
            "no 'reason'",
        ),
        (
            "a not-shipped row WITH a reason (must be accepted)",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31",
                         "components": complete[:-1]
                         + [{"name": obliged[-1], "shipped": False,
                             "reason": "feature not enabled"}]}),
            "",
        ),
        (
            "a tag that is not vX.Y.Z",
            ledger_with({"tag": "1.0.0", "date": "2026-01-31", "components": complete}),
            "not a vX.Y.Z release tag",
        ),
        (
            "a date that is not YYYY-MM-DD",
            ledger_with({"tag": "v1.0.0", "date": "31 Jan 2026",
                         "components": complete}),
            "is not YYYY-MM-DD",
        ),
        (
            "a truncated SHA-256",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31",
                         "components": complete[:-1]
                         + [row(obliged[-1], sha256="abc123")]}),
            "64 lowercase hex digits",
        ),
        (
            "a component in no register entry",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31",
                         "components": complete + [row("OpenSSL")]}),
            "matches no register entry",
        ),
        (
            "a published release with no postal address (OQ-013)",
            ledger_with({"tag": "v1.0.0", "date": "2026-01-31", "components": complete},
                        postal_address=None),
            "not an offer",
        ),
    ]
    for label, candidate, expected in cases:
        problems = check_ledger(register, candidate)
        if expected and not any(expected in p for p in problems):
            failures.append(
                f"ledger gate did not fire for {label}: expected {expected!r}, got "
                f"{problems or 'no problems'}"
            )
        if not expected and problems:
            failures.append(f"ledger gate wrongly rejected {label}: {problems}")

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
        f"  the committed release ledger validates, and the REQ-GEN-020 gate fires on "
        f"all {len(cases) - 2} malformed release rows tried (missing component, "
        f"unexplained not-shipped row, bad tag, bad date, short SHA-256, unregistered "
        f"component, published release with no postal address)."
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

    # -- the source-offer document has a second input and a second gate.
    if args.document == "source-offer":
        try:
            ledger = load_releases(args.releases)
        except RegisterError as exc:
            print(f"FATAL: {exc}", file=sys.stderr)
            return None, 2
        problems = check_ledger(register, ledger)
        if problems:
            print(f"{len(problems)} problem(s) in the release ledger — REQ-GEN-020:\n",
                  file=sys.stderr)
            for problem in problems:
                print(f"  ✗ {problem}", file=sys.stderr)
            print(
                "\nREQ-GEN-020: the published offer must link the precise source archive "
                "for every\nLGPL component in every release, keyed by release tag. Fix "
                f"{rel(args.releases)} —\nnot this document, which is generated from it.",
                file=sys.stderr,
            )
            return None, 1
        return render_source_offer(register, ledger, manifest_pins(VCPKG_MANIFEST)), 0

    return render(register, mode, graph, graph_source), 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate docs/THIRD-PARTY.md from the §4.2 register (REQ-GEN-012).",
    )
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and fail (exit 1) if the file on disk is stale")
    parser.add_argument("--resolved-graph", type=Path, metavar="FILE",
                        help="output of `vcpkg install --dry-run`, to cover the transitive set (OQ-025)")
    parser.add_argument("--document", choices=sorted(DOCUMENTS), default="third-party",
                        help="which document to emit (default: third-party)")
    parser.add_argument("--output", type=Path, default=None,
                        help="document to write/check (default: the one for --document)")
    parser.add_argument("--register", type=Path, default=DEFAULT_REGISTER,
                        help=f"register data (default: {rel(DEFAULT_REGISTER)})")
    parser.add_argument("--releases", type=Path, default=DEFAULT_RELEASES,
                        help=f"per-tag LGPL ledger, --document source-offer only "
                             f"(default: {rel(DEFAULT_RELEASES)})")
    parser.add_argument("--self-test", action="store_true",
                        help="test the graph parser and classifier against the committed fixture")
    args = parser.parse_args()
    if args.output is None:
        args.output = DOCUMENTS[args.document]

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

    offer_doc = args.document == "source-offer"
    requirement = "REQ-GEN-020" if offer_doc else "REQ-GEN-012"
    inputs = f"{rel(args.register)} and {rel(args.releases)}" if offer_doc \
        else rel(args.register)
    regenerate = (
        "  python3 tools/gen-third-party/gen-third-party.py"
        + (f" --document {args.document}" if offer_doc else "")
        + (f" --resolved-graph {args.resolved_graph}"
           if args.resolved_graph and not offer_doc else "")
    )

    if args.check:
        if not args.output.exists():
            print(f"{rel(args.output)} is MISSING — run the generator to create it "
                  f"({requirement}, §27).", file=sys.stderr)
            return 1
        current = args.output.read_text(encoding="utf-8")
        if current != content:
            print(
                f"{rel(args.output)} is STALE — {requirement}.\n\n"
                f"It does not match what the generator produces from "
                f"{inputs}.\nRegenerate and commit it:\n{regenerate}",
                file=sys.stderr,
            )
            return 1
        detail = (
            f"{len(ledger_summary(args.releases))} published release(s) in the ledger"
            if offer_doc
            else f"mode: {'resolved-graph' if args.resolved_graph else 'direct-only'}"
        )
        print(f"{rel(args.output)} is up to date ({len(content.splitlines())} lines, "
              f"{detail}).")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    if offer_doc:
        published = len(ledger_summary(args.releases))
        print(f"wrote {rel(args.output)} ({len(content.splitlines())} lines, "
              f"{published} published release(s) in the ledger).")
        if not published:
            print("  The per-tag ledger is EMPTY: no release has been published, so no "
                  "source\n  obligation has attached yet. §25.5 step 8 appends a row per "
                  "tag (REQ-GEN-020).")
        return 0
    mode = "resolved-graph" if args.resolved_graph else "direct-only"
    print(f"wrote {rel(args.output)} ({len(content.splitlines())} lines, mode: {mode}).")
    if mode == "direct-only":
        print("  NOT covered here: the transitive port set. Pass --resolved-graph with "
              "the\n  output of `vcpkg install --dry-run` to include it (OQ-025).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""CycloneDX SBOM generator — spec §25.6 (REQ-GEN-021) and §25.4 (REQ-SEC-014).

REQ-GEN-021: "Every release MUST include a CycloneDX SBOM (§25.6) enumerating all
components with SPDX licence identifiers."  REQ-SEC-014 adds the release-time half:
an SBOM per release artifact, a licence audit, and a CVE scan that fails the build
on a new high-severity finding.

WHY THIS IS GENERATED
    For the same reason docs/THIRD-PARTY.md is. tools/gen-third-party/register.json
    is the single source of truth for §4.2, and a second copy of the same versions,
    licences and source URLs starts drifting the day it is written. The two facts an
    SBOM needs that no other consumer does — a component's canonical machine name
    and its CycloneDX scope — therefore live in that register's `sbom` block rather
    than in a file of their own. A desktop entry with no `sbom` block is a hard
    failure here, so adding a dependency cannot quietly skip the SBOM.

WHY CycloneDX 1.6
    It is the current specification version, and `component.version` is optional
    from 1.4 onward, which this register needs (see VERSION PRECISION below). No
    field newer than 1.4 is used, so re-emitting at a lower specVersion is a
    one-line change if a consumer ever demands it.

WHY THE COMMITTED BASELINE HAS NO TIMESTAMP AND NO serialNumber
    Both are optional in CycloneDX, and both would change on every regeneration.
    A document that differs from itself run to run cannot be checked for staleness,
    and `--check` — the gate that makes "generated, never hand-written" mean
    something — is the whole reason the file is committed. The release pipeline
    passes `--timestamp` and `--serial-number` for the artifact it publishes, where
    a unique identity is what is wanted; the tree keeps the stable baseline.

THE LICENCE ALLOWLIST IS THE §25.4 LICENCE AUDIT
    ALLOWED_LICENCES below is not documentation. Every SPDX identifier in the
    register, and every identifier inside every SPDX expression, must appear in it
    or this tool fails. An identifier it has not been told about is a failure and
    never a guess: §4.1 forbids GPL and AGPL outright, and the honest way to notice
    a new dependency arriving under an unexamined licence is for the build to stop.

VERSION PRECISION
    Only three of the thirteen desktop entries pin an exact release; the other ten
    record a series (`2.x`, `3.4x`, `≥ 0.2.2`, `current`). `component.version` is
    a version, not prose, so a series yields no `version` field at all and the raw
    register string is kept as a property instead. `--require-exact-versions` is the
    gate that says so out loud: it fails today and names every offender. A resolved
    vcpkg graph (`--resolved-graph`) supplies real releases and clears every offender
    that has a vcpkg port, which is what makes the gate satisfiable rather than
    aspirational. One offender survives even then — nlohmann/json has no port at all
    and can only be pinned in the register — and the gate says which kind each is
    rather than pointing at a flag that would not help (OQ-047, OQ-026).

PURLS NAME THE PACKAGE MANAGER THAT INSTALLED THE THING
    `vcpkg` is a registered purl type, so the ports vcpkg installs are
    `pkg:vcpkg/<port>`, qualified by the pinned registry baseline
    (`repository_revision`) and the triplet. `port_version` is emitted only where it
    is known, because the type definition says an omitted `port_version` means
    port-version 0 — omitting an unknown one would assert 0 rather than say nothing.
    Qt 6 and nlohmann/json come through no package manager and stay `pkg:generic`,
    which is what that type is for.

NO CPEs, AND THE PURL DOES NOT MAKE UP FOR IT
    No component carries a `cpe`, because the register records no CPE names and
    writing them from memory would be inventing data. The consequence, read out of
    the scanners rather than assumed: osv-scanner maps neither `vcpkg` nor `generic`
    to any ecosystem, trivy classifies both as unknown and skips them before
    matching, and grype resolves no CPE and returns nothing unless
    `--add-cpes-if-none` (off by default) is passed. So a purl-only CVE scan over
    this document covers nothing today, and `security.yml` has to say that instead
    of banking the green (OQ-046). A wrong CPE would be worse still — it matches
    nothing and looks like it worked.

THE DEPENDENCY GRAPH SAYS ONLY WHAT IS KNOWN
    In CycloneDX, `"dependsOn": []` asserts that a component has no dependencies,
    while omitting its entry means they are unknown. FFmpeg pulls in zlib; nobody
    here has resolved what utfcpp pulls in. So the graph carries the edges the
    register actually records — the application to its direct set, and each parent
    to the transitive ports the register attributes to it — and leaves everything
    else absent rather than claiming an empty dependency list.

VALIDATION IS A STRUCTURAL SUBSET, DELIBERATELY
    `validate_bom` checks the invariants that can be got wrong by hand: the required
    top-level fields, unique bom-refs, the licence array shape, purls agreeing with
    component names, and every dependency reference resolving to a component that
    exists. It is NOT a JSON Schema validation.

    The generated document HAS been checked against the canonical
    bom-1.6.schema.json — with CycloneDX's own valid example as a control and a
    planted enum defect to prove the check was not vacuous — but that ran through a
    throwaway draft-07 adapter, not through anything committed here:
    tools/jsonschema_mini.py implements a draft-2020-12 subset, while the CycloneDX
    schemas are draft-07 with cross-file `$ref`s. CI closes the gap with
    `cyclonedx-cli validate --input-version v1_6`, which embeds its schemas and so
    needs no network (OQ-048).

--self-test
    Per OQ-045: a gate that has never failed is not evidence. The self-test plants
    each defect this tool exists to catch — a missing `sbom` block, a scope outside
    the enum, an unjustified exclusion, a forbidden licence, a dangling dependency
    reference, an upper-case serialNumber, a component name that is not one of its
    entry's own vcpkg ports, a licence regression between two releases — over
    synthetic registers held in memory, and requires each to be caught while the
    lookalikes that must pass still pass. It also spells out every purl shape and
    runs the `--resolved-graph` path through the real vcpkg dry-run parser and
    classifier, because that path had a bucket-shape bug no amount of reading
    caught and one run found immediately.

Exit codes match gen-third-party.py: 0 success, 1 a gate failed, 2 usage or I/O.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parent
GEN_THIRD_PARTY = SCRIPT_DIR / "gen-third-party" / "gen-third-party.py"
DEFAULT_REGISTER = SCRIPT_DIR / "gen-third-party" / "register.json"
DEFAULT_RELEASES = SCRIPT_DIR / "gen-third-party" / "releases.json"
DEFAULT_OUTPUT = REPO / "docs" / "sbom" / "eclipse-player.cdx.json"
VERSION_FILE = REPO / "desktop" / "version.txt"
QT_VERSION_FILE = REPO / "desktop" / "qt-version.txt"
VCPKG_MANIFEST = REPO / "desktop" / "vcpkg.json"

SPEC_VERSION = "1.6"
SCHEMA_URL = f"http://cyclonedx.org/schema/bom-{SPEC_VERSION}.schema.json"

ROOT_NAME = "eclipse-player"
ROOT_LICENCE = "MPL-2.0"          # REQ-GEN-010
PROP_NS = "eclipse:"


class SbomError(Exception):
    """An input is missing or structurally wrong — an exit-2 condition."""


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return str(path)


# ===========================================================================
#  gen-third-party.py, reused rather than copied
#
#  It already owns the vcpkg dry-run parser, the port index and the
#  direct/transitive/build-only/unknown classifier, all of which this tool needs
#  and none of which should exist twice: two parsers for one format disagree
#  eventually, and then the licence table and the SBOM disagree about what was
#  built. Its filename is not a Python identifier, so it is loaded by path.
# ===========================================================================
_GTP = None


def gtp():
    """The gen-third-party module, loaded once. Its top level is side-effect free."""
    global _GTP
    if _GTP is None:
        if not GEN_THIRD_PARTY.exists():
            raise SbomError(f"{rel(GEN_THIRD_PARTY)} does not exist; this tool reuses "
                            f"its register loader and vcpkg graph parser")
        spec = importlib.util.spec_from_file_location(
            "eclipse_gen_third_party", GEN_THIRD_PARTY)
        if spec is None or spec.loader is None:
            raise SbomError(f"cannot import {rel(GEN_THIRD_PARTY)}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        _GTP = module
    return _GTP


# ===========================================================================
#  The licence audit (§25.4, REQ-GEN-012)
#
#  Every SPDX identifier the register may carry, with the reason it is acceptable
#  under §4.1. This table is the audit: an identifier absent from it fails the
#  build. §4.1 admits permissive licences and LGPL with dynamic linking plus a
#  source offer, and refuses GPL and AGPL for the core — REQ-GEN-010 accepts the
#  consequence that some libraries are therefore unavailable to this project.
# ===========================================================================
ALLOWED_LICENCES = {
    "MPL-2.0": "the project's own licence (REQ-GEN-010)",
    "LGPL-3.0-only": "§4.1 copyleft: dynamic linking plus a source offer (Qt 6)",
    "LGPL-2.1-or-later": "§4.1 copyleft: dynamic linking plus a source offer",
    "MPL-1.1": "TagLib's alternative arm; weak copyleft, file-scoped",
    "BSD-2-Clause": "permissive, attribution only",
    "BSD-3-Clause": "permissive, attribution plus a no-endorsement clause",
    "MIT": "permissive, attribution only",
    "Zlib": "permissive, attribution only",
    "blessing": "SQLite's public-domain dedication; the SPDX id is lower case",
    "BSL-1.0": "Boost Software Licence; permissive, no binary attribution",
    "Apache-2.0": "permissive with an explicit patent grant",
    "ISC": "permissive, attribution only",
}

# SPDX licence *exceptions* (the operand after WITH). Empty on purpose: no
# dependency in this register uses one. A WITH expression therefore fails with a
# message naming what would have to be added, rather than being waved through by
# a tokeniser that skipped the operand it did not understand.
ALLOWED_EXCEPTIONS: dict[str, str] = {}

# The forbidden families, named so the failure message can say why rather than
# only that. Matched as a prefix on the identifier.
REFUSED_PREFIXES = {
    "GPL-": "§4.1 refuses GPL for the core; REQ-GEN-010 accepts the consequence",
    "AGPL-": "§4.1 refuses AGPL for the core; REQ-GEN-010 accepts the consequence",
    "SSPL-": "not an OSI-approved licence and incompatible with §4.1",
    "CC-BY-NC-": "non-commercial terms are incompatible with §4.1 redistribution",
}

SPDX_OPERATORS = {"AND", "OR", "WITH"}
_TOKEN_RE = re.compile(r"[A-Za-z0-9.+-]+")
_OPERATOR_RE = re.compile(r"(?:^|\s)(?:AND|OR|WITH)(?:\s|$)")


def is_expression(spdx: str) -> bool:
    """True when `spdx` is a compound SPDX expression rather than a single id."""
    return bool(_OPERATOR_RE.search(spdx))


def licence_tokens(spdx: str) -> tuple[list[str], list[str]]:
    """(licence ids, exception ids) in an SPDX expression.

    The operand after WITH is an exception identifier, not a licence identifier,
    and conflating the two is how `GPL-2.0-only WITH Classpath-exception-2.0`
    would slip past a naive allowlist.
    """
    ids: list[str] = []
    exceptions: list[str] = []
    expect_exception = False
    for token in _TOKEN_RE.findall(spdx):
        if token in SPDX_OPERATORS:
            expect_exception = token == "WITH"
            continue
        (exceptions if expect_exception else ids).append(token)
        expect_exception = False
    return ids, exceptions


def audit_licence(spdx: str, where: str) -> list[str]:
    """§25.4's licence audit over one SPDX id or expression."""
    failures: list[str] = []
    if not isinstance(spdx, str) or not spdx.strip():
        return [f"{where}: no SPDX licence identifier (REQ-GEN-021 requires one "
                f"per component)"]

    ids, exceptions = licence_tokens(spdx)
    if not ids:
        return [f"{where}: {spdx!r} contains no licence identifier"]

    for ident in ids:
        if ident in ALLOWED_LICENCES:
            continue
        for prefix, reason in REFUSED_PREFIXES.items():
            if ident.startswith(prefix):
                failures.append(
                    f"{where}: licence {ident!r} is REFUSED — {reason}.")
                break
        else:
            failures.append(
                f"{where}: licence {ident!r} is not in ALLOWED_LICENCES. This tool "
                f"will not guess whether a licence is acceptable. Read §4.1, decide, "
                f"and add the id with its reason in tools/gen-sbom.py.")
    for exc in exceptions:
        if exc not in ALLOWED_EXCEPTIONS:
            failures.append(
                f"{where}: SPDX exception {exc!r} is not in ALLOWED_EXCEPTIONS. An "
                f"exception changes what a licence permits, so it has to be read "
                f"before it is accepted.")
    return failures


def licence_field(spdx: str, url_template: str) -> list[dict]:
    """CycloneDX `licenses`, either one identified licence or one expression.

    The two shapes are never mixed. An array holding both a `license` object and
    an `expression` says two different things about the same component, and the
    expression is the one that carries the operators — so a compound licence is
    emitted as a single-element expression array and nothing else.
    """
    if is_expression(spdx):
        return [{"expression": spdx}]
    licence: dict = {"id": spdx}
    if url_template:
        licence["url"] = url_template.format(id=spdx)
    return [{"license": licence}]


# ===========================================================================
#  Versions
#
#  The register's `version` column is prose written for a table: "7.1.2 (vcpkg
#  port-version 5)", "≥ 0.2.2", "3.4x", "current". CycloneDX's `version` is a
#  version. Only a dotted numeric release survives the conversion; everything
#  else yields no `version` field and keeps the raw string as a property, which
#  is honest about imprecision instead of encoding a range as though it were a
#  release.
# ===========================================================================
EXACT_VERSION_RE = re.compile(r"^\d+(?:\.\d+)*$")
_PAREN_RE = re.compile(r"^(?P<head>.*?)\s*\((?P<note>[^()]*)\)\s*$")

# What may appear in an emitted `version`. The defect worth catching is prose
# leaking through — a space, a parenthesis, a comparison operator — not an
# unusual-but-real upstream version string like a date or `1.2.3#4`.
VERSION_FIELD_RE = re.compile(r"^[0-9][0-9A-Za-z.+~:#_-]*$")


def split_version(raw: str) -> tuple[str | None, str | None]:
    """(exact version or None, trailing annotation or None).

    A single trailing parenthetical is peeled off and kept: FFmpeg's
    "7.1.2 (vcpkg port-version 5)" is an exact 7.1.2 plus a fact about which
    vcpkg port built it, and losing either half loses information.
    """
    text = (raw or "").strip()
    note = None
    match = _PAREN_RE.match(text)
    if match:
        text = match.group("head").strip()
        note = match.group("note").strip() or None
    if EXACT_VERSION_RE.match(text):
        return text, note
    return None, note


# ===========================================================================
#  purls and external references
#
#  `vcpkg` IS a registered purl type, so a port that vcpkg installs is named as
#  one. Its type definition prohibits a namespace, makes the name the port name,
#  and defines the qualifiers used here:
#
#    port_version         the vcpkg packaging revision. Emitted ONLY where it is
#                         known, because the definition states that an omitted
#                         port_version refers to port-version 0 — so omitting an
#                         unknown one is a false claim, not silence. Direct-only
#                         mode knows it for ffmpeg (from the manifest override);
#                         --resolved-graph knows it for every port.
#    repository_revision  the pinned registry baseline commit. Without it a port
#                         name and version do not identify what was built.
#    triplet              "does not affect package identity" per the definition,
#                         but records which ABI this document describes. It is
#                         emitted only for a port whose triplet is known: the
#                         manifest pins the target triplet, so target ports get
#                         it, while vcpkg's own host-side helper ports are built
#                         for a host triplet this run has no way to name. A
#                         resolved graph names every port's triplet and all of
#                         them then carry one.
#
#  `features` is deliberately not emitted: the definition calls it "informational
#  and not currently normative", and the feature set is already carried as
#  register properties, where it cannot be mistaken for identity.
#
#  Qt 6 and nlohmann/json have no vcpkg port at all (`"vcpkg": []` in the
#  register) and stay `pkg:generic`, as does the application itself. There the
#  `download_url` qualifier is set only when the register's source URL is an
#  actual archive; a project homepage or a git repository is not a download URL
#  and is carried in externalReferences, where the type states what it is.
# ===========================================================================
ARCHIVE_SUFFIXES = (".tar.xz", ".tar.gz", ".tar.bz2", ".tar.zst", ".tgz", ".zip")

# Kept to types that have existed since CycloneDX 1.0/1.1 and mean exactly what
# they say. All five are confirmed present in the 1.6 enum; the 1.6 additions
# (`source-distribution` among them) are not used, because nothing here needs a
# distinction these five do not already draw.
REFERENCE_TYPES = {"website", "vcs", "distribution", "license", "documentation"}

_REPO_URL_RE = re.compile(r"^https?://(?:www\.)?(?:github|gitlab)\.com/[^/]+/[^/]+/?$")


def is_archive_url(url: str) -> bool:
    return url.split("?")[0].split("#")[0].lower().endswith(ARCHIVE_SUFFIXES)


def reference_type(url: str) -> str:
    """The externalReference type a source URL honestly deserves."""
    if is_archive_url(url):
        return "distribution"
    if _REPO_URL_RE.match(url):
        return "vcs"
    return "website"


def purl_for(component: str, version: str | None, *,
             vcpkg_port: str | None = None, pins: dict | None = None,
             port_version: object = None, source_url: str | None = None) -> str:
    """`pkg:vcpkg/<port>` for a vcpkg port, `pkg:generic/<name>` for anything else."""
    purl = (f"pkg:vcpkg/{quote(vcpkg_port, safe='')}" if vcpkg_port
            else f"pkg:generic/{quote(component, safe='')}")
    if version:
        # `#` starts a purl subpath. quote() escapes it, so a caller that hands
        # over a vcpkg-style "7.1.2#5" gets a wrong version rather than a purl
        # that silently parses as version 7.1.2 with subpath 5.
        purl += f"@{quote(version, safe='')}"

    qualifiers: list[tuple[str, str]] = []
    if vcpkg_port:
        # Port-version 0 is what an absent qualifier already means, so saying it
        # adds nothing; an unknown port-version must be absent for the same
        # reason it must not be guessed.
        if port_version is not None and str(port_version) not in ("", "0"):
            qualifiers.append(("port_version", str(port_version)))
        for key in ("repository_revision", "triplet"):
            value = (pins or {}).get("baseline" if key == "repository_revision"
                                     else "triplet")
            if value:
                qualifiers.append((key, str(value)))
    elif source_url and is_archive_url(source_url):
        qualifiers.append(("download_url", source_url))

    if qualifiers:
        # purl requires qualifier keys sorted; sorting here rather than relying on
        # the order they happen to be appended in keeps the output canonical.
        purl += "?" + "&".join(f"{key}={quote(value, safe='')}"
                               for key, value in sorted(qualifiers))
    return purl


def prop(name: str, value: object) -> dict:
    return {"name": f"{PROP_NS}{name}", "value": str(value)}


# ===========================================================================
#  Components
# ===========================================================================
SCOPES = ("required", "optional", "excluded")

SCOPE_MEANING = {
    "required": "linked into the shipped artifact",
    "optional": "built only behind a vcpkg feature that is off by default",
    "excluded": "described by the §4.2 register but not reachable at runtime",
}


def desktop_component(entry: dict, url_template: str,
                      resolved_version: str | None = None, *,
                      pins: dict | None = None,
                      port_version: object = None) -> dict:
    """A CycloneDX component from a §4.2 desktop register entry.

    `sbom.component` is the vcpkg port name wherever the entry has one — a rule
    check_register_sbom enforces rather than assumes — so the component name is
    also the purl name and the two cannot drift apart.
    """
    sbom = entry["sbom"]
    name = sbom["component"]
    raw_version = entry["version"]
    version, annotation = split_version(raw_version)
    if resolved_version:
        version = resolved_version
    source_url = entry.get("source_url") or None

    component: dict = {
        "type": "library",
        "bom-ref": name,
        "name": name,
    }
    if version:
        component["version"] = version
    component["scope"] = sbom["scope"]
    component["licenses"] = licence_field(entry["spdx"], url_template)
    component["purl"] = purl_for(name, version,
                                 vcpkg_port=name if entry.get("vcpkg") else None,
                                 pins=pins, port_version=port_version,
                                 source_url=source_url)

    references = []
    if source_url:
        references.append({"type": reference_type(source_url), "url": source_url})
    if url_template and not is_expression(entry["spdx"]):
        references.append({"type": "license",
                           "url": url_template.format(id=entry["spdx"])})
    if references:
        component["externalReferences"] = references

    properties = [
        prop("register-name", entry["name"]),
        prop("register-version", raw_version),
        prop("version-precision",
             "resolved" if resolved_version else ("exact" if version else "series")),
    ]
    if annotation:
        properties.append(prop("version-annotation", annotation))
    properties.append(prop("linkage", entry["linkage"]))
    properties.append(prop("obligation", entry["obligation"]))
    if entry.get("vcpkg"):
        properties.append(prop("vcpkg-port", ", ".join(entry["vcpkg"])))
        if port_version is not None and str(port_version) not in ("", "0"):
            properties.append(prop("vcpkg-port-version", port_version))
    else:
        properties.append(prop("acquisition", entry.get("acquisition", "not via vcpkg")))
    properties.append(prop("scope-meaning", SCOPE_MEANING[sbom["scope"]]))
    if sbom.get("scope_reason"):
        properties.append(prop("scope-reason", sbom["scope_reason"]))
    component["properties"] = properties
    return component


def reference_component(port: dict, scope: str, url_template: str, provenance: str,
                        resolved_version: str | None = None, *,
                        pins: dict | None = None,
                        port_version: object = None) -> dict:
    """A transitive or build-only port, which carries far less register data.

    These records have a licence and a parent and nothing else — no version, no
    source URL — so the component says only that. Its `scope` is inherited from
    the parent that pulls it in, and a property says so, because the register
    records no independent runtime-reachability decision for a transitive port.

    Every port in either of these two sets is a vcpkg port by construction: the
    transitive set is what vcpkg pulled in, and the build-only set is vcpkg's own
    helper ports plus pkgconf. So all of them are `pkg:vcpkg`.
    """
    name = port["name"]
    component: dict = {
        "type": "library",
        "bom-ref": name,
        "name": name,
    }
    if resolved_version:
        component["version"] = resolved_version
    component["scope"] = scope
    component["licenses"] = licence_field(port["spdx"], url_template)
    component["purl"] = purl_for(name, resolved_version, vcpkg_port=name,
                                 pins=pins, port_version=port_version)
    if url_template and not is_expression(port["spdx"]):
        component["externalReferences"] = [
            {"type": "license", "url": url_template.format(id=port["spdx"])}]

    properties = [prop("provenance", provenance)]
    if port.get("licence_display"):
        properties.append(prop("licence-display", port["licence_display"]))
    if port.get("pulled_in_by"):
        properties.append(prop("pulled-in-by", port["pulled_in_by"]))
        properties.append(prop("scope-derivation",
                               f"inherited from {port['pulled_in_by']}; the register "
                               f"records no separate scope for a transitive port"))
    if not resolved_version:
        properties.append(prop("version-precision", "unknown"))
    if port_version is not None and str(port_version) not in ("", "0"):
        properties.append(prop("vcpkg-port-version", port_version))
    properties.append(prop("scope-meaning", SCOPE_MEANING[scope]))
    component["properties"] = properties
    return component


# ===========================================================================
#  Register gates — the register must be SBOM-complete before anything is built
# ===========================================================================
def check_register_sbom(register: dict) -> list[str]:
    """Every failure that makes the register unfit to generate an SBOM from.

    Adding a dependency to §4.2 without deciding what the SBOM should say about
    it is the mistake this catches. It is a failure and not a default, because a
    default would be a guess about what ships.
    """
    failures: list[str] = []
    seen: dict[str, str] = {}

    entries = register.get("platforms", {}).get("desktop", {}).get("entries", [])
    for entry in entries:
        label = entry.get("name", "?")
        sbom = entry.get("sbom")
        if sbom is None:
            failures.append(
                f"desktop entry {label!r} has no 'sbom' block. REQ-GEN-021 needs a "
                f"machine name and a scope for every component; add\n"
                f'      "sbom": {{ "component": "<vcpkg port name>", '
                f'"scope": "required" }}\n'
                f"    to it in tools/gen-third-party/register.json.")
            continue
        if not isinstance(sbom, dict):
            failures.append(f"desktop entry {label!r}: 'sbom' is not an object")
            continue

        component = sbom.get("component")
        if not isinstance(component, str) or not component.strip():
            failures.append(f"desktop entry {label!r}: sbom.component is missing "
                            f"or empty")
            component = None
        elif component in seen:
            failures.append(
                f"sbom.component {component!r} is used by both {seen[component]!r} "
                f"and {label!r}. It is the bom-ref, so it must be unique.")
        else:
            seen[component] = label

        # The purl for a vcpkg port is pkg:vcpkg/<component>, and the resolved
        # graph is looked up by the same name, so a component name that is not
        # one of the entry's own ports would silently produce a purl naming a
        # port that does not exist and a version lookup that always misses.
        ports = entry.get("vcpkg") or []
        if component and ports and component not in ports:
            failures.append(
                f"desktop entry {label!r}: sbom.component is {component!r} but the "
                f"entry's vcpkg port(s) are {', '.join(repr(q) for q in ports)}. "
                f"The component name must be one of them — it is what the purl "
                f"and the resolved-graph lookup both use.")

        scope = sbom.get("scope")
        if scope not in SCOPES:
            failures.append(
                f"desktop entry {label!r}: sbom.scope is {scope!r}; CycloneDX "
                f"permits only {', '.join(SCOPES)}")
        else:
            reason = sbom.get("scope_reason")
            if scope == "required" and reason:
                failures.append(
                    f"desktop entry {label!r}: scope 'required' must not carry a "
                    f"scope_reason — being in the shipped artifact is the default "
                    f"and needs no justification")
            if scope != "required" and not reason:
                failures.append(
                    f"desktop entry {label!r}: scope {scope!r} requires a "
                    f"scope_reason. Taking a component out of the runtime set "
                    f"changes what the SBOM claims was shipped, so the reason "
                    f"belongs in the register.")

        failures.extend(audit_licence(entry.get("spdx", ""),
                                      f"desktop entry {label!r}"))

    known = set(seen)
    transitive = register.get("transitive_reference", {}).get("ports", [])
    transitive_names = {p.get("name") for p in transitive}
    for port in transitive:
        name = port.get("name", "?")
        failures.extend(audit_licence(port.get("spdx", ""),
                                      f"transitive port {name!r}"))
        parent = port.get("pulled_in_by")
        if not parent:
            failures.append(f"transitive port {name!r} has no 'pulled_in_by'; "
                            f"without it the dependency graph cannot place it")
        elif parent not in known and parent not in transitive_names:
            failures.append(
                f"transitive port {name!r} says it is pulled in by {parent!r}, "
                f"which is neither a registered sbom.component nor another "
                f"transitive port")

    for port in register.get("build_only_ports", {}).get("ports", []):
        failures.extend(audit_licence(port.get("spdx", ""),
                                      f"build-only port {port.get('name', '?')!r}"))
    return failures


def resolved_versions(graph: dict | None) -> dict[str, tuple[str, object, str | None]]:
    """port name -> (version, port_version, triplet) for every port with a version.

    classify_graph returns `(port, register_record)` PAIRS in each of its three
    named buckets — direct, transitive and build_only alike — and bare ports only
    in `unknown`. Reading build_only as bare ports raised AttributeError on the
    first real --resolved-graph run; the self-test now builds this map through the
    real parser and the real classifier so the shape cannot drift unnoticed.

    A port the graph lists without a version contributes nothing: the SBOM would
    rather omit `version` than invent one.
    """
    out: dict[str, tuple[str, object, str | None]] = {}
    if graph is None:
        return out
    for port, _record in graph["direct"] + graph["transitive"] + graph["build_only"]:
        if port.version:
            out[port.name] = (port.version, port.port_version, port.triplet)
    return out


def inexact_versions(register: dict,
                     resolved: dict | None = None) -> list[tuple[str, str, bool]]:
    """(component, raw version, resolvable-by-graph) per entry with no exact version.

    A resolved vcpkg graph supplies real releases, so an entry whose port the graph
    resolved is not an offender however loosely the register spells it — which is
    what makes --require-exact-versions satisfiable rather than a standing failure.
    An entry with no vcpkg port at all (Qt 6, nlohmann/json) can never be fixed that
    way, and the third field says so instead of pointing at a flag that will not
    help.
    """
    resolved = resolved or {}
    out = []
    for entry in register.get("platforms", {}).get("desktop", {}).get("entries", []):
        sbom = entry.get("sbom") or {}
        component = sbom.get("component", entry.get("name", "?"))
        if component in resolved:
            continue
        version, _ = split_version(entry.get("version", ""))
        if not version:
            out.append((component, entry.get("version", ""),
                        bool(entry.get("vcpkg"))))
    return out


# ===========================================================================
#  Assembly
# ===========================================================================
def build_bom(register: dict, *, pins: dict, root_version: str,
              qt_version: str | None = None, graph: dict | None = None,
              graph_source: str | None = None, register_path: str | None = None,
              vcs_url: str | None = None, timestamp: str | None = None,
              serial_number: str | None = None) -> dict:
    """The whole document, deterministic given its inputs."""
    url_template = register.get("spdx_url_template", "")
    entries = register["platforms"]["desktop"]["entries"]
    by_component = {e["sbom"]["component"]: e for e in entries}

    # Version and port-version stay apart. vcpkg's own display form is
    # "7.1.2#5", but `#` opens a subpath in a purl and the packaging revision is
    # not part of the upstream version, so the two are never pasted together.
    resolved = resolved_versions(graph)

    # In direct-only mode the manifest's overrides are the only port-versions
    # anyone here knows; every other port's is genuinely unknown.
    pinned_port_versions = {
        override["name"]: override["port-version"]
        for override in pins.get("overrides") or []
        if override.get("port-version") is not None
    }

    def port_pins(triplet: str | None) -> dict:
        """The pins as they apply to one port, with only a triplet it really has."""
        return {"baseline": pins.get("baseline"), "triplet": triplet}

    components: list[dict] = []
    for component_name in sorted(by_component):
        entry = by_component[component_name]
        version = None
        port_version = pinned_port_versions.get(component_name)
        triplet = pins.get("triplet")
        if component_name in resolved:
            version, port_version, triplet = resolved[component_name]
        components.append(desktop_component(entry, url_template, version,
                                            pins=port_pins(triplet),
                                            port_version=port_version))

    # Transitive ports. In direct-only mode this is the register's recorded
    # reference set (OQ-025), not a resolution this run performed, and the
    # provenance property says exactly that so a reader is not misled into
    # treating it as observed fact.
    provenance = (f"resolved vcpkg graph ({graph_source})" if graph_source
                  else "register transitive_reference — recorded in OQ-025 from one "
                       "observed graph, not resolved by this run")
    for port in sorted(register["transitive_reference"]["ports"],
                       key=lambda p: p["name"]):
        parent = by_component.get(port.get("pulled_in_by", ""))
        scope = parent["sbom"]["scope"] if parent else "required"
        version, port_version, triplet = resolved.get(
            port["name"],
            (None, pinned_port_versions.get(port["name"]), pins.get("triplet")))
        components.append(reference_component(port, scope, url_template, provenance,
                                              version, pins=port_pins(triplet),
                                              port_version=port_version))

    for port in sorted(register["build_only_ports"]["ports"], key=lambda p: p["name"]):
        # A host-side helper is not built for the target triplet, and this run
        # cannot name the host one, so it carries no triplet rather than a wrong
        # one. A resolved graph supplies the real value.
        version, port_version, triplet = resolved.get(
            port["name"], (None, pinned_port_versions.get(port["name"]), None))
        components.append(reference_component(
            port, "excluded", url_template,
            "vcpkg build-system helper; host tooling, never linked into an artifact",
            version, pins=port_pins(triplet), port_version=port_version))

    # ---- the graph, carrying only the edges the register actually records ----
    dependencies = [{
        "ref": ROOT_NAME,
        "dependsOn": sorted(name for name, entry in by_component.items()
                            if entry["sbom"]["scope"] != "excluded"),
    }]
    children: dict[str, list[str]] = {}
    for port in register["transitive_reference"]["ports"]:
        parent = port.get("pulled_in_by")
        if parent:
            children.setdefault(parent, []).append(port["name"])
    for parent in sorted(children):
        dependencies.append({"ref": parent, "dependsOn": sorted(children[parent])})

    root: dict = {
        "type": "application",
        "bom-ref": ROOT_NAME,
        "name": ROOT_NAME,
        "version": root_version,
        "licenses": licence_field(ROOT_LICENCE, url_template),
        "purl": purl_for(ROOT_NAME, root_version),
    }
    if vcs_url:
        root["externalReferences"] = [{"type": "vcs", "url": vcs_url}]

    android = register.get("platforms", {}).get("android", {}).get("entries", [])
    metadata_properties = [
        prop("register", register_path or rel(DEFAULT_REGISTER)),
        prop("spec-section", register.get("spec_section", "4.2")),
        prop("requirements", "REQ-GEN-021, REQ-SEC-014, REQ-GEN-012"),
        prop("component-set", "resolved-graph" if graph is not None else "direct-only"),
    ]
    if pins.get("baseline"):
        metadata_properties.append(prop("vcpkg-builtin-baseline", pins["baseline"]))
    if pins.get("triplet"):
        metadata_properties.append(prop("vcpkg-triplet", pins["triplet"]))
    if qt_version:
        metadata_properties.append(prop(
            "qt-version",
            f"{qt_version} (not a vcpkg port; aqtinstall per ADR 0005)"))
    metadata_properties.append(prop(
        "android-entries-omitted",
        f"{len(android)} — the Android app is a Phase 0 scaffold (ADR 0012) whose "
        f"Gradle dependencies are not yet reconciled with the §4.2 register, so no "
        f"Android component is described here; this document covers the desktop build"))
    inexact = inexact_versions(register, resolved)
    metadata_properties.append(prop(
        "components-without-exact-version",
        f"{len(inexact)} of {len(entries)}" + (
            f" ({', '.join(name for name, _, _ in inexact)}) — OQ-047"
            if inexact else "")))
    metadata_properties.append(prop(
        "cpe-absent",
        "no component carries a cpe: the register records no CPE names and this "
        "tool will not invent them. osv-scanner maps neither the vcpkg nor the "
        "generic purl type to any ecosystem, trivy skips both as unknown before "
        "matching, and grype resolves no CPE unless --add-cpes-if-none is passed, "
        "so a purl-only CVE scan over this document is not evidence of no known "
        "vulnerabilities (OQ-046)"))
    metadata_properties.append(prop(
        "validation",
        "this generator checks structure only. The document has been validated "
        "against the canonical bom-1.6.schema.json in a one-off local run; the "
        "committed validator implements a draft-2020-12 subset and the CycloneDX "
        "schemas are draft-07, so CI validates with cyclonedx-cli --input-version "
        "v1_6, which embeds its schemas (OQ-048)"))

    metadata: dict = {}
    if timestamp:
        metadata["timestamp"] = timestamp
    metadata["tools"] = {"components": [{
        "type": "application",
        "name": "tools/gen-sbom.py",
        "version": root_version,
    }]}
    metadata["component"] = root
    metadata["properties"] = metadata_properties

    bom: dict = {
        "$schema": SCHEMA_URL,
        "bomFormat": "CycloneDX",
        "specVersion": SPEC_VERSION,
        "version": 1,
    }
    if serial_number:
        bom["serialNumber"] = serial_number
    bom["metadata"] = metadata
    bom["components"] = components
    bom["dependencies"] = dependencies
    return bom


def serialise(bom: dict) -> str:
    """Two-space JSON with a trailing newline — diffable, and stable run to run."""
    return json.dumps(bom, indent=2, ensure_ascii=False, sort_keys=False) + "\n"


# ===========================================================================
#  Validation — a documented structural subset, not JSON Schema (OQ-048)
# ===========================================================================
# The CycloneDX 1.6 `serialNumber` pattern is lowercase hex only, so an
# uppercase UUID is a schema failure and is rejected here rather than emitted.
_UUID_RE = re.compile(
    r"^urn:uuid:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
_TIMESTAMP_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$")
COMPONENT_TYPES = {"application", "library", "framework", "container", "file",
                   "device", "firmware", "operating-system"}


def validate_bom(bom: object) -> list[str]:
    """Invariants that can be got wrong by hand. Empty list means it holds."""
    failures: list[str] = []
    if not isinstance(bom, dict):
        return ["the document is not a JSON object"]

    if bom.get("bomFormat") != "CycloneDX":
        failures.append(f"bomFormat is {bom.get('bomFormat')!r}, must be 'CycloneDX'")
    if not isinstance(bom.get("specVersion"), str) or not bom.get("specVersion"):
        failures.append("specVersion is missing")
    if not isinstance(bom.get("version"), int) or bom.get("version", 0) < 1:
        failures.append(f"version is {bom.get('version')!r}, must be an integer >= 1")
    if "serialNumber" in bom and not _UUID_RE.match(str(bom["serialNumber"])):
        failures.append(f"serialNumber {bom['serialNumber']!r} is not a urn:uuid")

    metadata = bom.get("metadata") or {}
    if "timestamp" in metadata and not _TIMESTAMP_RE.match(str(metadata["timestamp"])):
        failures.append(f"metadata.timestamp {metadata['timestamp']!r} is not ISO-8601")

    refs: set[str] = set()

    def check_component(component: object, where: str) -> None:
        if not isinstance(component, dict):
            failures.append(f"{where} is not an object")
            return
        name = component.get("name")
        if not isinstance(name, str) or not name.strip():
            failures.append(f"{where}: name is missing or empty")
        if component.get("type") not in COMPONENT_TYPES:
            failures.append(f"{where}: type {component.get('type')!r} is not one of "
                            f"{', '.join(sorted(COMPONENT_TYPES))}")
        ref = component.get("bom-ref")
        if not isinstance(ref, str) or not ref.strip():
            failures.append(f"{where}: bom-ref is missing or empty")
        elif ref in refs:
            failures.append(f"{where}: bom-ref {ref!r} is used more than once; "
                            f"dependency references would be ambiguous")
        else:
            refs.add(ref)

        if "version" in component:
            version = component["version"]
            if not isinstance(version, str) or not VERSION_FIELD_RE.match(version):
                failures.append(
                    f"{where}: version {version!r} is not a version. A range, a "
                    f"series or a parenthetical belongs in a property, not here.")

        scope = component.get("scope")
        if scope is not None and scope not in SCOPES:
            failures.append(f"{where}: scope {scope!r} is not one of "
                            f"{', '.join(SCOPES)}")

        licences = component.get("licenses")
        if not isinstance(licences, list) or not licences:
            failures.append(f"{where}: licenses is missing or empty — REQ-GEN-021 "
                            f"requires an SPDX identifier per component")
        else:
            expressions = [x for x in licences if isinstance(x, dict) and "expression" in x]
            identified = [x for x in licences if isinstance(x, dict) and "license" in x]
            if expressions and identified:
                failures.append(f"{where}: licenses mixes an expression with an "
                                f"identified licence; an expression must be the "
                                f"only element")
            if expressions and len(licences) > 1:
                failures.append(f"{where}: licenses holds {len(licences)} elements "
                                f"beside an expression")
            for item in identified:
                licence = item["license"]
                if not isinstance(licence, dict) or not licence.get("id"):
                    failures.append(f"{where}: a licenses entry has no id")
            for item in expressions:
                failures.extend(audit_licence(item["expression"], where))
            for item in identified:
                if isinstance(item.get("license"), dict):
                    failures.extend(audit_licence(str(item["license"].get("id", "")),
                                                  where))

        purl = component.get("purl")
        if purl is not None:
            if not str(purl).startswith("pkg:"):
                failures.append(f"{where}: purl {purl!r} does not start with 'pkg:'")
            elif isinstance(name, str):
                stem = str(purl).split("?")[0].split("@")[0]
                if stem.rsplit("/", 1)[-1] != quote(name, safe=""):
                    failures.append(f"{where}: purl {purl!r} names something other "
                                    f"than the component {name!r}")

        for reference in component.get("externalReferences", []) or []:
            if not isinstance(reference, dict):
                failures.append(f"{where}: an externalReference is not an object")
                continue
            if reference.get("type") not in REFERENCE_TYPES:
                failures.append(f"{where}: externalReference type "
                                f"{reference.get('type')!r} is outside the "
                                f"conservative set this tool emits")
            if not str(reference.get("url", "")).strip():
                failures.append(f"{where}: an externalReference has no url")

        for property_ in component.get("properties", []) or []:
            if not isinstance(property_, dict) or not property_.get("name"):
                failures.append(f"{where}: a property has no name")

    root = metadata.get("component")
    if root is None:
        failures.append("metadata.component is absent, so the document does not say "
                        "what it describes")
    else:
        check_component(root, "metadata.component")

    components = bom.get("components")
    if not isinstance(components, list):
        failures.append("components is missing or not an array")
        components = []
    for index, component in enumerate(components):
        label = component.get("name", index) if isinstance(component, dict) else index
        check_component(component, f"components[{label}]")

    names = [c.get("name") for c in components if isinstance(c, dict)]
    if len(names) != len(set(names)):
        duplicates = sorted({n for n in names if names.count(n) > 1})
        failures.append(f"duplicate component name(s): {', '.join(map(str, duplicates))}")

    for index, edge in enumerate(bom.get("dependencies") or []):
        if not isinstance(edge, dict):
            failures.append(f"dependencies[{index}] is not an object")
            continue
        ref = edge.get("ref")
        if ref not in refs:
            failures.append(f"dependencies[{index}]: ref {ref!r} matches no bom-ref")
        depends = edge.get("dependsOn")
        if depends is None:
            continue
        if not isinstance(depends, list):
            failures.append(f"dependencies[{index}]: dependsOn is not an array")
            continue
        for target in depends:
            if target == ref:
                failures.append(f"dependencies[{index}]: {ref!r} depends on itself")
            elif target not in refs:
                failures.append(f"dependencies[{index}]: dependsOn {target!r} "
                                f"matches no bom-ref")
    return failures


# ===========================================================================
#  Diff against the previous release — §25.4 step 4
#
#  Failure policy, stated because it is a judgement and not an obvious default:
#  a diff that failed on ANY change would fail on every legitimate version bump,
#  and a gate that fires on correct work gets disabled. So the gate fires on one
#  thing only — a licence that is not in ALLOWED_LICENCES — and everything else,
#  including an allowed-to-allowed licence change, is reported for a human to
#  read. Losing a component is reported loudly too: it can mean a dependency was
#  dropped, or it can mean the generator stopped seeing it.
# ===========================================================================
def licence_summary(component: dict) -> str:
    parts = []
    for item in component.get("licenses") or []:
        if not isinstance(item, dict):
            continue
        if "expression" in item:
            parts.append(str(item["expression"]))
        elif isinstance(item.get("license"), dict):
            parts.append(str(item["license"].get("id", "?")))
    return " / ".join(parts) or "(none)"


def index_components(bom: dict) -> dict[str, dict]:
    index = {}
    root = (bom.get("metadata") or {}).get("component")
    if isinstance(root, dict) and root.get("name"):
        index[str(root["name"])] = root
    for component in bom.get("components") or []:
        if isinstance(component, dict) and component.get("name"):
            index[str(component["name"])] = component
    return index


def diff_boms(old: dict, new: dict) -> tuple[list[str], list[str]]:
    """(report lines, gate failures) between a previous SBOM and this one."""
    before, after = index_components(old), index_components(new)
    lines: list[str] = []

    for name in sorted(set(after) - set(before)):
        component = after[name]
        lines.append(f"  + added    {name} {component.get('version', '(no version)')} "
                     f"[{licence_summary(component)}]")
    for name in sorted(set(before) - set(after)):
        lines.append(f"  - removed  {name} — a dropped dependency, or a component the "
                     f"generator stopped seeing")
    for name in sorted(set(before) & set(after)):
        old_component, new_component = before[name], after[name]
        old_version = old_component.get("version", "(no version)")
        new_version = new_component.get("version", "(no version)")
        if old_version != new_version:
            lines.append(f"  ~ version  {name}: {old_version} -> {new_version}")
        old_licence, new_licence = (licence_summary(old_component),
                                    licence_summary(new_component))
        if old_licence != new_licence:
            lines.append(f"  ! licence  {name}: {old_licence} -> {new_licence} "
                         f"(REVIEW: §4.1)")
        if old_component.get("scope") != new_component.get("scope"):
            lines.append(f"  ~ scope    {name}: {old_component.get('scope')} -> "
                         f"{new_component.get('scope')}")

    failures: list[str] = []
    for name in sorted(after):
        failures.extend(audit_licence(licence_summary(after[name]),
                                      f"the new SBOM's {name!r}"))
    return lines, failures


# ===========================================================================
#  Self-test (OQ-045)
# ===========================================================================
_SELF_TEST_PINS = {"baseline": "0" * 40, "overrides": [], "triplet": "x64-linux-test"}


def _entry(component: str, *, spdx: str = "MIT", version: str = "1.2.3",
           scope: str = "required", reason: str | None = None,
           source_url: str | None = None, drop_sbom: bool = False,
           vcpkg: list[str] | None = None) -> dict:
    entry: dict = {
        "name": component.upper(),
        "spdx": spdx,
        "licence_display": spdx,
        "register_version": version,
        "version": version,
        "linkage": "Dynamic",
        "obligation": "Attribution.",
        "source_url": source_url or f"https://example.invalid/{component}",
        "vcpkg": [component] if vcpkg is None else vcpkg,
        "acquisition": "synthetic",
        "notes": [],
    }
    if not drop_sbom:
        entry["sbom"] = {"component": component, "scope": scope}
        if reason is not None:
            entry["sbom"]["scope_reason"] = reason
    return entry


def _register(entries, transitive=(), build_only=()) -> dict:
    return {
        "spec_section": "4.2",
        "spdx_url_template": "https://spdx.org/licenses/{id}.html",
        "platforms": {
            "desktop": {"shipped": True, "entries": list(entries)},
            "android": {"shipped": False, "entries": []},
        },
        "transitive_reference": {"source": "synthetic", "ports": list(transitive)},
        "build_only_ports": {"ports": list(build_only)},
    }


def _bom(register: dict) -> dict:
    return build_bom(register, pins=_SELF_TEST_PINS, root_version="9.9.9",
                     register_path="synthetic/register.json")


# Registers that MUST be rejected. Each is one deliberate defect over an
# otherwise valid register, so a pass here means the specific rule fired.
MUST_REJECT = [
    ("a desktop entry with no sbom block",
     _register([_entry("alpha", drop_sbom=True)])),
    ("a scope outside CycloneDX's enum",
     _register([_entry("alpha", scope="runtime")])),
    ("an excluded component with no scope_reason",
     _register([_entry("alpha", scope="excluded")])),
    ("an optional component with no scope_reason",
     _register([_entry("alpha", scope="optional")])),
    ("a required component carrying a scope_reason it does not need",
     _register([_entry("alpha", reason="because")])),
    ("two entries claiming the same sbom.component",
     _register([_entry("alpha"), _entry("alpha", version="2.0.0")])),
    ("a GPL dependency, which §4.1 refuses",
     _register([_entry("alpha", spdx="GPL-3.0-only")])),
    ("an AGPL dependency, which §4.1 refuses",
     _register([_entry("alpha", spdx="AGPL-3.0-or-later")])),
    ("a refused licence hidden inside an OR expression",
     _register([_entry("alpha", spdx="MIT OR GPL-3.0-only")])),
    ("an unexamined licence nobody has decided about",
     _register([_entry("alpha", spdx="WTFPL")])),
    ("an SPDX exception, whose effect nobody has read",
     _register([_entry("alpha", spdx="MIT WITH Classpath-exception-2.0")])),
    ("a licence identifier with the wrong case",
     _register([_entry("alpha", spdx="Blessing")])),
    ("an entry with no licence at all",
     _register([dict(_entry("alpha"), spdx="")])),
    ("a transitive port pulled in by nothing",
     _register([_entry("alpha")],
               transitive=[{"name": "beta", "spdx": "MIT",
                            "licence_display": "MIT"}])),
    ("a transitive port pulled in by a component that does not exist",
     _register([_entry("alpha")],
               transitive=[{"name": "beta", "spdx": "MIT", "licence_display": "MIT",
                            "pulled_in_by": "ghost"}])),
    ("a refused licence on a transitive port",
     _register([_entry("alpha")],
               transitive=[{"name": "beta", "spdx": "GPL-2.0-only",
                            "licence_display": "GPL", "pulled_in_by": "alpha"}])),
    ("a refused licence on a build-only port",
     _register([_entry("alpha")],
               build_only=[{"name": "helper", "spdx": "SSPL-1.0",
                            "licence_display": "SSPL"}])),
    ("a component name that is not one of the entry's own vcpkg ports",
     _register([_entry("alpha", vcpkg=["alpha-port"])])),
]

# Registers that MUST pass. Without these the suite above would be satisfied by
# a checker that rejects everything.
MUST_ACCEPT = [
    ("a minimal well-formed register", _register([_entry("alpha")])),
    ("a dual licence whose every arm is allowed",
     _register([_entry("alpha", spdx="LGPL-2.1-or-later OR MPL-1.1")])),
    ("an excluded component with its reason given",
     _register([_entry("alpha", scope="excluded", reason="test-only")])),
    ("an optional component with its reason given",
     _register([_entry("alpha", scope="optional", reason="off by default")])),
    ("SQLite's lower-case public-domain id",
     _register([_entry("alpha", spdx="blessing")])),
    ("a series version, which is imprecise but not invalid",
     _register([_entry("alpha", version="2.x")])),
    ("a transitive port placed under its parent",
     _register([_entry("alpha")],
               transitive=[{"name": "beta", "spdx": "BSL-1.0",
                            "licence_display": "Boost-1.0",
                            "pulled_in_by": "alpha"}])),
    ("a build-only helper port",
     _register([_entry("alpha")],
               build_only=[{"name": "helper", "spdx": "MIT",
                            "licence_display": "MIT"}])),
    ("an entry with no vcpkg port at all, as Qt 6 and nlohmann/json have none",
     _register([_entry("alpha", vcpkg=[])])),
    ("an entry listing more ports than one, its component among them",
     _register([_entry("alpha", vcpkg=["alpha", "alpha-extra"])])),
]

# (raw register string, exact version or None, annotation or None)
VERSION_CASES = [
    ("6.8.2", "6.8.2", None),
    ("1.15.2", "1.15.2", None),
    ("7.1.2 (vcpkg port-version 5)", "7.1.2", "vcpkg port-version 5"),
    ("2.x", None, None),
    ("2.3.x", None, None),
    ("3.4x", None, None),
    ("≥ 0.2.2", None, None),
    ("current", None, None),
    ("6.x", None, None),
    ("", None, None),
    ("7", "7", None),
]

# (raw source URL, expected externalReference type)
# The registry baseline and triplet a purl is qualified with. Distinct from
# _SELF_TEST_PINS so a case that drops a qualifier is visible in the expected
# string rather than lost in a field of zeroes.
_PURL_PINS = {"baseline": "b" * 40, "overrides": [], "triplet": "x64-linux-test"}
_REV = "b" * 40

# Every purl shape this document can emit, spelled out. Qualifier keys are
# sorted, port-version 0 and unknown are both absent, and a vcpkg-style version
# with a '#' cannot open a subpath.
PURL_CASES = [
    ("a vcpkg port with a known port-version",
     dict(component="ffmpeg", version="7.1.2", vcpkg_port="ffmpeg",
          pins=_PURL_PINS, port_version=5),
     f"pkg:vcpkg/ffmpeg@7.1.2?port_version=5&repository_revision={_REV}"
     f"&triplet=x64-linux-test"),
    ("a vcpkg port whose port-version is unknown — omitted, not guessed as 0",
     dict(component="taglib", version=None, vcpkg_port="taglib", pins=_PURL_PINS),
     f"pkg:vcpkg/taglib?repository_revision={_REV}&triplet=x64-linux-test"),
    ("port-version 0 is what an absent qualifier already means",
     dict(component="zlib", version="1.3.1", vcpkg_port="zlib", pins=_PURL_PINS,
          port_version=0),
     f"pkg:vcpkg/zlib@1.3.1?repository_revision={_REV}&triplet=x64-linux-test"),
    ("a port-version arriving as a string, as the graph parser yields it",
     dict(component="zlib", version="1.3.1", vcpkg_port="zlib", pins=_PURL_PINS,
          port_version="2"),
     f"pkg:vcpkg/zlib@1.3.1?port_version=2&repository_revision={_REV}"
     f"&triplet=x64-linux-test"),
    ("no pins at all: a bare vcpkg purl rather than an empty qualifier",
     dict(component="zlib", version="1.3.1", vcpkg_port="zlib", pins=None),
     "pkg:vcpkg/zlib@1.3.1"),
    ("a vcpkg display version must not open a purl subpath at '#'",
     dict(component="ffmpeg", version="7.1.2#5", vcpkg_port="ffmpeg",
          pins=_PURL_PINS),
     f"pkg:vcpkg/ffmpeg@7.1.2%235?repository_revision={_REV}"
     f"&triplet=x64-linux-test"),
    ("no vcpkg port, source URL is an archive: generic with download_url",
     dict(component="qt6", version="6.8.2",
          source_url="https://download.qt.io/x/qt-everywhere-src-6.8.2.tar.xz"),
     "pkg:generic/qt6@6.8.2?download_url=https%3A%2F%2Fdownload.qt.io%2Fx%2F"
     "qt-everywhere-src-6.8.2.tar.xz"),
    ("no vcpkg port, source URL is a repository: no qualifier, it is not a download",
     dict(component="nlohmann-json", version=None,
          source_url="https://github.com/nlohmann/json"),
     "pkg:generic/nlohmann-json"),
    ("the application itself",
     dict(component="eclipse-player", version="0.1.0"),
     "pkg:generic/eclipse-player@0.1.0"),
    ("pins are ignored for a component that came through no package manager",
     dict(component="qt6", version="6.8.2", pins=_PURL_PINS, port_version=5),
     "pkg:generic/qt6@6.8.2"),
]

REFERENCE_CASES = [
    ("https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz", "distribution"),
    ("https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz",
     "distribution"),
    ("https://github.com/taglib/taglib", "vcs"),
    ("https://github.com/nlohmann/json", "vcs"),
    ("https://www.surina.net/soundtouch/", "website"),
    ("https://zlib.net/", "website"),
    ("https://github.com/projectM-visualizer/projectm/releases/tag/v4.1.0", "website"),
]


# A richer base than the single-entry registers above: two direct components, a
# transitive port under one of them, and a build-only helper, so a mutation has
# somewhere to hide.
_MUTATION_REGISTER = _register(
    [_entry("alpha", source_url="https://example.invalid/alpha-1.2.3.tar.gz"),
     _entry("beta", spdx="Zlib", version="1.3.1")],
    transitive=[{"name": "gamma", "spdx": "MIT", "licence_display": "MIT",
                 "pulled_in_by": "alpha"}],
    build_only=[{"name": "helper", "spdx": "ISC", "licence_display": "ISC"}],
)


def _first(bom: dict) -> dict:
    return bom["components"][0]


# Each mutation is one hand-made mistake in an otherwise valid document. A
# validator that returns an empty list for all of them is not validating.
BOM_MUTATIONS = [
    ("a duplicated bom-ref",
     lambda b: b["components"].append(dict(_first(b)))),
    ("a missing bomFormat", lambda b: b.pop("bomFormat")),
    ("an empty specVersion", lambda b: b.update(specVersion="")),
    ("a document version of zero", lambda b: b.update(version=0)),
    ("a serialNumber that is not a urn:uuid",
     lambda b: b.update(serialNumber="12345")),
    ("a serialNumber whose hex is upper-case, which the 1.6 pattern forbids",
     lambda b: b.update(
         serialNumber="urn:uuid:00000000-0000-4000-8000-00000000000A")),
    ("a timestamp that is not ISO-8601",
     lambda b: b["metadata"].update(timestamp="last Tuesday")),
    ("no metadata.component, so the document says nothing about itself",
     lambda b: b["metadata"].pop("component")),
    ("a dependsOn naming a component that does not exist",
     lambda b: b["dependencies"][0]["dependsOn"].append("ghost")),
    ("a dependency edge whose ref matches no component",
     lambda b: b["dependencies"].append({"ref": "ghost", "dependsOn": []})),
    ("a component depending on itself",
     lambda b: b["dependencies"].append({"ref": "alpha", "dependsOn": ["alpha"]})),
    ("a licences array mixing an expression with an identified licence",
     lambda b: _first(b).update(licenses=[{"license": {"id": "MIT"}},
                                          {"expression": "MIT OR Zlib"}])),
    ("prose in the version field",
     lambda b: _first(b).update(version="≥ 0.2.2")),
    ("a version field carrying a parenthetical",
     lambda b: _first(b).update(version="7.1.2 (port-version 5)")),
    ("a purl naming something other than its component",
     lambda b: _first(b).update(purl="pkg:generic/other@1.2.3")),
    ("a purl with no pkg: scheme",
     lambda b: _first(b).update(purl="generic/alpha@1.2.3")),
    ("an externalReference type outside the set this tool emits",
     lambda b: _first(b).update(externalReferences=[{"type": "chat",
                                                     "url": "https://x.invalid"}])),
    ("an externalReference with no url",
     lambda b: _first(b).update(externalReferences=[{"type": "website", "url": ""}])),
    ("a component type outside the enum",
     lambda b: _first(b).update(type="widget")),
    ("a scope outside the enum",
     lambda b: _first(b).update(scope="runtime")),
    ("a component with no licences at all",
     lambda b: _first(b).pop("licenses")),
    ("a refused licence surviving into the emitted document",
     lambda b: _first(b).update(licenses=[{"license": {"id": "GPL-3.0-only"}}])),
    ("an empty bom-ref", lambda b: _first(b).update({"bom-ref": ""})),
    ("a component with no name", lambda b: _first(b).pop("name")),
    ("two components sharing a name",
     lambda b: b["components"][1].update(name=_first(b)["name"],
                                         purl=_first(b)["purl"])),
]


def self_test() -> int:
    failures: list[str] = []

    for label, register in MUST_REJECT:
        if not check_register_sbom(register):
            failures.append(f"accepted a register it must reject: {label}")
    for label, register in MUST_ACCEPT:
        found = check_register_sbom(register)
        if found:
            failures.append(f"rejected a valid register ({label}): {found[0]}")

    for raw, expected_version, expected_note in VERSION_CASES:
        version, note = split_version(raw)
        if version != expected_version or note != expected_note:
            failures.append(
                f"split_version({raw!r}) gave ({version!r}, {note!r}), "
                f"expected ({expected_version!r}, {expected_note!r})")

    for url, expected in REFERENCE_CASES:
        found = reference_type(url)
        if found != expected:
            failures.append(f"reference_type({url!r}) gave {found!r}, "
                            f"expected {expected!r}")

    for label, kwargs, expected in PURL_CASES:
        component = kwargs.pop("component")
        version = kwargs.pop("version", None)
        found = purl_for(component, version, **kwargs)
        kwargs["component"], kwargs["version"] = component, version
        if found != expected:
            failures.append(f"purl_for ({label}) gave\n      {found}\n    expected\n"
                            f"      {expected}")

    # The version gate must name every offender and must be silent when there
    # are none — a gate that always fires is as useless as one that never does.
    series = _register([_entry("alpha", version="2.x"),
                       _entry("beta", version="1.2.3"),
                       _entry("solo", version="9.x", vcpkg=[])])
    named = inexact_versions(series)
    if [n for n, _, _ in named] != ["alpha", "solo"]:
        failures.append(f"--require-exact-versions named {named}, "
                        f"expected alpha and solo")
    if [r for _, _, r in named] != [True, False]:
        failures.append(f"--require-exact-versions mislabelled which offenders a "
                        f"resolved graph could fix: {named}")
    if inexact_versions(_register([_entry("alpha", version="1.2.3")])):
        failures.append("--require-exact-versions fired over an all-exact register")
    if inexact_versions(series, {"alpha": ("2.7.1", None, "t")}) != [
            ("solo", "9.x", False)]:
        failures.append("a resolved version did not clear its entry from the "
                        "version gate, so --resolved-graph cannot satisfy it")

    # ---- the --resolved-graph path, through the real parser and classifier ----
    #
    # classify_graph returns (port, record) PAIRS in direct, transitive AND
    # build_only, and bare ports only in `unknown`. Reading build_only as bare
    # ports raised AttributeError on the first real run of --resolved-graph; going
    # through the actual functions here, rather than a hand-made stand-in, is what
    # keeps that shape honest.
    graph_register = _register(
        [_entry("alpha", version="1.x"), _entry("beta", version="4.5.6")],
        transitive=[{"name": "gamma", "spdx": "MIT", "licence_display": "MIT",
                     "pulled_in_by": "alpha"}],
        build_only=[{"name": "helper", "spdx": "MIT", "licence_display": "MIT"}])
    graph_text = "\n".join([
        "The following packages will be built and installed:",
        "  * alpha[core]:x64-linux-test -> 1.2.3#4",
        "  * beta:x64-linux-test -> 4.5.6",
        "  * gamma:x64-linux-test -> 0.9.0#1",
        "  * helper:x64-host-test -> 2024-06-01",
        "  * delta:x64-linux-test -> 1.0.0",
    ])
    graph = gtp().classify_graph(graph_register,
                                gtp().parse_resolved_graph(graph_text))
    if [port.name for port in graph["unknown"]] != ["delta"]:
        failures.append("classify_graph did not isolate the port no register "
                        "entry describes")
    found = resolved_versions(graph)
    expected_map = {"alpha": ("1.2.3", "4", "x64-linux-test"),
                    "beta": ("4.5.6", None, "x64-linux-test"),
                    "gamma": ("0.9.0", "1", "x64-linux-test"),
                    "helper": ("2024-06-01", None, "x64-host-test")}
    if found != expected_map:
        failures.append(f"resolved_versions gave {found}, expected {expected_map}")

    resolved_bom = build_bom(graph_register, pins=_SELF_TEST_PINS,
                             root_version="0.0.0", graph=graph,
                             graph_source="a synthetic graph")
    resolved_by_name = {c["name"]: c for c in resolved_bom["components"]}
    for name, version, fragment in (
            ("alpha", "1.2.3", "port_version=4"),
            ("beta", "4.5.6", "pkg:vcpkg/beta@4.5.6"),
            ("gamma", "0.9.0", "port_version=1"),
            ("helper", "2024-06-01", "triplet=x64-host-test")):
        component = resolved_by_name.get(name)
        if component is None:
            failures.append(f"the resolved-graph document has no {name!r} component")
            continue
        if component.get("version") != version:
            failures.append(f"{name} carries version {component.get('version')!r} in "
                            f"resolved mode, expected {version!r}")
        if fragment not in component["purl"]:
            failures.append(f"{name} purl {component['purl']!r} lacks {fragment!r}")
    if "%23" in json.dumps(resolved_bom):
        failures.append("a vcpkg '#' revision reached a purl: version and "
                        "port-version were pasted together somewhere")
    resolved_document_failures = validate_bom(resolved_bom)
    if resolved_document_failures:
        failures.append(f"the resolved-graph document does not validate: "
                        f"{resolved_document_failures[0]}")
    if inexact_versions(graph_register, resolved_versions(graph)):
        failures.append("the version gate still fired after a graph resolved "
                        "every version it could")

    # A component whose version is a series must carry no `version` field, and
    # must keep the raw string where a reader can still see it.
    component = desktop_component(_entry("alpha", version="3.4x"), "")
    if "version" in component:
        failures.append("a series version reached component.version")
    if not any(p["value"] == "3.4x" for p in component["properties"]):
        failures.append("a series version was dropped instead of kept as a property")

    exact = desktop_component(_entry("alpha", version="7.1.2 (vcpkg port-version 5)"), "")
    if exact.get("version") != "7.1.2":
        failures.append(f"the FFmpeg-shaped version yielded {exact.get('version')!r}")

    # Licence shapes.
    single = licence_field("MIT", "https://spdx.org/licenses/{id}.html")
    if single != [{"license": {"id": "MIT",
                               "url": "https://spdx.org/licenses/MIT.html"}}]:
        failures.append(f"licence_field for a single id gave {single!r}")
    compound = licence_field("LGPL-2.1-or-later OR MPL-1.1", "https://x/{id}")
    if compound != [{"expression": "LGPL-2.1-or-later OR MPL-1.1"}]:
        failures.append(f"licence_field for an expression gave {compound!r}")

    # The document itself.
    valid = _bom(_MUTATION_REGISTER)
    found = validate_bom(valid)
    if found:
        failures.append(f"the unmutated document failed validation: {found[0]}")

    # In direct-only mode the manifest pins the TARGET triplet, so a target port
    # carries it and a host-side build helper must not: nothing here knows which
    # host triplet vcpkg used, and the target one would be a wrong answer rather
    # than a missing one.
    direct_by_name = {c["name"]: c for c in valid["components"]}
    for name, expectation in (("alpha", True), ("gamma", True), ("helper", False)):
        purl = direct_by_name[name]["purl"]
        if ("triplet=" in purl) != expectation:
            failures.append(
                f"{name} purl {purl!r} "
                + ("lacks the target triplet the manifest pins" if expectation else
                   "claims a triplet for a host-side helper this run cannot name"))

    for label, mutate in BOM_MUTATIONS:
        broken = json.loads(json.dumps(valid))
        mutate(broken)
        if not validate_bom(broken):
            failures.append(f"validated a document it must reject: {label}")

    # Determinism: --check is meaningless if two runs disagree.
    if serialise(_bom(_MUTATION_REGISTER)) != serialise(valid):
        failures.append("two runs over the same register produced different documents")
    if "serialNumber" in valid or "timestamp" in valid.get("metadata", {}):
        failures.append("the committed baseline carries a per-run identity, so "
                        "--check could never pass twice")
    stamped = build_bom(_MUTATION_REGISTER, pins=_SELF_TEST_PINS, root_version="9.9.9",
                        timestamp="2026-08-26T00:00:00Z",
                        serial_number="urn:uuid:00000000-0000-4000-8000-000000000000")
    if validate_bom(stamped):
        failures.append("a stamped document failed validation")
    if stamped["metadata"]["timestamp"] != "2026-08-26T00:00:00Z":
        failures.append("--timestamp did not reach the document")

    # `dependsOn: []` asserts "no dependencies"; an absent entry means unknown.
    # gamma's own dependencies are unknown, so gamma must have no entry.
    refs = {edge["ref"] for edge in valid["dependencies"]}
    if "gamma" in refs:
        failures.append("a leaf with unknown dependencies was given a dependsOn, "
                        "which asserts it has none")
    if refs != {ROOT_NAME, "alpha"}:
        failures.append(f"dependency edges are {sorted(refs)}, expected the root "
                        f"and alpha only")

    # An excluded component is in the document but not in the shipped graph.
    excluded_register = _register([_entry("alpha"),
                                   _entry("beta", scope="excluded",
                                          reason="test-only")])
    excluded_bom = _bom(excluded_register)
    root_edge = excluded_bom["dependencies"][0]["dependsOn"]
    if "beta" in root_edge:
        failures.append("an excluded component appears in the application's "
                        "dependency graph")
    if not any(c["name"] == "beta" for c in excluded_bom["components"]):
        failures.append("an excluded component was dropped from the document "
                        "instead of being marked excluded")

    # Diffs.
    bumped = json.loads(json.dumps(valid))
    bumped["components"][1]["version"] = "1.3.2"
    regressed = json.loads(json.dumps(valid))
    regressed["components"][1]["licenses"] = [{"license": {"id": "GPL-3.0-only"}}]
    relicensed = json.loads(json.dumps(valid))
    relicensed["components"][1]["licenses"] = [{"license": {"id": "MIT"}}]
    added = json.loads(json.dumps(valid))
    added["components"].append({"type": "library", "bom-ref": "delta", "name": "delta",
                                "scope": "required",
                                "licenses": [{"license": {"id": "MIT"}}]})
    removed = json.loads(json.dumps(valid))
    removed["components"].pop(1)

    DIFF_CASES = [
        ("an unchanged document", valid, valid, 0, False),
        ("a version bump", valid, bumped, 1, False),
        ("a licence regression to GPL", valid, regressed, 1, True),
        ("an allowed licence change", valid, relicensed, 1, False),
        ("a new component", valid, added, 1, False),
        ("a dropped component", valid, removed, 1, False),
    ]
    for label, old, new, expected_lines, must_fail in DIFF_CASES:
        lines, diff_failures = diff_boms(old, new)
        if len(lines) != expected_lines:
            failures.append(f"diff of {label} reported {len(lines)} line(s), "
                            f"expected {expected_lines}: {lines}")
        if must_fail and not diff_failures:
            failures.append(f"diff of {label} did not fail the gate")
        if not must_fail and diff_failures:
            failures.append(f"diff of {label} failed the gate: {diff_failures[0]}")

    # Vacuity: every table above would also be satisfied by a checker fed nothing.
    # An empty register produces no complaints, which is exactly why the flip
    # below matters — the same entry, one field changed, must change the verdict.
    if check_register_sbom(_register([])):
        failures.append("an empty register produced complaints")
    good = _register([_entry("alpha", scope="optional", reason="off by default")])
    bad = _register([_entry("alpha", scope="optional")])
    if check_register_sbom(good) or len(check_register_sbom(bad)) != 1:
        failures.append("removing one scope_reason did not flip the verdict")

    if failures:
        print(f"gen-sbom self-test: {len(failures)} failure(s)", file=sys.stderr)
        for failure in failures:
            print(f"  · {failure}", file=sys.stderr)
        return 1

    print(f"gen-sbom self-test: {len(MUST_REJECT)} planted register defect(s) caught, "
          f"{len(MUST_ACCEPT)} valid register(s) accepted, "
          f"{len(BOM_MUTATIONS)} planted document defect(s) caught over a control "
          f"that validates")
    print(f"  · {len(VERSION_CASES)} version string(s), "
          f"{len(REFERENCE_CASES)} source URL(s) and {len(PURL_CASES)} purl(s) "
          f"spelled as recorded; {len(DIFF_CASES)} diff case(s), the licence "
          f"regression the only one that fails the gate")
    print("  · the --resolved-graph path exercised through the real vcpkg dry-run "
          "parser and\n    classifier, over all four of its buckets")
    return 0


# ===========================================================================
#  CLI
# ===========================================================================
def read_text_file(path: Path, what: str) -> str:
    if not path.exists():
        raise SbomError(f"{rel(path)} does not exist; it holds {what}")
    return path.read_text(encoding="utf-8").strip()


def repository_url(path: Path) -> str | None:
    """The vcs URL for the root component, from the source-offer ledger.

    Optional on purpose: the ledger is REQ-GEN-020's data, not this tool's, and a
    missing or reshaped file must not stop an SBOM from being produced.
    """
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None
    url = (data.get("offer") or {}).get("repository_url")
    return str(url) if url else None


def load_bom(path: Path) -> dict:
    if not path.exists():
        raise SbomError(f"{rel(path)} does not exist")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SbomError(f"{rel(path)} is not valid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise SbomError(f"{rel(path)} is not a JSON object")
    return data


def run_diff(old_path: Path, new_path: Path) -> int:
    """§25.4 step 4. A missing previous release is a notice, never a pass by
    accident: the condition that makes this start comparing is named out loud."""
    new = load_bom(new_path)
    if not old_path.exists():
        print(f"::notice::No previous SBOM at {rel(old_path)}, so there is nothing "
              f"to diff against. This is the expected state until the first release "
              f"is tagged and §25.5 step 6 attaches its SBOM (REQ-SEC-014).")
        print(f"{rel(new_path)}: {len(new.get('components') or [])} component(s), "
              f"no baseline to compare.")
        return 0
    old = load_bom(old_path)
    lines, failures = diff_boms(old, new)
    print(f"SBOM diff {rel(old_path)} -> {rel(new_path)}")
    if not lines:
        print("  no component, version, licence or scope changes")
    for line in lines:
        print(line)
    if failures:
        print(f"\n{len(failures)} licence failure(s) — §4.1, REQ-GEN-012:\n",
              file=sys.stderr)
        for failure in failures:
            print(f"  · {failure}", file=sys.stderr)
        return 1
    if lines:
        print("\nNo licence left the permitted set, so this diff does not fail the "
              "build. Read the lines above: a version bump is routine, a licence or "
              "scope change is not.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the CycloneDX SBOM from the §4.2 register "
                    "(REQ-GEN-021, REQ-SEC-014).")
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and fail (exit 1) if the file on "
                             "disk is stale")
    parser.add_argument("--stdout", action="store_true",
                        help="write the document to stdout instead of a file")
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"document to write/check (default: {rel(DEFAULT_OUTPUT)})")
    parser.add_argument("--register", type=Path, default=DEFAULT_REGISTER,
                        help=f"register data (default: {rel(DEFAULT_REGISTER)})")
    parser.add_argument("--releases", type=Path, default=DEFAULT_RELEASES,
                        help=f"source-offer ledger, for the repository URL "
                             f"(default: {rel(DEFAULT_RELEASES)})")
    parser.add_argument("--resolved-graph", type=Path, metavar="FILE",
                        help="output of `vcpkg install --dry-run`: supplies exact "
                             "versions and covers the transitive set (OQ-025)")
    parser.add_argument("--require-exact-versions", action="store_true",
                        help="fail if any component's version is a series or a range "
                             "rather than a release (OQ-047)")
    parser.add_argument("--timestamp", metavar="ISO8601",
                        help="stamp metadata.timestamp; omitted from the committed "
                             "baseline so --check stays meaningful")
    parser.add_argument("--serial-number", metavar="URN",
                        help="stamp serialNumber as urn:uuid:...; omitted from the "
                             "committed baseline for the same reason")
    parser.add_argument("--diff", nargs=2, metavar=("OLD", "NEW"), type=Path,
                        help="compare two SBOM documents (§25.4 step 4)")
    parser.add_argument("--self-test", action="store_true",
                        help="plant every defect this tool exists to catch and "
                             "require each to be caught (OQ-045)")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    try:
        if args.diff:
            return run_diff(args.diff[0], args.diff[1])

        if args.check and (args.timestamp or args.serial_number):
            print("FATAL: --check compares against the committed baseline, which "
                  "carries no timestamp or serialNumber by design. Stamping and "
                  "checking in the same run can never agree.", file=sys.stderr)
            return 2
        if args.timestamp and not _TIMESTAMP_RE.match(args.timestamp):
            print(f"FATAL: --timestamp {args.timestamp!r} is not ISO-8601 "
                  f"(e.g. 2026-08-26T12:00:00Z)", file=sys.stderr)
            return 2
        if args.serial_number and not _UUID_RE.match(args.serial_number):
            print(f"FATAL: --serial-number {args.serial_number!r} is not a "
                  f"urn:uuid:... value", file=sys.stderr)
            return 2

        register = gtp().load_register(args.register)
        root_version = read_text_file(VERSION_FILE, "the application version")
        qt_version = (read_text_file(QT_VERSION_FILE, "the pinned Qt version")
                      if QT_VERSION_FILE.exists() else None)
        pins = gtp().manifest_pins(VCPKG_MANIFEST) if VCPKG_MANIFEST.exists() else {}

        graph = None
        graph_source = None
        if args.resolved_graph:
            text = read_text_file(args.resolved_graph, "a vcpkg dry-run graph")
            ports = gtp().parse_resolved_graph(text)
            if not ports:
                print(f"FATAL: no ports parsed out of {rel(args.resolved_graph)}",
                      file=sys.stderr)
                return 2
            graph = gtp().classify_graph(register, ports)
            graph_source = rel(args.resolved_graph)
            if graph["unknown"]:
                print(f"{len(graph['unknown'])} unknown component(s) in "
                      f"{graph_source} — REQ-GEN-012, OQ-025:\n", file=sys.stderr)
                for port in graph["unknown"]:
                    print(f"  · {port.name} {port.version_display()}: described by no "
                          f"§4.2 entry, no transitive reference and no build-only "
                          f"entry", file=sys.stderr)
                return 1
    except gtp().RegisterError as exc:  # noqa: B902 - re-raised as a clean message
        print(f"FATAL: {exc}", file=sys.stderr)
        return 2
    except SbomError as exc:
        print(f"FATAL: {exc}", file=sys.stderr)
        return 2

    # Gate 1: the register must be SBOM-complete.
    register_failures = check_register_sbom(register)
    if register_failures:
        print(f"{len(register_failures)} register failure(s) — REQ-GEN-021, "
              f"REQ-GEN-012:\n", file=sys.stderr)
        for failure in register_failures:
            print(f"  · {failure}\n", file=sys.stderr)
        return 1

    # Gate 2: version precision, only when asked for.
    if args.require_exact_versions:
        inexact = inexact_versions(register, resolved_versions(graph))
        if inexact:
            print(f"{len(inexact)} component(s) have no exact version — OQ-047, "
                  f"REQ-SEC-013:\n", file=sys.stderr)
            for component, raw, resolvable in inexact:
                hint = ("a resolved graph would supply one" if resolvable
                        else "no vcpkg port, so only the register can pin it")
                print(f"  · {component}: {raw!r} is a series or a range, not a "
                      f"release — {hint}", file=sys.stderr)
            if any(resolvable for _, _, resolvable in inexact) and graph is None:
                print("\n  Pass --resolved-graph with the output of `vcpkg install "
                      "--dry-run`\n  to take the versions actually built (OQ-026).",
                      file=sys.stderr)
            else:
                print("\n  Pin the remaining versions in the register (OQ-026).",
                      file=sys.stderr)
            return 1

    bom = build_bom(register, pins=pins, root_version=root_version,
                    qt_version=qt_version, graph=graph, graph_source=graph_source,
                    register_path=rel(args.register),
                    vcs_url=repository_url(args.releases),
                    timestamp=args.timestamp, serial_number=args.serial_number)

    # Gate 3: never emit a document this tool's own reader would reject.
    document_failures = validate_bom(bom)
    if document_failures:
        print(f"{len(document_failures)} structural failure(s) in the document just "
              f"built — this is a bug in tools/gen-sbom.py, not in the register:\n",
              file=sys.stderr)
        for failure in document_failures:
            print(f"  · {failure}", file=sys.stderr)
        return 1

    content = serialise(bom)
    count = len(bom["components"])
    mode = "resolved-graph" if graph is not None else "direct-only"

    if args.stdout:
        sys.stdout.write(content)
        return 0

    if args.check:
        if not args.output.exists():
            print(f"{rel(args.output)} is MISSING — run the generator to create it "
                  f"(REQ-GEN-021, §25.6).", file=sys.stderr)
            return 1
        if args.output.read_text(encoding="utf-8") != content:
            print(f"{rel(args.output)} is STALE — REQ-GEN-021.\n\n"
                  f"It does not match what the generator produces from "
                  f"{rel(args.register)}.\nRegenerate and commit it:\n"
                  f"  python3 tools/gen-sbom.py", file=sys.stderr)
            return 1
        print(f"{rel(args.output)} is up to date ({count} components, mode: {mode}).")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"wrote {rel(args.output)} ({count} components, {len(content.splitlines())} "
          f"lines, mode: {mode}).")
    if mode == "direct-only":
        print("  Transitive ports come from the register's recorded reference set, not "
              "from a\n  resolved graph. Pass --resolved-graph to use the ports "
              "actually built (OQ-025).")
    inexact = inexact_versions(register, resolved_versions(graph))
    if inexact and not args.require_exact_versions:
        print(f"  {len(inexact)} of "
              f"{len(register['platforms']['desktop']['entries'])} components carry no "
              f"exact version, so\n  they carry no `version` field at all (OQ-047): "
              f"{', '.join(name for name, _, _ in inexact)}.")
    print("  No component carries a `cpe`. osv-scanner and trivy match nothing off "
          "a vcpkg\n  or generic purl and grype needs --add-cpes-if-none, so a green "
          "CVE scan over\n  this document is not coverage (OQ-046).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# LGPL Source Offer — Arrow Player

`eclipse-player.md` §27 requires this document, and `REQ-GEN-020` states it verbatim:

> `REQ-GEN-020` `[v1.0]` The project MUST publish a **written source offer** page (`docs/LGPL-SOURCE-OFFER.md`, mirrored on the website) linking the precise source archive for every LGPL component in every release, keyed by release tag. This mirrors AIMP's published practice and is the cheapest way to be unambiguously compliant.

This is that page. It is **generated** by `tools/gen-third-party/gen-third-party.py` from `tools/gen-third-party/register.json` (the §4.2 licence register) and `tools/gen-third-party/releases.json` (the per-tag ledger), and MUST NOT be edited by hand — fix the data or the generator, never the output. The companion document [THIRD-PARTY.md](THIRD-PARTY.md) is generated from the same register, so the two cannot disagree about a version or a source URL.

> **Status: no release has been published.** The per-tag ledger below is therefore empty, and deliberately so: an entry in it asserts that a binary was distributed and that the listed archives correspond to that binary. The offer text, the component list, and the clause mapping are all in force *now* and apply to the first release the moment it exists.

- [The offer](#the-offer)
- [Which components carry the obligation](#which-components-carry-the-obligation)
- [Which clause each obligation is satisfied under](#which-clause-each-obligation-is-satisfied-under)
- [Qt 6 — the right to relink, not merely the source](#qt-6--the-right-to-relink-not-merely-the-source)
- [FFmpeg — the configuration is part of the corresponding source](#ffmpeg--the-configuration-is-part-of-the-corresponding-source)
- [TagLib — a dual licence, and which arm we take](#taglib--a-dual-licence-and-which-arm-we-take)
- [Per-release ledger, keyed by tag](#per-release-ledger-keyed-by-tag)
- [How to obtain the corresponding source today](#how-to-obtain-the-corresponding-source-today)
- [What is not yet in place](#what-is-not-yet-in-place)
- [How this document is generated](#how-this-document-is-generated)
- [See also](#see-also)

## The offer

For every binary release of Arrow Player published at
[https://github.com/maragung/ArrowPlayer/releases](https://github.com/maragung/ArrowPlayer/releases):

1. **The complete corresponding source code for every LGPL component that release ships is available from the same place as the binary, at no charge.** Each release lists, per component, the exact upstream archive it was built from and that archive's SHA-256; the ledger below records them permanently, keyed by tag.
2. **In addition, and as a written offer valid for at least three years from the date of that release**, the same source is available on request to the maintainer, at no charge beyond the cost of performing the distribution, which for a network transfer is nothing.
3. **Nothing in a shipped artifact prevents replacing an LGPL library with your own build of it.** No checksum, signature, or integrity check is applied to any LGPL shared library, and none will be (`REQ-GEN-013`(2),(5)).

**The request channel, stated exactly as it is.** Today the only channel the project can honestly promise to read is the maintainer's GitHub profile, @maragung ([https://github.com/maragung](https://github.com/maragung)), and the repository's issue tracker at [https://github.com/maragung/ArrowPlayer](https://github.com/maragung/ArrowPlayer).

Written requests: Arrow Player Project, c/o @maragung on GitHub (https://github.com/maragung) — written offer valid on the same terms as the GitHub Releases page above, and the source archives are the tagged tarballs attached to each release.. Electronic requests: https://github.com/maragung/ArrowPlayer/issues/new.

This document is a compliance record, not legal advice. Where it names a licence clause it names the clause the project relies on, so that a reader can check the reasoning against the licence text rather than take it on trust.

## Which components carry the obligation

Six of the components in the §4.2 register are distributed under an LGPL arm and therefore carry a source obligation. Versions below are what **this tree pins today**; what a *release* shipped is in the ledger.

| Component | SPDX id | Linkage | Pinned version | Upstream source |
|---|---|---|---|---|
| Qt 6 | [`LGPL-3.0-only`](https://spdx.org/licenses/LGPL-3.0-only.html) | Dynamic, always | 6.8.2 | [https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz](https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz) |
| FFmpeg (libavformat, libavcodec, libavutil, libswresample) | [`LGPL-2.1-or-later`](https://spdx.org/licenses/LGPL-2.1-or-later.html) | Dynamic | 7.1.2 (vcpkg port-version 5) | [https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz](https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz) |
| TagLib | `LGPL-2.1-or-later OR MPL-1.1` | Dynamic | 2.x | [https://github.com/taglib/taglib](https://github.com/taglib/taglib) |
| SoundTouch | [`LGPL-2.1-or-later`](https://spdx.org/licenses/LGPL-2.1-or-later.html) | Dynamic | 2.3.x | [https://www.surina.net/soundtouch/](https://www.surina.net/soundtouch/) |
| Chromaprint | [`LGPL-2.1-or-later`](https://spdx.org/licenses/LGPL-2.1-or-later.html) | Dynamic | 1.5.x | [https://github.com/acoustid/chromaprint](https://github.com/acoustid/chromaprint) |
| projectM | [`LGPL-2.1-or-later`](https://spdx.org/licenses/LGPL-2.1-or-later.html) | Dynamic | 4.x | [https://github.com/projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm) |

Every one of them is **dynamically linked**. That is not incidental: dynamic linking is itself one of the two ways the LGPL lets a combined work be distributed (LGPL-2.1 §6(b), LGPL-3.0 §4(d)(1)), and it is what makes the relinking promise in route 3 of the offer true in practice rather than in principle.

Three of them sit behind a build feature — SoundTouch (`tempo`), Chromaprint (`fingerprint`), projectM (`visualizer`) — all on by default. A release built with one of those features off does not distribute that component, and its ledger row says so explicitly with a reason, rather than linking an archive for something nobody received.

The remaining register entries (BSD, MIT, zlib, public-domain, and the test-only ones) carry attribution duties, not source duties, and are covered in [THIRD-PARTY.md](THIRD-PARTY.md).

## Which clause each obligation is satisfied under

| Licence | Components | How the combined work is distributed | How the source is provided |
|---|---|---|---|
| `LGPL-2.1-or-later` | FFmpeg (libavformat, libavcodec, libavutil, libswresample), TagLib, SoundTouch, Chromaprint, projectM | §6(b) — a suitable shared library mechanism: the library is loaded at run time from a copy on the user's system and works with a modified, interface-compatible build. | §6(d) — equivalent access from the same designated place as the binary; §6(c) — a written offer valid for at least three years, as the fallback. |
| `LGPL-3.0-only` | Qt 6 | §4(d)(1) — the same shared library mechanism, plus §4(b),(c) notices and the full licence text at `licenses/LGPL-3.0.txt`. | GPL-3.0 §6(d) as incorporated — source from the same place; GPL-3.0 §6(b) — a written offer valid for at least three years, as the fallback. |

Two consequences of that table worth stating outright. First, because every LGPL component is a shared library and the source sits beside the binary, the project does not need the written offer to be compliant — it publishes one anyway, which is exactly what `REQ-GEN-020` calls the cheapest way to be unambiguously compliant. Second, the shared-library route imposes a duty on the *build*, not on this document: ship the libraries dynamically, keep them replaceable, and never gate them behind an integrity check. A source offer cannot rescue a statically linked artifact.

## Qt 6 — the right to relink, not merely the source

Qt is `LGPL-3.0-only` — the strictest obligation in the tree, and the one most easily broken by a packaging shortcut. `REQ-GEN-013` fixes five rules, all five of which are conditions on the shipped artifact:

1. Qt libraries are **dynamically linked**. Static Qt is forbidden in every shipped artifact.
2. The user can **replace a Qt shared library** with a compatible build and Arrow Player still runs. No hard-coded checksums or signature checks over Qt binaries.
3. The full LGPL-3.0 text ships at `licenses/LGPL-3.0.txt` in the installed tree and is reachable from **Help → Licences**.
4. This document and [THIRD-PARTY.md](THIRD-PARTY.md) state the exact Qt version and configuration and link the corresponding source.
5. **No anti-tivoization conflict:** the application is not shipped in a form that prevents installing a modified Qt.

- **Exact version:** 6.8.2 (pinned in `desktop/qt-version.txt`; the register names the 6.8 LTS series).
- **Configuration:** the official prebuilt **shared** libraries obtained via `aqtinstall`, not built from source and not from vcpkg ([ADR 0005](adr/0005-qt-acquisition.md) / `REQ-BLD-001`). No Qt configure flags of ours are involved, because we do not configure Qt — which is itself the fact `REQ-GEN-013`(4) needs recorded.
- **Corresponding source:** [https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz](https://download.qt.io/archive/qt/6.8/6.8.2/single/qt-everywhere-src-6.8.2.tar.xz)

Relinking is testable, and that is the point of stating it here: unpack a release, replace the bundled `libQt6Core` with your own interface-compatible build, and the player must start. If it ever does not, that is a licence defect, not a packaging preference, and it should be reported the same way a crash would be.

## FFmpeg — the configuration is part of the corresponding source

FFmpeg can be built LGPL or GPL from the same tree — the licence is a function of the configure line. So the corresponding source for FFmpeg is not just the tarball: it is the tarball **plus** the configuration, and both are recorded here ([ADR 0006](adr/0006-ffmpeg-lgpl.md), `REQ-GEN-014`).

```text
--disable-gpl
--disable-nonfree
--disable-programs
--disable-doc
--disable-encoders
--disable-muxers
--disable-filters
--disable-devices
--disable-network
--enable-shared
--disable-static
```

- **Pinned version:** 7.1.2 (vcpkg port-version 5).
- **Corresponding source:** [https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz](https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz)
- **Assertion, not assurance:** `REQ-GEN-015` requires CI to verify at build time that the linked FFmpeg reports `LGPL` and neither `GPL version` nor `nonfree` via `avutil_license()`, and to fail the build otherwise. A configure flag recorded in a document is a flag that eventually goes wrong; a test that fails the build is a flag that stays right. Stated exactly, because the difference matters for an offer: the step is wired in `desktop-ci.yml`, and no build has yet linked FFmpeg for it to assert against — the adapter arrives in Phase 1. Until then CI fails if an FFmpeg adapter appears without a registered licence case, which is the state this document would otherwise be describing optimistically (OQ-042).
- **Decode only in 1.0.** Encoders are enabled selectively and only for LGPL-clean codecs when the converter lands (`REQ-GEN-016`); `libfdk_aac` is permanently excluded as non-free.

## TagLib — a dual licence, and which arm we take

TagLib is offered under `LGPL-2.1-or-later OR MPL-1.1` — the recipient of *TagLib* may choose either arm. A distributor of a work that links it must say which arm it relies on, because the two impose different duties, and §4.2's own obligation column makes the offer conditional: “Source offer if the LGPL arm is chosen.”

**Arrow Player takes the `LGPL-2.1-or-later` arm.** The source-offer obligation therefore applies to TagLib exactly as it does to FFmpeg, and TagLib appears in the component table above and in every ledger row.

The reason is uniformity rather than preference. Four other components (FFmpeg, SoundTouch, Chromaprint, projectM) are already LGPL-2.1-or-later, so taking that arm for TagLib means the project has **one** obligation model to satisfy and audit — dynamic linking plus source availability — instead of two. MPL-1.1 would add a second, older set of terms with its own source-disclosure and jurisdiction language, for no gain: we ship TagLib unmodified and dynamically linked, which the LGPL arm already permits outright.

## Per-release ledger, keyed by tag

`REQ-GEN-020` asks for the precise archive **per release tag**, not merely per component: a user holding a 1.2.0 binary needs the source that binary was built from, not whatever the tree pins later. This is that record.

**No release has been published, so there is no row here yet.** The current version in `desktop/version.txt` is a pre-release working version; no tag exists, no binary has been distributed, and therefore no source obligation has yet attached to anything. Inventing a row would be the one failure mode this document exists to prevent — a link that looks authoritative and corresponds to nothing.

One entry looks like this, and is appended by the release pipeline (§25.5 step 8) rather than by hand:

```json
{
  "tag": "v1.0.0",
  "date": "2026-01-31",
  "components": [
    {
      "name": "FFmpeg (libavformat, libavcodec, libavutil, libswresample)",
      "spdx": "LGPL-2.1-or-later",
      "version": "7.1.2",
      "upstream_url": "https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz",
      "sha256": "<64 hex digits of the archive actually used>"
    },
    {
      "name": "projectM",
      "shipped": false,
      "reason": "built without the vcpkg 'visualizer' feature; the library is not in the artifact"
    }
  ]
}
```

The generator refuses to emit this document if a release row omits an LGPL component without giving a reason, if a recorded SHA-256 is not 64 hex digits, if a tag is not `vX.Y.Z`, or if a component names no register entry (`REQ-GEN-012`). It also refuses if any release is recorded while `offer.postal_address` is still null, because at that point the written offer names no channel that survives the platform hosting it.

## How to obtain the corresponding source today

Until a release exists, the components above can be obtained exactly as the build obtains them. Two routes, both exact:

**1. The upstream archives** — the URLs in the component table. These are the unmodified upstream releases; Qt and FFmpeg are pinned to a specific archive, the rest to a version series that the vcpkg baseline resolves precisely.

**2. The vcpkg recipe** — for every component except Qt, this is the authoritative one, because it names the patches as well as the source:

```sh
git -C "$VCPKG_ROOT" fetch --all
git -C "$VCPKG_ROOT" checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d
cd desktop
vcpkg install --triplet x64-linux-arrow --x-install-root=vcpkg_installed
```

- The baseline commit `9e593bb18ea69cc5095e012465dcd675a822ed0d` in `desktop/vcpkg.json` is what fixes every port version; without it "from vcpkg" would name a moving target rather than this source.
- Version overrides in force: `ffmpeg 7.1.2#5`.
- After the install, `buildtrees/<port>/src/` holds the extracted upstream source with the port's patches applied — the complete corresponding source in the licence's sense — and `vcpkg_installed/<triplet>/share/<port>/copyright` holds that port's verbatim licence text.

**Qt is not in vcpkg here.** It comes from the official prebuilt shared libraries via `aqtinstall` ([ADR 0005](adr/0005-qt-acquisition.md)), so its corresponding source is the Qt archive linked above, unmodified.

## What is not yet in place

§0.1 rule 2 forbids silently downgrading a requirement, so the parts of `REQ-GEN-020` and its neighbours that are **not** yet satisfied are listed here rather than left to be discovered:

| Not in place | Requirement | Consequence, stated plainly |
|---|---|---|
| No published release, so the per-tag ledger is empty | `REQ-GEN-020` | The per-tag half of the requirement is structurally ready and completely unexercised. Its validator has never seen a real row. |
| No website, so this page is not mirrored anywhere | `REQ-GEN-020` | The repository is the only publication point (OQ-041). The compliance substance — source beside the binary — does not depend on the mirror; the stated MUST does. |
| No postal address, monitored mailbox, or published PGP key | `REQ-GEN-020`, OQ-013 | The written-offer route rests on a platform account (OQ-013). A 1.0.0 release blocker. |
| No `release.yml`, so §25.5 step 8 is not automated | `REQ-BLD-025` | Nothing yet appends a ledger row or fails a stale document at tag time. Until it exists, the ledger would be updated by hand — which is why the freshness gate below runs on every push instead. |
| No artifact signing and no checksums | `REQ-SEC-016`, `REQ-SEC-017` | A recipient cannot yet verify that a downloaded binary is the one these sources correspond to. The SBOM half of this row is now closed — see the row below. |
| The SBOM exists but has never been attached to a release | `REQ-GEN-021`, `REQ-SEC-014` | `docs/sbom/arrow-player.cdx.json` is generated from this same register by `tools/gen-sbom.py`, which `repo-lint.yml` runs `--check` on for every push and pull request. `REQ-SEC-014` asks for one *per release artifact*, which needs §25.5 step 6 and therefore a release that has not happened yet. |
| The Android components (projectM, Chromaprint NDK) are in no build | ADR 0011 | They stay in the register and out of the ledger: they have never been distributed, so no obligation has attached ([ADR 0011](adr/0011-desktop-first-sequencing.md)). |

## How this document is generated

Both this document and [THIRD-PARTY.md](THIRD-PARTY.md) are emitted by `tools/gen-third-party/gen-third-party.py`. Regenerate and check them with:

```sh
python3 tools/gen-third-party/gen-third-party.py --document source-offer
python3 tools/gen-third-party/gen-third-party.py --document source-offer --check
python3 tools/gen-third-party/gen-third-party.py --self-test
```

`--check` regenerates in memory and fails if the committed file differs, which is what makes “kept accurate” mechanical rather than aspirational — the same gate §25.5 step 8 and §25.6 require at release time. The data are `register.json` (versions, SPDX ids, source URLs, linkage — shared with the third-party document) and `releases.json` (the offer's contact facts and the per-tag ledger). Fix those, never this file.

## See also

- [THIRD-PARTY.md](THIRD-PARTY.md) — the full §4.2 register, SPDX ids, patent notes, and the transitive set.
- [ADR 0001](adr/0001-project-license.md) — MPL-2.0 for the core, and why LGPL-only dependencies.
- [ADR 0005](adr/0005-qt-acquisition.md) — Qt via `aqtinstall`, never vcpkg, never static.
- [ADR 0006](adr/0006-ffmpeg-lgpl.md) — the FFmpeg configuration and the build-time licence assertion.
- [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) — OQ-013 (no mailbox or key) and OQ-041 (no website mirror).

---

Generated by `tools/gen-third-party/gen-third-party.py` from `register.json` and `releases.json`. No timestamp is written, so the output is deterministic and `--check` is stable.

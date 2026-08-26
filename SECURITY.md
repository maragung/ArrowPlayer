# Security Policy

Eclipse Player parses untrusted input for a living. Audio containers, tags,
artwork, cue sheets, playlists, skin packages and network responses all come from
outside and none of them are trusted (§21.1). Vulnerability reports are therefore
treated as first-class work, not as an interruption.

## Reporting a vulnerability

**Do not open a public issue for a security vulnerability.**

Use GitHub's private vulnerability reporting:

1. Go to the repository's **Security** tab →
   [**Report a vulnerability**](https://github.com/maragung/ArrowPlayer/security/advisories/new).
2. Describe the issue, the affected version (Help → About shows the exact build),
   and the platform.
3. Include a reproducer if you have one — a crashing input file, a hostile
   `.eclipseskin`, a malformed HTTP response. Attach it as a file rather than
   pasting bytes.

If GitHub private reporting is unavailable to you, contact the maintainer
([@maragung](https://github.com/maragung)) through their GitHub profile and ask
for a private channel. Do not include vulnerability details in a public message.

> **Not yet in place:** a dedicated, monitored security mailbox and a published
> PGP key. Both are release blockers for 1.0.0 and are tracked as `OQ-013` in
> [`docs/OPEN-QUESTIONS.md`](docs/OPEN-QUESTIONS.md). Until then GitHub private
> reporting is the only channel we can honestly promise to read.

## What to expect

The project is in early development and maintained by a very small team, so these
are commitments we can actually keep rather than aspirational figures:

| Stage | Target |
|---|---|
| Acknowledgement that a human has read the report | 5 business days |
| Initial assessment — severity, affected versions, reproduced or not | 14 days |
| Fix or documented mitigation for a high/critical issue | 60 days from assessment |
| Fix for a low/moderate issue | in a subsequent release, no fixed date |

If a deadline is going to be missed, you will be told before it passes rather
than after. If you receive no acknowledgement within 10 days, escalate by
mentioning the maintainer publicly **without any vulnerability detail** — a
"please check your security reports" comment is fine and gives nothing away.

**Disclosure.** Coordinated. We ask for 90 days or until a fix ships, whichever
is sooner. If you intend to publish on a different timeline, tell us in the first
message — an aligned expectation is worth more than a policy nobody agreed to.
Reporters are credited in the advisory and in `CHANGELOG.md` unless they ask not
to be. There is currently **no bug bounty**; we will not pretend otherwise.

## Supported versions

Until 1.0.0 ships, only the latest release and `main` receive fixes. There is no
long-term-support branch yet.

| Version | Supported |
|---|---|
| `main` | ✅ |
| Latest tagged release | ✅ |
| Anything earlier | ❌ — upgrade |

From 1.0.0 (`REQ-BLD-032`): the current minor receives fixes, and the previous
minor receives security-only backports via its `release/1.x` branch until the
minor after next ships.

## Scope

**In scope** — anything reachable from data the user did not write themselves:

- Memory corruption, out-of-bounds access, integer overflow or uncontrolled
  allocation in any parser: containers, codecs, tags, artwork, cue sheets,
  playlists, `.eclipseskin` packages, JSON, SVG, EBNF-driven inputs.
- Escaping the skin sandbox: any way a skin package achieves code execution,
  reads a file outside its own extraction directory, writes anywhere, or makes a
  network request. ADR 0003 states the design goal plainly — a malicious skin
  should at worst produce an ugly UI. A counterexample to that is a serious bug.
- Path traversal or zip-slip in package extraction, playlist import, or the
  converter's output paths (`REQ-THM-018`).
- SQL injection, including through smart-playlist rules. Rules compile to
  **parameterised** SQL; string interpolation is forbidden by `REQ-PLS-010` and
  `REQ-SEC-009`. Any interpolation you find is a vulnerability, not a style
  issue.
- Unexpected outbound network traffic. Every network feature is off by default;
  `REQ-SET-010` and `REQ-TST-023` make "no connections during a full offline
  session" a test. A connection we did not ask for is in scope even if it leaks
  nothing.
- Local privilege or IPC issues: the single-instance channel, CLI argument
  handling, file associations, the update mechanism.
- Signature or integrity failures in release artifacts (`REQ-SEC-016`,
  `REQ-SEC-017`).
- Missing hardening flags in a shipped binary (`REQ-SEC-018`).

**Out of scope:**

- Vulnerabilities in third-party dependencies with no exploitable path through
  Eclipse Player. Report those upstream; tell us too, so we can bump the pin.
- Anything requiring an already-compromised machine or a malicious local
  administrator.
- **Native plugins.** Plugins are explicitly consented, not sandboxed (§16.5). A
  plugin doing something hostile is the documented trust model, not a
  vulnerability. A way to load a plugin *without* consent is in scope.
- Denial of service by feeding the app an absurd file, where the failure is a
  clean rejection or a bounded slowdown. Unbounded allocation or a crash is in
  scope; "a 4 GiB FLAC takes a while to scan" is not.
- Self-XSS or social-engineering scenarios that require the user to paste
  attacker-supplied content into a developer console.
- Missing hardening on a debug or sanitizer build.

## How we try to prevent these in the first place

Listed so you know where to look, and so the claims are checkable rather than
reassuring:

- **Parser hardening is a requirement, not a habit** (`REQ-SEC-002`): validate
  lengths before allocating, reject rather than clamp implausible values, checked
  arithmetic on every offset, a hard input-size cap, and no self-declared length
  believed without bounds-checking it against the bytes actually present.
- **Fuzzing** (§21.6) with libFuzzer targets over every untrusted parser: 60 s
  smoke per target in `desktop-ci.yml`, 15 minutes per target in `security.yml`
  with a persisted corpus.
- **CodeQL** on C++ and Kotlin, plus `clang-tidy` with `bugprone-*`, `cert-*` and
  `cppcoreguidelines-*` (`REQ-SEC-015`).
- **Sanitizers in CI:** an ASan/UBSan suite and a TSan concurrency suite on every
  pull request.
- **Mechanical gates:** SQL-safety, layer-rule, RT-safety, dependency-denylist
  and licence-audit scripts that fail the build (§25.6).
- **A hostile-input corpus in the repository.**
  `shared-spec/conformance/theme-validation-cases/malicious/` holds zip-slip
  paths, XXE and billion-laughs XML, `<script>`-bearing SVGs, hostile checksum
  keys and asset references, each with its expected rejection recorded. If you
  find a hostile input class that is missing, that is a welcome report even
  without a working exploit.
- **Pinned dependencies with an SBOM** (`REQ-SEC-013`, `REQ-SEC-014`):
  [`docs/sbom/eclipse-player.cdx.json`](docs/sbom/eclipse-player.cdx.json), a
  CycloneDX 1.6 document generated from the §4.2 register and re-checked for
  staleness by `repo-lint.yml` on every push and pull request. The **CVE scanning**
  half of `REQ-SEC-014` is not running yet, and would not find much if it were:
  the three common scanners either skip `pkg:vcpkg` components or match them to
  nothing ([OQ-046](docs/OPEN-QUESTIONS.md)). That is written down rather than
  papered over precisely because a scan reporting zero findings over zero coverage
  is worse than no scan.

Three of the items above are **specified but not yet wired**, and saying so is the
point of a checkable list: the 15-minute fuzzing budget, CodeQL, and the CVE and
licence audits all belong to `security.yml` (§25.4), which does not exist in this
repository yet. The 60-second fuzzing smoke, the sanitizer suites, the parser
hardening, the hostile-input corpus and every mechanical gate except
`check-dependency-denylist.py` do run today. `docs/OPEN-QUESTIONS.md` §5 is the
authoritative record of which is which.

None of this means there are no vulnerabilities. It means we would rather find
them mechanically, and that a report which defeats one of these gates is
especially valuable — it tells us the gate was wrong.

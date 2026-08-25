# Contributing to Eclipse Player

Thank you for considering it. This document is the short version; the long
version is [`eclipse-player.md`](eclipse-player.md), which is the specification
every change in this repository answers to.

Two things are worth knowing before you read further:

1. **Requirements come first.** Every behavioural change implements a numbered
   requirement (`REQ-AUD-035`, `REQ-THM-042`, …). If your change does not
   correspond to one, the change is either a refactor or a specification change
   — and a specification change is its own kind of work, described in
   [Changing the specification](#changing-the-specification) below.
2. **The gates are mechanical.** Layer rules, SQL safety, RT safety, licence
   compliance and conformance verdicts are enforced by scripts, not by a
   reviewer's memory. This is on purpose: a policy nobody can enforce is a
   policy nobody follows (§25.6). It also means CI will tell you about most
   mistakes faster and more precisely than a human reviewer would.

## Setup

Desktop build, clean machine, per platform: [`docs/BUILDING.md`](docs/BUILDING.md).
The short form on Linux:

```bash
cd desktop
CXX=g++-12 cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

The build has **no mandatory third-party dependencies**. Qt, FFmpeg, TagLib and
the rest are detected and their features compiled out when absent, so the domain
and DSP layers build and their tests run on a machine with nothing but a
compiler, CMake and Ninja. Do not add a hard dependency to a layer that does not
need one — see [`docs/BUILDING.md`](docs/BUILDING.md#dependency-model).

Before opening a pull request:

```bash
# From the repository root.
python3 tools/check-layers.py          # REQ-GEN-050 / REQ-GEN-051
python3 tools/check-sql-safety.py      # REQ-SEC-009
python3 tools/check-rt-safety.py       # REQ-AUD-017
python3 tools/validate-shared-spec.py  # shared-spec/ schemas and fixtures
```

All four need only Python 3.9 from the standard library — no virtualenv, no
`pip install`. Three of them (`check-layers`, `check-sql-safety`,
`check-rt-safety`) run in `desktop-ci.yml` today; `validate-shared-spec.py`
belongs to `spec-ci.yml` (§25.3), which is not written yet, so for now it is
enforced by you running it, not by CI. Run it.

## Style

| Language | Formatter | Linter |
|---|---|---|
| C++20 | `clang-format` (`.clang-format`) | `clang-tidy` (`.clang-tidy`), `cppcheck` |
| Kotlin | `ktlint` | `detekt` |
| Markdown | — | `markdownlint-cli2` (`.markdownlint.json`) |

Formatting is checked, not suggested: `clang-format --dry-run --Werror` runs in
CI. Run it before you push.

The Markdown gate is `npx markdownlint-cli2` with **no arguments**. The file set
and the exclusions both come from `.markdownlint-cli2.jsonc`; passing your own
globs on the command line adds to that set but cannot escape the exclusions, and
it is how you end up linting a different set than the gate does. Two details are
worth knowing because getting them wrong is silent rather than loud:

- `.markdownlintignore` is **not** read by cli2. Exclusions live in the `ignores`
  array of `.markdownlint-cli2.jsonc`.
- `eclipse-player.md` is excluded on purpose — it is the upstream specification,
  and rewrapping it would churn the line numbers that commit messages, ADRs, and
  code comments cite.

Beyond formatting, three conventions matter more than they look:

- **`/// RT-SAFE:` annotations are load-bearing.** Any function reachable from
  the real-time audio callback carries one, stating why it is safe (no
  allocation, no lock, no syscall, bounded time). `tools/check-rt-safety.py`
  refuses a call from the RT path into a function that lacks one. The annotation
  is the contract; the script is what makes it true.
- **Public symbols are documented, and the doc-comment states thread-safety and
  RT-safety** (§1.3 rule 4). Not "what it does" — a reader can see that — but
  what it is safe to call it from.
- **User-visible strings are externalised for translation** (§12.7). A literal in
  a UI file fails the i18n gate.

## Commit format

[Conventional commits](https://www.conventionalcommits.org/), enforced by
`commitlint` (`commitlint.config.js`) — `REQ-BLD-031`:

```text
<type>(<scope>): <subject>

<body — the reasoning, and the requirement IDs>

Refs: REQ-AUD-035, REQ-AUD-036
```

- **Types:** `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `ci`,
  `chore`, `revert`.
- **Scopes** mirror the requirement areas of §0.2, lowercased: `gen`, `aud`,
  `lib`, `pls`, `efs`, `net`, `syn`, `set`, `tst`, `thm`, `uix`, `key`, `osi`,
  `aut`, `plg`, `sec`, `nfr`, `bld` — plus `spec` for `shared-spec/` and `deps`
  for dependency bumps.
- **Subject** at most 72 characters total for the header line, imperative mood,
  no trailing full stop.
- **A `feat`, `fix`, `perf` or `revert` commit MUST name its requirement ID in
  the body.** This is a lint rule, not a convention (§1.3 rule 10). The reason
  is traceability: when a requirement's behaviour is questioned two years from
  now, `git log --grep=REQ-AUD-035` should answer it.

Write the body for the person who runs `git blame` on a line and wants to know
*why*. Prefer explaining the reasoning and the alternatives you rejected over
restating the diff.

## Definition of Done

Copied from §1.3, because this is the checklist a review actually applies. A
change is done when **all** of these hold:

1. It implements a stated requirement ID, or is refactoring with no behaviour
   change.
2. Unit tests cover the happy path, every documented failure mode, and every
   boundary condition.
3. `clang-format` / `ktlint` clean; `clang-tidy` / `detekt` produce no new
   findings.
4. Public APIs are documented; the doc-comment states thread-safety and
   RT-safety.
5. CI is green on **every** platform in the matrix, not just yours.
6. Any user-visible string is externalised for translation (§12.7).
7. Any new dependency is recorded in §4.2 with its licence, and in the SBOM.
8. Any deviation from the specification is recorded in `docs/adr/`.
9. Performance-sensitive paths (§20) have a benchmark, and it did not regress.
10. The commit message references the requirement ID.

Item 2 deserves emphasis. "Every documented failure mode" means the failure
modes the requirement names, not the ones that were convenient to trigger. If a
requirement says a parser rejects a malformed length field, there is a test with
a malformed length field.

## Review expectations

- **Trunk-based** on `main`, which is protected and always releasable
  (`REQ-BLD-032`). Short-lived feature branches; rebase rather than merge.
- One pull request per unit of work. A PR that implements three requirements is
  three PRs.
- CI must be green on every matrix platform before review, not after
  (§1.3 rule 5). "Green on Linux, will check Windows later" is not ready.
- Expect review comments to cite requirement IDs and specification sections.
  That is not pedantry — it is how a 525-requirement specification stays
  implementable by more than one person.
- **A test is never weakened to make a change pass.** If a conformance fixture
  disagrees with your implementation, either the implementation is wrong or the
  fixture is — and deciding which is a deliberate, documented act, not a diff
  that quietly edits an expectation. See
  [`shared-spec/README.md`](shared-spec/README.md#versioning).

## Changing the specification

`eclipse-player.md` is not immutable, but it is not edited casually either. §0.1
rule 2 is the governing rule: **do not silently downgrade a requirement to make
an implementation pass.**

To add or change a requirement:

1. **Write an ADR** in `docs/adr/`, numbered after the highest existing one, with
   the four sections every ADR here has: context, decision, consequences,
   rejected alternatives. `docs/adr/0001-project-license.md` is the model.
2. **Assign the requirement an ID** in the right area (§0.2) and the right
   MoSCoW tier (§0.3). Use the next free number in that area; never reuse one,
   even for a requirement that was removed — a stale reference that resolves to
   an unrelated requirement is worse than one that resolves to nothing.
3. **State the acceptance criterion.** A requirement nobody can test is a wish.
   If it cannot be mechanically checked, say how it will be checked manually and
   add it to the relevant checklist in `docs/TESTING.md`.
4. **Update the specification and the affected fixtures in the same commit**, so
   the schema/fixture-sync gate (`REQ-BLD-023`) has something to verify.
5. If the change is a correction to an earlier claim, add a row to §29.6, which
   exists so that mistakes are not silently reintroduced.

If you find something in the specification that is simply wrong, or two
requirements that contradict each other, that is a valuable finding — open an
issue. Recorded contradictions live in
[`docs/OPEN-QUESTIONS.md`](docs/OPEN-QUESTIONS.md) as `OQ-001` … `OQ-003`; that
register also holds every narrowing and gap, each with a stable `OQ-NNN` id you
can cite in an issue or a commit body.

## Reporting bugs and security issues

- **Bugs and features:** open an issue using the relevant template. Include
  platform, version (Help → About), and for audio issues the output device and
  sink backend.
- **Security vulnerabilities:** do **not** open a public issue. Follow
  [`SECURITY.md`](SECURITY.md).

## Licence of contributions

Eclipse Player is [MPL-2.0](LICENSE). By contributing you agree that your
contribution is licensed under MPL-2.0. There is no CLA — the licence is the
agreement.

Contributions that would introduce a dependency outside the §4.2 register, or a
GPL-licensed dependency, cannot be accepted: the whole dependency policy exists
so that the licence answer is decidable, and it is enforced by the licence audit
in `security.yml`. If you need a new dependency, propose it as a specification
change (above) so its licence is recorded first.

# Vendored third-party archives

Pinned tarballs only — no floating versions, no network fetch at configure time
(REQ-SEC-013). Each archive's SHA-256 is asserted in `desktop/tests/CMakeLists.txt`.

| Archive | Version | SHA-256 (first 12) | Used by |
|---|---|---|---|
| `googletest-1.15.2.tar.gz` | 1.15.2 | `7b42b4d6ed48` | all test suites |

Do not add anything else here without an ADR.

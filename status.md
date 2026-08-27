# Status pengerjaan — Eclipse Player

Ringkasan apa yang **sudah** ada di repositori ini dan apa yang **belum**, diukur
dari `eclipse-player.md` (525 requirement, 10 fase di §28).

Dokumen ini deskriptif, bukan rencana. Rencana ada di `docs/ROADMAP.md`; hal-hal
yang masih menggantung ada di `docs/OPEN-QUESTIONS.md`.

- Diperbarui: 2026-08-27
- Commit di `main`: 47
- **Belum pernah di-push.** Konsekuensinya penting dan diulang di beberapa tempat
  di bawah: **tidak satu pun dari lima workflow CI pernah dieksekusi.** Semua
  klaim "hijau" di bawah adalah hasil menjalankan gate secara lokal.

---

## 1 · Ringkasan satu tabel

| Fase (§28) | Status | Catatan |
|---|---|---|
| **Fase 0 — Foundation** | **Hampir selesai** | 5 dari 7 exit gate terpenuhi sebagian; rinci di §3 |
| Fase 1 — Playback core | Belum dimulai | Tidak ada satu pun adapter audio; `IDecoder`/`IAudioSink` belum ada |
| Fase 2 — Library | Belum dimulai | Skema SQL sudah **ditulis di spesifikasi**, belum ada kode |
| Fase 3 — UI/UX v1 | Belum dimulai | Termasuk pipeline i18n (§12.7) dan empat tema |
| Fase 4 — OS integration | Belum dimulai | Termasuk CLI, single-instance, safe/portable mode (OQ-054) |
| Fase 5 — Skin engine | Belum dimulai | `tools/theme-validate` masih direktori kosong |
| Fase 6 — Advanced playback | Belum dimulai | |
| Fase 7 — Ecosystem `[v1.x]` | Belum dimulai | |
| Fase 8 — Sync `[v1.x]` | Belum dimulai | |
| Fase 9 — Hardening & 1.0.0 | Belum dimulai | Signing, installer, release pipeline (OQ-053) |

§28 melarang memulai fase N+1 sebelum gate fase N hijau, jadi urutan di atas
bukan pilihan.

**Android dibatalkan dari cakupan** oleh [ADR 0011](docs/adr/0011-desktop-first-sequencing.md):
`android/` dan `android-ci.yml` tidak ada, dan exit gate 2 Fase 0 tidak akan
pernah hijau di repositori ini sampai keputusan itu dicabut.

---

## 2 · Yang sudah jadi

### Kode C++ yang dikompilasi (layer 3 dan 4 dari §7.1)

| Berkas | Isi |
|---|---|
| `desktop/src/core/text.{hpp,cpp}` | UTF-8, sort key, keamanan path |
| `desktop/src/core/error.{hpp,cpp}` | `Result<T>` / `Status`, kode error stabil (§22.1) |
| `desktop/src/core/json/json.{hpp,cpp}` | Parser JSON yang dikeraskan (batas kedalaman, ukuran) |
| `desktop/src/audio/dsp/biquad.{hpp,cpp}` | Koefisien biquad |
| `desktop/src/audio/dsp/equalizer.{hpp,cpp}` | EQ grafis + parametrik |
| `desktop/src/audio/decode/gapless_info.{hpp,cpp}` | Xing/LAME, `iTunSMPB`, `OpusHead`, granule Ogg |
| `desktop/src/app/app_info.{hpp,cpp}` | Identitas build, dibaca dari header versi hasil generate |
| `desktop/src/app/lifecycle.{hpp,cpp}` | Startup berurutan, teardown terbalik |
| `desktop/src/app/application.{hpp,cpp}` | Objek aplikasi + pemetaan exit code |
| `desktop/src/main.cpp` | Composition root, satu-satunya TU yang boleh melihat layer 4 dan 5 |

Semuanya murni C++20 — **tidak ada Qt, tidak ada dependensi pihak ketiga** — dan
karena itu bisa diuji di mesin tanpa Qt.

### Tes: 210 kasus, semuanya lulus

| Binari CTest | Kasus |
|---|---|
| `test_core` | 81 |
| `test_dsp` | 56 |
| `test_gapless` | 52 |
| `test_app` | 17 |
| 4 × `fuzz_corpus.*` (49 seed) | 4 |
| **Total** | **210** |

Dijalankan pada tanggal di atas, dari direktori build bersih:

```text
linux-release: 100% tests passed, 0 tests failed out of 210
linux-asan   : 100% tests passed, 0 tests failed out of 210
linux-tsan   : 100% tests passed, 0 tests failed out of 210
```

TSan hijau **hanya bermakna sebatas kode domain**: belum ada thread di pohon ini,
jadi belum ada concurrency yang diuji. Ini dicatat sebagai OQ-018.

### Build system

- CMake 3.25+, 8 preset: `linux-{debug,release,asan,tsan,fuzz}`,
  `windows-{debug,release}`, `windows-arm64`.
- Warnings-as-errors aktif (`ECLIPSE_WERROR=ON` di preset), hardening flag
  `REQ-SEC-018` terpasang dan **terbukti ada di binari** lewat
  `tools/check-hardening.py` (9 binari diperiksa, termasuk `eclipse-player`).
- Deteksi dependensi bersifat opsional dan jujur: `OFF` berarti pustakanya tidak
  ditemukan, bukan berarti adapternya tidak didukung (OQ-021).
- Manifest vcpkg (`desktop/vcpkg.json`) ada; **belum pernah dipakai membangun**
  (OQ-027).

### Gate yang bisa dijalankan (semua lulus lokal)

| Skrip | Yang ditegakkan |
|---|---|
| `tools/check-layers.py` | §7.2 aturan 1, 2, 3, 4 + `shared-spec` tanpa kode |
| `tools/check-hardening.py` | `REQ-SEC-018` di binari nyata |
| `tools/check-rt-safety.py` | §8.2 daftar larangan di jalur RT |
| `tools/check-sql-safety.py` | Tidak ada SQL yang dirangkai string |
| `tools/check-doc-links.py` | 35 dokumen, 244 tautan internal, 31 deliverable §27, register OQ |
| `tools/check-action-pins.py` | 47 referensi action, semua dipin ke SHA |
| `tools/check-cve-baseline.py` | `REQ-SEC-004` |
| `tools/check-dependency-denylist.py` | §4.2 daftar larangan lisensi |
| `tools/validate-shared-spec.py` | Skema + fixture `shared-spec/` |
| `tools/gen-sbom.py`, `gen-changelog.py`, `gen-third-party/` | SBOM CycloneDX, changelog, `docs/THIRD-PARTY.md` |

Setiap skrip punya `--self-test` dengan kasus **positif dan negatif** — sebuah gate
tanpa uji negatif tidak bisa dibedakan dari gate yang tidak pernah cocok (bentuk
OQ-045 yang dijaga di seluruh pohon ini).

### `shared-spec/` — kontrak lintas platform

`schemas/`, `design-system/`, `grammars/`, `conformance/theme-validation-cases/`
lengkap dan divalidasi. Tidak ada kode terkompilasi di dalamnya (§7.2 aturan 4).

### Dokumentasi

13 dokumen di `docs/` + 11 ADR + `README.md`, `CONTRIBUTING.md`, `SECURITY.md`,
`CODE_OF_CONDUCT.md`, `CHANGELOG.md`, dan `.github/` (5 workflow, template PR,
3 form issue). markdownlint bersih: 33 berkas, 0 masalah.

`docs/OPEN-QUESTIONS.md`: **55 entri** — 24 `Settled`, 22 `Gap`, 9 `Open`.

---

## 3 · Exit gate Fase 0 — satu per satu

| # | Gate | Status |
|---|---|---|
| 1 | `desktop-ci.yml` hijau di 3 platform, **jendela terbuka**, `ctest` jalan | **Belum.** `ctest` jalan (210 lulus) tetapi belum ada jendela, dan workflow belum pernah dieksekusi |
| 2 | `android-ci.yml` hijau | **Dibatalkan** oleh ADR 0011 |
| 3 | `spec-ci.yml` hijau — `theme-schema.json` valid | **Sebagian.** Validasi lulus lokal; workflow belum pernah jalan |
| 4 | Warnings-as-errors; `clang-format` + `ktlint` ditegakkan | **Sebagian.** `-Werror` aktif dan terbukti; `clang-format` **tidak terpasang di mesin ini**, jadi hanya CI yang bisa membuktikannya; `ktlint` N/A |
| 5 | Cache biner vcpkg **dan cache Qt** terbukti bekerja | **Belum.** Lane vcpkg sudah ditulis dengan akuntansi yang gagal saat senyap; **lane Qt belum ditulis** (OQ-026) |
| 6 | Skrip penegak aturan layer ada dan lulus (`REQ-GEN-051`) | **Terpenuhi** (lokal). Aturan 1, 2, 3, 4 ditegakkan; aturan 5 milik Android, di luar cakupan. OQ-031 ditutup; OQ-055 mencatat inversi penomoran §7.1 yang ditemukan saat menutupnya |
| 7 | String versi dari git tampil di **About** | **Sebagian.** String versi digenerate dari git, dibaca satu kali oleh `AppInfo`, dan dicetak oleh binari; **dialog About belum ada** |

Jadi Fase 0 belum boleh ditutup, dan alasannya bukan satu hal besar melainkan
tiga hal kecil: jendela Qt, lane cache Qt, dan dialog About — ditambah satu hal
yang tidak bisa dikerjakan di mesin ini, yaitu menjalankan CI itu sendiri.

---

## 4 · Yang sedang dikerjakan sekarang

| Unit | Isi | Keadaan |
|---|---|---|
| A — layer aplikasi | `src/app/`, `main.cpp`, `test_app` | **Selesai** — commit `f03b5b3` |
| A2 — aturan layer 1 | Peta direktori→layer + assertion include di `check-layers.py` | **Selesai** — commit `1084ac4` |
| B — shell Qt | `desktop/ui/`, dialog About, `.qrc`, `.ts` untuk `en`+`id`, tes UI off-screen | Berjalan; baru `ui/include/eclipse/ui/shell.hpp` yang ada di disk |
| C — lane Qt di CI | Instalasi + cache Qt di `desktop-ci.yml`, akuntansi cold/warm | Belum |

Belum di-commit saat dokumen ini ditulis: hanya `desktop/ui/` (unit B) dan
pembaruan `status.md` ini sendiri.

---

## 5 · Yang belum ada sama sekali

Bukan daftar keinginan — ini bagian spesifikasi yang belum punya kode:

- **Seluruh layer 2 (PORTS)**: `IDecoder`, `IAudioSink`, `ITagReader`,
  `ITagWriter`, `ILibraryIndex`, `IHttpClient`, `IClock`, `IFileSystem`,
  `IMediaSession`, `IPluginHost` — sepuluh antarmuka, nol implementasi.
- **Seluruh layer 1 (ADAPTERS)**: FFmpeg, WASAPI/ALSA/PulseAudio, TagLib,
  SQLite, Qt, SMTC/MPRIS, projectM, Chromaprint.
- **Layer 5 (PRESENTATION)**: satu header, tanpa implementasi.
- **Engine tema, empat tema, engine skin, `tools/theme-validate`.**
- **Pipeline i18n** (`lupdate`/`lrelease`, `.ts`, `.qm`) — Fase 3.
- **Packaging, signing, installer, SBOM release** — Fase 9.
- **`docs/PLUGIN-AUTHORING.md`** — satu-satunya deliverable §27 yang masih absen.
- Suite integrasi, UI, soak, dan chaos (`REQ-TST-023`…`026`).

---

## 6 · Batasan lingkungan yang membentuk semua di atas

Ini bukan keluhan; ini yang menentukan apa yang bisa dibuktikan di sini.

| Alat | Ada? | Akibatnya |
|---|---|---|
| **Qt (versi apa pun)** | **Tidak** | Kode Qt apa pun yang ditulis di sini **belum pernah dikompilasi**; CI yang akan pertama kali mengompilasinya (OQ-017) |
| `clang-format`, `clang-tidy`, `cppcheck` | Tidak | Gate format/lint hanya bisa dibuktikan di CI |
| `vcpkg`, `libsamplerate`, FFmpeg, TagLib, ALSA | Tidak | Adapter terkait `OFF`, bukan "tidak didukung" |
| `sudo` tanpa password, `pip`/venv | Tidak | Tidak ada instalasi paket sistem; gate ditulis dengan pustaka standar saja (`tools/jsonschema_mini.py` lahir dari batasan ini) |
| CMake, Ninja, GCC 14, Python 3.13, Node 26, git | Ya | Semua yang diklaim lulus di atas, lulus lewat alat-alat ini |

Aturan yang dipegang: **sesuatu yang belum pernah dijalankan tidak pernah
dilaporkan hijau.** Setiap celah verifikasi di atas punya entri di
`docs/OPEN-QUESTIONS.md`, karena §0.1 aturan 2 melarang menurunkan requirement
secara diam-diam, dan celah yang tidak dicatat adalah tepat bentuk diam itu.

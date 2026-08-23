# Building Eclipse Player — Desktop

Covers Windows 10/11 and Ubuntu 22.04 / 24.04 LTS. See `../eclipse-player.md`
§3 for the full support matrix and §24 for the toolchain requirements.

## Requirements

| Component | Minimum | Why |
|---|---|---|
| C++ compiler | GCC 12, Clang 16, or MSVC 19.40 (VS 2022 17.10) | C++20. **Ubuntu 22.04 defaults to GCC 11, whose C++20 support is incomplete — you must install and select `g++-12`.** |
| CMake | 3.28 | `CMakePresets` v6 |
| Ninja | 1.11 | default generator |
| Python | 3.9 | architecture gate scripts in `tools/` |

Everything else is **optional**. This is deliberate: see
[Dependency model](#dependency-model).

## Quick start

```bash
# Linux
sudo apt-get install -y build-essential g++-12 cmake ninja-build pkg-config \
                        libsqlite3-dev zlib1g-dev

cd desktop
CXX=g++-12 cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

```powershell
# Windows, from a Developer PowerShell for VS 2022
choco install cmake ninja
cd desktop
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

## Presets

| Preset | Purpose |
|---|---|
| `linux-debug` / `windows-debug` | day-to-day development |
| `linux-release` / `windows-release` | optimised, `-Werror` on |
| `linux-asan` | AddressSanitizer + UndefinedBehaviorSanitizer (§23.5) |
| `linux-tsan` | ThreadSanitizer, for the real-time audio path (REQ-AUD-018) |
| `windows-arm64` | Windows on ARM release artifact (REQ-GEN-001) |

Run the sanitizer presets before touching anything in `src/audio/`. The audio
callback has hard real-time constraints (§8.2.3) and a data race there is a
dropout, not a theoretical concern.

## Dependency model

Every external library is **optional at configure time**. The tree always
configures, and adapters whose library is absent are simply not built. Configure
prints exactly what was found:

```
  --- adapters (dependency-gated) ---
  SQLite3       : ON
  FFmpeg        : OFF
  TagLib        : OFF
  ALSA          : OFF
  Qt 6          : OFF
```

This is a structural consequence of the architecture, not a convenience hack.
§7.2 (REQ-GEN-050) requires the **domain layer to link against nothing but the
standard library** — and the domain layer is where the audio maths, the gapless
parsers, the format-string engine, the smart-playlist compiler and the theme
validators all live. That is the majority of the interesting logic, and it builds
and tests anywhere a C++20 compiler exists.

`tools/check-layers.py` enforces this mechanically in CI, so it cannot decay.

### Optional dependencies

```bash
# Ubuntu — audio decode, tagging, output backends, resampling
sudo apt-get install -y \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libtag1-dev libasound2-dev libpulse-dev libsamplerate0-dev
```

**FFmpeg licence constraint.** A shipped build must use an FFmpeg configured
**without** `--enable-gpl` and **without** `--enable-nonfree` (§4.4,
REQ-GEN-014). Your distribution's FFmpeg may be either; CI asserts the linked
build reports LGPL at runtime (REQ-GEN-015). For release artifacts, build FFmpeg
from source with the flag set in §4.4.

## Qt

Qt is **not** obtained through vcpkg. Building Qt from source per platform per
cache miss costs hours and is fragile, and Ubuntu 22.04 ships only Qt 6.2.4,
below what the QML surfaces need. Instead, Qt comes from official prebuilt
binaries at the version pinned in `qt-version.txt`
(see [ADR 0005](adr/0005-qt-acquisition.md)):

```bash
pip install "aqtinstall==3.*"
aqt install-qt linux desktop "$(cat qt-version.txt)" gcc_64 \
    -m qtdeclarative qtsvg qttools qtshadertools qtwayland -O "$HOME/Qt"

export CMAKE_PREFIX_PATH="$HOME/Qt/$(cat qt-version.txt)/gcc_64"
cmake --preset linux-release
```

Qt **must be dynamically linked**. `cmake/EclipseDependencies.cmake` fails the
configure step on a static Qt, because LGPL-3.0 requires that users be able to
replace it (REQ-GEN-013). This is a licence obligation, not a preference.

Without Qt, everything except the UI still builds and all domain tests run.

## Optional components

```bash
# ASIO (Windows). The Steinberg SDK is NOT redistributable (§4.6), so you must
# obtain it yourself and accept its licence.
cmake --preset windows-release -DECLIPSE_ENABLE_ASIO=ON -DASIO_SDK_DIR=C:/asiosdk

# RtAudio fallback sink. Off by default and NOT bit-perfect capable — see
# ADR 0002 for why it is a fallback rather than the primary output path.
cmake --preset linux-release -DECLIPSE_ENABLE_RTAUDIO=ON
```

## Architecture gates

CI runs these before compiling anything, and you can run them locally in about a
second. They exist because §7.2, §21.5 and §8.2.3 define rules that reviewer
discipline does not reliably enforce.

```bash
python3 tools/check-layers.py      # layer dependency direction (REQ-GEN-051)
python3 tools/check-sql-safety.py  # no interpolated SQL      (REQ-SEC-009)
python3 tools/check-rt-safety.py   # RT-SAFE claims are true  (REQ-AUD-017)
```

## Building without network access

`desktop/third_party/googletest-1.15.2.tar.gz` is vendored as a pinned tarball
with a checked SHA-256, rather than fetched at configure time (REQ-SEC-013: no
floating versions). A clean build needs no network.

## Troubleshooting

**`error: 'std::ranges' has not been declared` on Ubuntu 22.04** — you are on
GCC 11. Install `g++-12` and set `CXX=g++-12`.

**`Qt is statically linked` fatal error at configure** — intentional
(REQ-GEN-013). Use a shared Qt build.

**A test binary is `NOT_BUILT` in CTest output** — a test source references a
module that is not yet implemented, or an adapter whose dependency is missing.
Check the configure summary.

**`-Wnull-dereference` errors only at `-O3`** — GCC's analysis is more aggressive
in release. Prefer returning values over pointers from small accessors; that is
why `BiquadCascade::coeffs()` returns by value.

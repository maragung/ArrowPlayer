# Building Arrow Player — Desktop

Covers Windows 10/11 and Ubuntu 22.04 / 24.04 LTS. See `../arrow-player.md`
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

```text
  deps from     : system / pkg-config
  --- adapters (dependency-gated) ---
  SQLite3       : ON
  FFmpeg        : OFF
  TagLib        : OFF
  ALSA          : OFF
  libsamplerate : OFF
  Qt 6          : OFF
  note          : OFF means the library was not found, not that the
                  adapter is unsupported here. With a user-local prefix,
                  export PKG_CONFIG_PATH — see docs/BUILDING.md.
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

### Building the dependencies into a user-local prefix

If you cannot install system packages — no root, a locked-down machine, or you
simply want the exact decode-only FFmpeg the project targets — build into
`~/.local` and point the build at it. This is a supported path, not a workaround;
it is how the project is developed.

```bash
# FFmpeg 7.1 — LGPL, decode-oriented, no network transports at all.
# This is the DEVELOPMENT configuration. It satisfies the licence half of
# REQ-GEN-014 (no --enable-gpl, no --enable-nonfree, shared, no programs, no
# doc, no devices, no network) but deliberately keeps a handful of encoders,
# muxers and filters that REQ-GEN-014 forbids in a *shipped* build, because
# the gapless corpus has to be encoded by something. See the note below.
./configure --prefix="$HOME/.local" --enable-shared --disable-static \
  --disable-everything --disable-doc --disable-debug --disable-network \
  --disable-avdevice --disable-swscale --disable-postproc \
  --disable-ffplay --disable-ffprobe \
  --enable-avcodec --enable-avformat --enable-avutil --enable-swresample \
  --enable-avfilter --enable-protocol='file,pipe' \
  --enable-decoder='mp3,mp3float,flac,alac,aac,aac_latm,vorbis,opus,wavpack,ape,mpc7,mpc8,wmav1,wmav2,wmapro,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_s16be,pcm_s24be,pcm_s32be,pcm_u8,pcm_s8' \
  --enable-demuxer='mp3,flac,wav,w64,aiff,aac,mov,ogg,wv,ape,mpc,mpc8,asf,caf,matroska,pcm_s16le' \
  --enable-parser='mpegaudio,flac,aac,aac_latm,vorbis,opus' \
  --enable-filter='aresample,aformat,anull,atrim,volume' \
  --enable-libmp3lame \
  --enable-encoder='flac,alac,wavpack,vorbis,opus,libmp3lame,pcm_s16le,pcm_s24le,pcm_f32le' \
  --enable-muxer='wav,flac,ogg,mp3,wv,caf,aiff,mov,mp4,ipod'
```

Two flags in there deserve an explanation, because both look like scope creep:

- **`--disable-network` and `--enable-protocol='file,pipe'`.** A player that
  reads local files does not need FFmpeg's HTTP, RTMP or TLS transports, and
  §19.5 makes the zero-connection default structural rather than a policy. This
  removes the capability instead of declining to use it.
- **`--enable-libmp3lame` and the encoder list.** These exist to **generate the
  test corpus**, not to ship. §8.11 test 3 proves gapless playback by comparing
  decoded output byte-for-byte across a track boundary, which means something has
  to encode the boundary in the first place — and MP3 needs LAME. `libmp3lame` is
  LGPL-2.1 and `REQ-GEN-016` permits it as a separately-enabled component.
  **`libfdk_aac` is not permitted** under any circumstances: it is non-free.

TagLib and alsa-lib build into the same prefix with their usual CMake and
autotools invocations.

**Then tell the build where to look.** `ArrowDependencies.cmake` finds FFmpeg,
TagLib, ALSA and libsamplerate through `pkg-config`, which does not search a
user-local prefix by default:

```bash
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
```

Without those two lines the configure summary prints `OFF` for every adapter even
though all of them are installed — which reads as "this machine cannot build the
adapter" when it actually means "pkg-config was not told where to look". The
presets deliberately do **not** hardcode a prefix, because a committed preset
that assumes one contributor's home directory changes dependency resolution for
everyone else. See `OQ-021` in
[`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md).

## Qt

Qt is **not** obtained through vcpkg. Building Qt from source per platform per
cache miss costs hours and is fragile, and Ubuntu 22.04 ships only Qt 6.2.4,
below what the QML surfaces need. Instead, Qt comes from official prebuilt
binaries at the version pinned in `qt-version.txt`
(see [ADR 0005](adr/0005-qt-acquisition.md)):

```bash
pip install "aqtinstall==3.*"
aqt install-qt linux desktop "$(cat qt-version.txt)" linux_gcc_64 \
    -m qtshadertools -O "$HOME/Qt"

export CMAKE_PREFIX_PATH="$HOME/Qt/$(cat qt-version.txt)/gcc_64"
cmake --preset linux-release
```

Two notes on that command, both learned from the first CI run. Qt's online
repository restructured: from 6.8 the base package already contains Qt
Declarative, Qt Svg and Qt Tools, so the only add-on this build needs is
`qtshadertools`, and asking for the old module names fails with "packages were
not found while parsing XML". And aqt 3.x renames the Linux architecture to
`linux_gcc_64` — the plain `gcc_64` no longer resolves as an argument — but the
directory the packages extract into is still `gcc_64`, so `CMAKE_PREFIX_PATH`
must use that name. On Windows the same split applies: the argument is
`win64_msvc2022_64` and the extracted directory is `msvc2022_64`.

Qt **must be dynamically linked**. `cmake/ArrowDependencies.cmake` fails the
configure step on a static Qt, because LGPL-3.0 requires that users be able to
replace it (REQ-GEN-013). This is a licence obligation, not a preference.

Without Qt, everything except the UI still builds and all domain tests run.

## vcpkg

Everything that is not Qt comes from vcpkg in manifest mode, with the registry
baseline pinned in `desktop/vcpkg.json` (§6.2, [ADR 0005](adr/0005-qt-acquisition.md);
REQ-SEC-013 forbids floating versions anywhere):

```bash
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"

cd desktop
cmake --preset linux-release -DVCPKG_TARGET_TRIPLET=x64-linux-arrow
```

`VCPKG_ROOT` is what switches vcpkg on. `desktop/CMakeLists.txt` picks up the
toolchain from that variable and nothing else, so the tree keeps configuring
against system packages or a user-local prefix when vcpkg is not installed — an
explicit `-DCMAKE_TOOLCHAIN_FILE=...` always wins over both.

### Why the triplet is not optional

The `-arrow` triplets in `desktop/cmake/triplets/` are the Linkage column of
the §4.2 dependency register expressed as build configuration:

| Ports | Linkage | Why |
|---|---|---|
| `ffmpeg`, `taglib`, `soundtouch`, `chromaprint`, `projectm` | dynamic | LGPL-2.1-or-later. §4.3 rule 2 requires the user be able to swap the shared library for a compatible build and still run Arrow Player. |
| `rtaudio` | dynamic | Permissive, but the register records it as dynamic, and the build follows the register. |
| everything else | static | `sqlite3`, `libsamplerate`, `libzip`, `zlib`, `gtest` — public domain, BSD or zlib licensed, "Static OK" in the register. |

Stock vcpkg triplets build every port with one linkage. On Linux that is
`static`, which would statically link FFmpeg and TagLib into a shipped
binary — exactly what REQ-GEN-013 forbids. Configure emits a warning if vcpkg is
active with a triplet whose name does not end in `-arrow`, because a licence
violation should not be something you find out about in a review.

Four triplets exist, matching the §3.1 target matrix: `x64-linux-arrow`,
`arm64-linux-arrow`, `x64-windows-arrow`, `arm64-windows-arrow`.

### Two manifest choices that are requirements, not taste

- **`ffmpeg` is pinned to 7.1.2 by an `overrides` entry.** §4.2 registers FFmpeg
  7.1.x and §6.3 pins "FFmpeg 7.1"; the baseline's own default is newer. An
  explicit pin is the only way both statements stay true. Its features are
  `avcodec`, `avformat`, `swresample`, `zlib` — no `avdevice`, no `swscale`, no
  `gpl`, no `nonfree`, no `fdk-aac`.
- **`libzip` is requested with `"default-features": false`.** The defaults pull
  in bzip2 and, on Linux, OpenSSL for AES. `REQ-THM-015` says a skin archive is
  "deflate or stored; **no** encryption, **no** other compression methods", so
  building support for bzip2 and AES would be building a code path the format
  forbids — and it would put OpenSSL in the dependency graph, where §4.2 has no
  row for it.

### Binary caching

REQ-BLD-022 makes binary caching mandatory rather than optional; without it
every CI job rebuilds every dependency. Locally, vcpkg caches to
`~/.cache/vcpkg/archives` by default and needs no configuration. CI sets a
`files` provider under the runner temp and persists it with `actions/cache` —
not `x-gha`, which the pinned vcpkg-tool release has removed (OQ-026).

## Optional components

```bash
# ASIO (Windows). The Steinberg SDK is NOT redistributable (§4.6), so you must
# obtain it yourself and accept its licence.
cmake --preset windows-release -DARROW_ENABLE_ASIO=ON -DASIO_SDK_DIR=C:/asiosdk

# RtAudio fallback sink. Off by default and NOT bit-perfect capable — see
# ADR 0002 for why it is a fallback rather than the primary output path.
cmake --preset linux-release -DARROW_ENABLE_RTAUDIO=ON
```

## Architecture gates

CI runs these before compiling anything, and you can run them locally in about a
second. They exist because §7.2, §21.5 and §8.2.3 define rules that reviewer
discipline does not reliably enforce.

```bash
python3 tools/check-layers.py      # layer dependency direction (REQ-GEN-051)
python3 tools/check-sql-safety.py  # no interpolated SQL      (REQ-SEC-009)
python3 tools/check-rt-safety.py   # RT-SAFE claims are true  (REQ-AUD-017)
python3 tools/check-hardening.py build/linux-release  # REQ-SEC-018, in the binary
```

The last one needs a build to have happened; the other three read source. It
also has a `--self-test` that runs without one.

## Android

[ADR 0012](adr/0012-restore-android.md) restored the Android target; the app
lives in `android/` and is a self-contained Gradle build that never imports
desktop code (§5). It is a Phase 0 scaffold — one Compose module, an About
screen — not yet the full §5 module list.

Requirements: JDK 21, Android SDK with platform 35, Gradle 8.9 (the wrapper
properties pin the same version; generate the wrapper jar with `gradle wrapper`
if you do not have it — CI uses `gradle/actions/setup-gradle` instead).

```bash
cd android
gradle ktlintCheck detekt assembleDebug testDebugUnitTest
# APK: app/build/outputs/apk/debug/app-debug.apk
```

Version: `android/gradle.properties`'s `arrow.version` is the single
Android-side source (same-commit rule with `desktop/version.txt`, REQ-LIB-001);
release CI stamps the tag version. All dependency versions live in
`android/gradle/libs.versions.toml` — no inline versions anywhere (§5).

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

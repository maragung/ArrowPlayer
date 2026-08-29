cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
#  vcpkg — spec §6.2 / ADR 0005. Manifest mode; the baseline is pinned in
#  vcpkg.json and the project triplets in cmake/triplets/ encode the §4.2
#  linkage rules. Qt is NOT in the manifest and never will be (§6.2).
#
#  This is opt-in by environment rather than hardcoded in the presets: the tree
#  must still configure on a machine whose dependencies come from the system
#  package manager or a user-local prefix (docs/BUILDING.md). An explicitly
#  supplied CMAKE_TOOLCHAIN_FILE always wins.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file (§6.2)")
endif()

# ---------------------------------------------------------------------------
#  Arrow Player — Desktop (Windows + Linux)
#  Spec: eclipse-player.md  §5 repo layout, §7 architecture, §24 build
# ---------------------------------------------------------------------------

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" ARROW_VERSION_RAW)
string(STRIP "${ARROW_VERSION_RAW}" ARROW_VERSION)

project(ArrowPlayer
    VERSION   ${ARROW_VERSION}
    LANGUAGES CXX
    DESCRIPTION "Free, open-source, privacy-first music player"
    HOMEPAGE_URL "https://arrow-player.org")

# REQ-GEN-001 / §3.2 — C++20, no C++23 features.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 3.28+ turns C++20 module scanning on by default with the Ninja
# generator, which then injects `-fmodules-ts -fmodule-mapper=...modmap` into
# every compile command. This project compiles no C++ modules, but clang-tidy
# reads that compile database and fails on the mapper path ("no such file or
# directory: '@...o.modmap'") — so the scanning has a real cost and no use.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
#  Options
# ---------------------------------------------------------------------------
option(ARROW_BUILD_TESTS      "Build the test suites"                ON)
option(ARROW_BUILD_FUZZERS    "Build libFuzzer targets (§21.6)"      OFF)
option(ARROW_BUILD_BENCH      "Build benchmarks (§20)"               OFF)
option(ARROW_BUILD_UI         "Build the Qt UI (requires Qt 6.8+)"   ON)
option(ARROW_ENABLE_ASIO      "ASIO output — SDK user-supplied (§4.6)" OFF)
option(ARROW_ENABLE_RTAUDIO   "RtAudio fallback sink (§8.7.7)"       OFF)
option(ARROW_WERROR           "Treat warnings as errors (§24.1)"     OFF)
option(ARROW_SANITIZE_ADDRESS "ASan + UBSan"                         OFF)
option(ARROW_SANITIZE_THREAD  "ThreadSanitizer"                      OFF)

include(ArrowWarnings)
include(ArrowSanitizers)
include(ArrowDependencies)

# ---------------------------------------------------------------------------
#  Generated version header  (REQ-BLD-007)
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(ARROW_GIT_SHA "unknown")
set(ARROW_GIT_DIRTY 0)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/../.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." OUTPUT_VARIABLE ARROW_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." RESULT_VARIABLE ARROW_GIT_DIRTY ERROR_QUIET)
endif()
configure_file(include/arrow/version.hpp.in
               "${CMAKE_BINARY_DIR}/generated/arrow/version.hpp" @ONLY)

# ---------------------------------------------------------------------------
#  Layer 3 — DOMAIN.  Pure C++20.  REQ-GEN-050(2): links against nothing but
#  the standard library.  Enforced by tools/check-layers.py in CI.
# ---------------------------------------------------------------------------
#  Absolute paths, and a variable rather than a literal list, because
#  tests/fuzz/ recompiles these same translation units with coverage
#  instrumentation for libFuzzer (a fuzzer linked against an uninstrumented
#  library is a fuzzer that mutates blindly). One list, one place to add a file.
set(ARROW_DOMAIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/text.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/json/json.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/evaluator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/format_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/track_context.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/biquad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/equalizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/replaygain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/gapless_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/playback_graph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/ring_buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/gapless_scheduler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/dsp_chain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/limiter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/fader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/playback_service.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/icy_metadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/hls_playlist.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/rss_feed.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/http_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/radio.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/podcast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/scrobble.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/discovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/pairing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/transport.cpp"
)

# ---------------------------------------------------------------------------
#  REQ-GEN-030: FileSets for public headers
# ---------------------------------------------------------------------------
# Domain layer public headers
set(ARROW_DOMAIN_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/error.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/text.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/json/json.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/parser.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/evaluator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/format_engine.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/format/track_context.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/biquad.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/equalizer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/replaygain.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/gapless_info.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/wav_decoder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/playback_graph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/ring_buffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/spsc_ring.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/playback_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ports/audio_ports.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ports/audio_types.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/icy_metadata.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/hls_playlist.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/http_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/rss_feed.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/radio_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/podcast_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/ports/scrobble_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/ports/discovery_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/ports/pairing_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sync/ports/transport_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ports/library_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ports/tag_port.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/app_info.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/command_line.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/application.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/lifecycle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_database.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library/smart_playlist.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library/filesystem_scanner.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library/sidecar_tag_reader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_importer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/theme/schema.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/theme/chain.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/theme/loader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/skin/layout.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/skin/svg_sanitize.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/skin/pack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/skin/zip_safety.hpp"
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.23")
    # CMake 3.23+ supports FileSets natively
    add_library(arrow-domain STATIC ${ARROW_DOMAIN_SOURCES})
    target_include_directories(arrow-domain PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>)
    target_sources(arrow-domain PUBLIC FILE_SET HEADERS FILES ${ARROW_DOMAIN_HEADERS})
else()
    add_library(arrow-domain STATIC ${ARROW_DOMAIN_SOURCES})
    target_include_directories(arrow-domain PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}/generated")
endif()

# Playback ports and the SPSC ring are header-only contracts/primitives. They
# are compiled by the domain consumers and intentionally require no adapter.
arrow_set_warnings(arrow-domain)
arrow_set_sanitizers(arrow-domain)
arrow_set_hardening(arrow-domain)   # REQ-SEC-018
target_compile_features(arrow-domain PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(arrow-domain PUBLIC Threads::Threads)
# REQ-NET-002: the libcurl-backed IHttpClient is the production transport.
# When libcurl is found at configure time, link the domain library against
# it; the http_client.cpp source then compiles the real libcurl path.
# Otherwise it compiles a stub that returns ErrorCode::NotImplemented. The
# link rule is private because the libcurl dependency is an implementation
# detail of one TU; the public port surface stays clean.
if(ARROW_HAVE_CURL)
    target_link_libraries(arrow-domain PRIVATE arrow::curl)
    target_compile_definitions(arrow-domain PRIVATE ARROW_HAVE_CURL=1)
endif()

# ---------------------------------------------------------------------------
#  Layer 1 — ADAPTERS, each gated on dependency availability so the tree
#  always configures.  §7.2: reachable only through layer-2 ports.
# ---------------------------------------------------------------------------
add_library(arrow-adapters INTERFACE)
add_library(arrow-audio-null STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/null_sink.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
target_include_directories(arrow-audio-null PUBLIC src)
target_link_libraries(arrow-audio-null PUBLIC arrow-domain)
arrow_set_warnings(arrow-audio-null)
arrow_set_sanitizers(arrow-audio-null)
arrow_set_hardening(arrow-audio-null)
target_compile_features(arrow-audio-null PUBLIC cxx_std_20)

add_library(arrow-audio-wav STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/wav_decoder.cpp")
target_include_directories(arrow-audio-wav PUBLIC src)
target_link_libraries(arrow-audio-wav PUBLIC arrow-domain)
arrow_set_warnings(arrow-audio-wav)
arrow_set_sanitizers(arrow-audio-wav)
arrow_set_hardening(arrow-audio-wav)
target_compile_features(arrow-audio-wav PUBLIC cxx_std_20)

# FFmpeg-backed decoder adapter (REQ-AUD-027). Built only when FFmpeg is available.
# The ffmpeg_decoder.cpp is a single translation unit; ARROW_HAVE_FFMPEG is
# used inside the .cpp to compile the real implementation or the stub.
if(ARROW_HAVE_FFMPEG)
    add_library(arrow-audio-ffmpeg STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/ffmpeg_decoder.cpp")
    target_compile_definitions(arrow-audio-ffmpeg PRIVATE ARROW_HAVE_FFMPEG=1)
    target_include_directories(arrow-audio-ffmpeg PUBLIC src)
    target_link_libraries(arrow-audio-ffmpeg PUBLIC arrow-domain arrow::ffmpeg)
    arrow_set_warnings(arrow-audio-ffmpeg)
    arrow_set_sanitizers(arrow-audio-ffmpeg)
    arrow_set_hardening(arrow-audio-ffmpeg)
    target_compile_features(arrow-audio-ffmpeg PUBLIC cxx_std_20)
    target_link_libraries(arrow-adapters INTERFACE arrow-audio-ffmpeg)
endif()

if(ARROW_HAVE_ALSA)
    # ALSA is on Linux only. The factory dispatches to AlsaSink at runtime,
    # so its compilation unit must live in the same library as alsa_sink.cpp
    # to resolve the vtable. We compile the factory twice — once in
    # arrow-audio-null (always) and once in arrow-audio-alsa (when ALSA
    # is on, with the AlsaSink vtable available) — and the executable links
    # the variant that matches the current platform. The ALSA library still
    # links against arrow-audio-null because the factory references the
    # NullSink vtable on the Automatic-preference fallback path.
    target_compile_definitions(arrow-audio-null PRIVATE ARROW_HAVE_ALSA=1)
    add_library(arrow-audio-alsa STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/alsa_sink.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
    target_compile_definitions(arrow-audio-alsa PRIVATE ARROW_HAVE_ALSA=1)
    target_include_directories(arrow-audio-alsa PUBLIC src)
    target_link_libraries(arrow-audio-alsa PUBLIC arrow-domain arrow-audio-null arrow::alsa)
    arrow_set_warnings(arrow-audio-alsa)
    arrow_set_sanitizers(arrow-audio-alsa)
    arrow_set_hardening(arrow-audio-alsa)
    target_compile_features(arrow-audio-alsa PUBLIC cxx_std_20)
    target_link_libraries(arrow-adapters INTERFACE arrow-audio-alsa)
else()
    target_link_libraries(arrow-adapters INTERFACE arrow-audio-null)
endif()
target_link_libraries(arrow-adapters INTERFACE arrow-audio-wav arrow-domain)

# ---------------------------------------------------------------------------
#  PulseAudio sink — Linux only, shared mode only, non-bit-perfect.
#  REQ-AUD-067: labelled as non-bit-perfect in diagnostics.
# ---------------------------------------------------------------------------
if(ARROW_HAVE_PULSEAUDIO)
    target_compile_definitions(arrow-audio-null PRIVATE ARROW_HAVE_PULSEAUDIO=1)
    add_library(arrow-audio-pulse STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/pulse_sink.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
    target_compile_definitions(arrow-audio-pulse PRIVATE ARROW_HAVE_PULSEAUDIO=1)
    target_include_directories(arrow-audio-pulse PUBLIC src)
    target_link_libraries(arrow-audio-pulse PUBLIC arrow-domain arrow-audio-null arrow::pulse)
    arrow_set_warnings(arrow-audio-pulse)
    arrow_set_sanitizers(arrow-audio-pulse)
    arrow_set_hardening(arrow-audio-pulse)
    target_compile_features(arrow-audio-pulse PUBLIC cxx_std_20)
    target_link_libraries(arrow-adapters INTERFACE arrow-audio-pulse)
endif()

# ---------------------------------------------------------------------------
#  WASAPI sink — Windows only, shared and exclusive modes.
#  REQ-AUD-118: device-loss recovery state machine.
# ---------------------------------------------------------------------------
if(WIN32)
    add_library(arrow-audio-wasapi STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/wasapi_sink.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
    target_compile_definitions(arrow-audio-wasapi PRIVATE ARROW_HAVE_WASAPI=1)
    target_include_directories(arrow-audio-wasapi PUBLIC src)
    target_link_libraries(arrow-audio-wasapi PUBLIC arrow-domain arrow-audio-null)
    arrow_set_warnings(arrow-audio-wasapi)
    arrow_set_sanitizers(arrow-audio-wasapi)
    arrow_set_hardening(arrow-audio-wasapi)
    target_compile_features(arrow-audio-wasapi PUBLIC cxx_std_20)
    target_link_libraries(arrow-adapters INTERFACE arrow-audio-wasapi)
endif()

# ---------------------------------------------------------------------------
#  Native OS integration layer — §14.
#  SMTC (Windows), MPRIS2 (Linux), system tray, global hotkeys.
# ---------------------------------------------------------------------------
add_library(arrow-native STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/smtc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/mpris2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/tray.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native/hotkeys.cpp")
target_include_directories(arrow-native PUBLIC src)
target_link_libraries(arrow-native PUBLIC arrow-domain)
arrow_set_warnings(arrow-native)
arrow_set_sanitizers(arrow-native)
arrow_set_hardening(arrow-native)
target_compile_features(arrow-native PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(arrow-native PUBLIC winmm.lib)
endif()
if(ARROW_HAVE_DBUS)
    target_link_libraries(arrow-native PUBLIC arrow::dbus)
endif()

target_link_libraries(arrow-adapters INTERFACE arrow-native)

if(ARROW_HAVE_SQLITE3)
    add_library(arrow-library-sqlite STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_database.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/database.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/scanner.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/tag_reader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/change_log.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/filesystem_scanner.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_importer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/sidecar_tag_reader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/smart_playlist.cpp")
    target_include_directories(arrow-library-sqlite PUBLIC src)
    # TagLib is required for tag reading (REQ-LIB-025)
    set(_library_deps arrow-domain arrow::sqlite3)
    if(ARROW_HAVE_TAGLIB)
        list(APPEND _library_deps arrow::taglib)
    endif()
    target_link_libraries(arrow-library-sqlite PUBLIC ${_library_deps})
    arrow_set_warnings(arrow-library-sqlite)
    arrow_set_sanitizers(arrow-library-sqlite)
    arrow_set_hardening(arrow-library-sqlite)
    target_compile_features(arrow-library-sqlite PUBLIC cxx_std_20)
    target_link_libraries(arrow-adapters INTERFACE arrow-library-sqlite)
endif()






# ---------------------------------------------------------------------------
#  Layer 4 — APPLICATION.  §7.1: use cases, lifecycle, command dispatch.  Pure
#  C++20 like the domain layer, and for the same reason: everything here must be
#  unit-testable without Qt, because Qt is absent on any machine that has not
#  installed it and CI is not a debugger.
#
#  It must never name layer 5. §7.2 rule 1 makes that an error the layer gate
#  reports (tools/check-layers.py); src/main.cpp is the composition root and the
#  only translation unit that may see both.
# ---------------------------------------------------------------------------
set(ARROW_APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/app_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/application.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/command_line.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/lifecycle.cpp"
)
add_library(arrow-app STATIC ${ARROW_APP_SOURCES})
target_link_libraries(arrow-app PUBLIC arrow-domain)
arrow_set_warnings(arrow-app)
arrow_set_sanitizers(arrow-app)
arrow_set_hardening(arrow-app)      # REQ-SEC-018
target_compile_features(arrow-app PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
#  Tools
# ---------------------------------------------------------------------------

# enable_testing() before the UI subdirectory: ui/tests-ui registers test_ui
# via add_test, and a test registered before enable_testing() takes effect is
# silently dropped from ctest — which is exactly what the first UI-enabled run
# proved (the off-screen suite executed 6/6 locally while ctest listed 211, not
# 212). Testing is on whenever tests are built; the block below is only about
# ordering, so the call lives here rather than next to add_subdirectory(tests).
if(ARROW_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
#  Qt UI  (optional; §6.1)
# ---------------------------------------------------------------------------
if(ARROW_BUILD_UI AND ARROW_HAVE_QT)
    add_subdirectory(ui)
endif()

# ---------------------------------------------------------------------------
#  The player executable  (§28 Phase 0: "CMake + presets producing a Qt hello
#  window"; exit gate 1 is "window opens", exit gate 7 "version shown in About")
#
#  Declared after the ui/ subdirectory on purpose: `TARGET arrow-ui` is only
#  answerable once that directory has been processed, and the alternative —
#  re-deriving "will the UI be built?" from ARROW_BUILD_UI and ARROW_HAVE_QT
#  here — is a second copy of a condition, which is a copy that drifts.
#
#  Without the UI this still builds and still links, and main.cpp reports that no
#  window can open rather than exiting 0 in silence.
# ---------------------------------------------------------------------------
add_executable(arrow-player "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(arrow-player PRIVATE arrow-app arrow-audio-wav arrow-native)
if(WIN32 AND ARROW_HAVE_WASAPI)
    target_link_libraries(arrow-player PRIVATE arrow-audio-wasapi)
elseif(ARROW_HAVE_PULSEAUDIO)
    target_link_libraries(arrow-player PRIVATE arrow-audio-pulse)
elseif(ARROW_HAVE_ALSA)
    target_link_libraries(arrow-player PRIVATE arrow-audio-alsa)
else()
    target_link_libraries(arrow-player PRIVATE arrow-audio-null)
endif()
arrow_set_warnings(arrow-player)
arrow_set_sanitizers(arrow-player)
arrow_set_hardening(arrow-player)   # REQ-SEC-018
if(TARGET arrow-ui)
    target_link_libraries(arrow-player PRIVATE arrow-ui)
    target_compile_definitions(arrow-player PRIVATE ARROW_WITH_UI=1)
endif()

# ---------------------------------------------------------------------------
#  REQ-BLD-026: arrow-player-desktop — aggregate target linking all modules
# ---------------------------------------------------------------------------
add_executable(arrow-player-desktop IMPORTED)
set_target_properties(arrow-player-desktop PROPERTIES
    IMPORTED_LOCATION "$<TARGET_FILE:arrow-player>")
# Propagate Qt runtime path on Windows (REQ-GEN-013 dynamic linking)
if(WIN32 AND ARROW_HAVE_QT)
    set_target_properties(arrow-player-desktop PROPERTIES
        BUILD_RPATH "$<TARGET_PROPERTY:arrow-player,BUILD_RPATH>")
endif()

# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------
if(ARROW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# tests/ adds tests/fuzz itself, because the corpus replay cases are ordinary
# tests (REQ-SEC-012). This branch covers the fuzzers-without-tests build only;
# adding the same directory twice is a hard CMake error, hence the NOT.
if(ARROW_BUILD_FUZZERS AND NOT ARROW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/fuzz)
endif()

# ---------------------------------------------------------------------------
#  Install rules — spec §24.3 (REQ-BLD-026)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS arrow-player
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

# Install headers for development
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.23")
    install(TARGETS arrow-domain EXPORT ArrowTargets
        FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/arrow")
else()
    install(DIRECTORY src/
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/arrow"
        FILES_MATCHING PATTERN "*.hpp")
endif()

install(EXPORT ArrowTargets FILE ArrowTargets.cmake DESTINATION share/arrow-player/cmake)

# CPack configuration for .deb, AppImage (Linux), MSI/NSIS (Windows)
set(CPACK_PACKAGE_NAME "arrow-player")
set(CPACK_PACKAGE_VERSION "${ARROW_VERSION}")
set(CPACK_PACKAGE_VENDOR "Arrow Player")
set(CPACK_PACKAGE_CONTACT "https://arrow-player.org")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_SOURCE_DIR}/README.md")

include(InstallRequiredSystemLibraries)

# Linux packaging
if(LINUX)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://arrow-player.org")
    set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
endif()

# Windows packaging
if(WIN32)
    set(CPACK_GENERATOR "NSIS;MSI")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
    set(CPACK_NSIS_MODIFY_PATH ON)
endif()

include(CPack)

# ---------------------------------------------------------------------------
#  Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "======== Arrow Player ${ARROW_VERSION} (${ARROW_GIT_SHA}) ========")
message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(VCPKG_TOOLCHAIN)
    message(STATUS "  deps from     : vcpkg (${VCPKG_TARGET_TRIPLET})")
else()
    message(STATUS "  deps from     : system / pkg-config")
endif()
message(STATUS "  --- adapters (dependency-gated) ---")
message(STATUS "  SQLite3       : ${ARROW_HAVE_SQLITE3}")
message(STATUS "  libcurl       : ${ARROW_HAVE_CURL}")
message(STATUS "  FFmpeg        : ${ARROW_HAVE_FFMPEG}")
message(STATUS "  TagLib        : ${ARROW_HAVE_TAGLIB}")
message(STATUS "  ALSA          : ${ARROW_HAVE_ALSA}")
message(STATUS "  libsamplerate : ${ARROW_HAVE_SAMPLERATE}")
message(STATUS "  nlohmann_json : ${ARROW_HAVE_NLOHMANN_JSON}")
message(STATUS "  SoundTouch    : ${ARROW_HAVE_SOUNDTOUCH}")
message(STATUS "  Chromaprint   : ${ARROW_HAVE_CHROMAPRINT}")
message(STATUS "  projectM      : ${ARROW_HAVE_PROJECTM}")
message(STATUS "  Qt 6          : ${ARROW_HAVE_QT}")

# An `OFF` above is easy to misread as "this platform cannot build that
# adapter". Usually it means pkg-config was not told where to look — which is
# the exact confusion recorded as OQ-021 in docs/OPEN-QUESTIONS.md.
if(NOT ARROW_HAVE_FFMPEG OR NOT ARROW_HAVE_TAGLIB OR NOT ARROW_HAVE_SAMPLERATE
        OR NOT ARROW_HAVE_SQLITE3)
    message(STATUS "  note          : OFF means the library was not found, not that the")
    message(STATUS "                  adapter is unsupported here. With a user-local prefix,")
    message(STATUS "                  export PKG_CONFIG_PATH — see docs/BUILDING.md.")
endif()
message(STATUS "  --- toggles ---")
message(STATUS "  tests         : ${ARROW_BUILD_TESTS}")
if(TARGET arrow-ui)
message(STATUS "  window        : yes (arrow-ui linked)")
else()
message(STATUS "  window        : NO — arrow-player will open no window")
endif()
message(STATUS "  werror        : ${ARROW_WERROR}")
message(STATUS "  asan+ubsan    : ${ARROW_SANITIZE_ADDRESS}")
message(STATUS "  tsan          : ${ARROW_SANITIZE_THREAD}")
message(STATUS "==================================================")
message(STATUS "")

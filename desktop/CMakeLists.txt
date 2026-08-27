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
#  Eclipse Player — Desktop (Windows + Linux)
#  Spec: eclipse-player.md  §5 repo layout, §7 architecture, §24 build
# ---------------------------------------------------------------------------

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" ECLIPSE_VERSION_RAW)
string(STRIP "${ECLIPSE_VERSION_RAW}" ECLIPSE_VERSION)

project(EclipsePlayer
    VERSION   ${ECLIPSE_VERSION}
    LANGUAGES CXX
    DESCRIPTION "Free, open-source, privacy-first music player"
    HOMEPAGE_URL "https://eclipse-player.org")

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
option(ECLIPSE_BUILD_TESTS      "Build the test suites"                ON)
option(ECLIPSE_BUILD_FUZZERS    "Build libFuzzer targets (§21.6)"      OFF)
option(ECLIPSE_BUILD_BENCH      "Build benchmarks (§20)"               OFF)
option(ECLIPSE_BUILD_UI         "Build the Qt UI (requires Qt 6.8+)"   ON)
option(ECLIPSE_ENABLE_ASIO      "ASIO output — SDK user-supplied (§4.6)" OFF)
option(ECLIPSE_ENABLE_RTAUDIO   "RtAudio fallback sink (§8.7.7)"       OFF)
option(ECLIPSE_WERROR           "Treat warnings as errors (§24.1)"     OFF)
option(ECLIPSE_SANITIZE_ADDRESS "ASan + UBSan"                         OFF)
option(ECLIPSE_SANITIZE_THREAD  "ThreadSanitizer"                      OFF)

include(EclipseWarnings)
include(EclipseSanitizers)
include(EclipseDependencies)

# ---------------------------------------------------------------------------
#  Generated version header  (REQ-BLD-007)
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(ECLIPSE_GIT_SHA "unknown")
set(ECLIPSE_GIT_DIRTY 0)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/../.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." OUTPUT_VARIABLE ECLIPSE_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." RESULT_VARIABLE ECLIPSE_GIT_DIRTY ERROR_QUIET)
endif()
configure_file(include/eclipse/version.hpp.in
               "${CMAKE_BINARY_DIR}/generated/eclipse/version.hpp" @ONLY)

# ---------------------------------------------------------------------------
#  Layer 3 — DOMAIN.  Pure C++20.  REQ-GEN-050(2): links against nothing but
#  the standard library.  Enforced by tools/check-layers.py in CI.
# ---------------------------------------------------------------------------
#  Absolute paths, and a variable rather than a literal list, because
#  tests/fuzz/ recompiles these same translation units with coverage
#  instrumentation for libFuzzer (a fuzzer linked against an uninstrumented
#  library is a fuzzer that mutates blindly). One list, one place to add a file.
set(ECLIPSE_DOMAIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/text.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/json/json.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/biquad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/dsp/equalizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/gapless_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/graph/playback_graph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/playback_service.cpp"
)


# Playback ports and the SPSC ring are header-only contracts/primitives. They
# are compiled by the domain consumers and intentionally require no adapter.
add_library(eclipse-domain STATIC ${ECLIPSE_DOMAIN_SOURCES})
target_include_directories(eclipse-domain PUBLIC
    include src "${CMAKE_BINARY_DIR}/generated")
eclipse_set_warnings(eclipse-domain)
eclipse_set_sanitizers(eclipse-domain)
eclipse_set_hardening(eclipse-domain)   # REQ-SEC-018
target_compile_features(eclipse-domain PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(eclipse-domain PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
#  Layer 1 — ADAPTERS, each gated on dependency availability so the tree
#  always configures.  §7.2: reachable only through layer-2 ports.
# ---------------------------------------------------------------------------
add_library(eclipse-adapters INTERFACE)
add_library(eclipse-audio-null STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/null_sink.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
target_include_directories(eclipse-audio-null PUBLIC src)
target_link_libraries(eclipse-audio-null PUBLIC eclipse-domain)
eclipse_set_warnings(eclipse-audio-null)
eclipse_set_sanitizers(eclipse-audio-null)
eclipse_set_hardening(eclipse-audio-null)
target_compile_features(eclipse-audio-null PUBLIC cxx_std_20)

add_library(eclipse-audio-wav STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/decode/wav_decoder.cpp")
target_include_directories(eclipse-audio-wav PUBLIC src)
target_link_libraries(eclipse-audio-wav PUBLIC eclipse-domain)
eclipse_set_warnings(eclipse-audio-wav)
eclipse_set_sanitizers(eclipse-audio-wav)
eclipse_set_hardening(eclipse-audio-wav)
target_compile_features(eclipse-audio-wav PUBLIC cxx_std_20)
if(ECLIPSE_HAVE_ALSA)
    # ALSA is on Linux only. The factory dispatches to AlsaSink at runtime,
    # so its compilation unit must live in the same library as alsa_sink.cpp
    # to resolve the vtable. We compile the factory twice — once in
    # eclipse-audio-null (always) and once in eclipse-audio-alsa (when ALSA
    # is on, with the AlsaSink vtable available) — and the executable links
    # the variant that matches the current platform.
    target_compile_definitions(eclipse-audio-null PRIVATE ECLIPSE_HAVE_ALSA=1)
    add_library(eclipse-audio-alsa STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/alsa_sink.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sink/sink_factory.cpp")
    target_compile_definitions(eclipse-audio-alsa PRIVATE ECLIPSE_HAVE_ALSA=1)
    target_include_directories(eclipse-audio-alsa PUBLIC src)
    target_link_libraries(eclipse-audio-alsa PUBLIC eclipse-domain eclipse::alsa)
    eclipse_set_warnings(eclipse-audio-alsa)
    eclipse_set_sanitizers(eclipse-audio-alsa)
    eclipse_set_hardening(eclipse-audio-alsa)
    target_compile_features(eclipse-audio-alsa PUBLIC cxx_std_20)
    target_link_libraries(eclipse-adapters INTERFACE eclipse-audio-alsa)
else()
    target_link_libraries(eclipse-adapters INTERFACE eclipse-audio-null)
endif()
target_link_libraries(eclipse-adapters INTERFACE eclipse-audio-wav eclipse-domain)
if(ECLIPSE_HAVE_SQLITE3)
    add_library(eclipse-library-sqlite STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_database.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/filesystem_scanner.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/library_importer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/library/sidecar_tag_reader.cpp")
    target_include_directories(eclipse-library-sqlite PUBLIC src)
    target_link_libraries(eclipse-library-sqlite PUBLIC eclipse-domain eclipse::sqlite3)
    eclipse_set_warnings(eclipse-library-sqlite)
    eclipse_set_sanitizers(eclipse-library-sqlite)
    eclipse_set_hardening(eclipse-library-sqlite)
    target_compile_features(eclipse-library-sqlite PUBLIC cxx_std_20)
    target_link_libraries(eclipse-adapters INTERFACE eclipse-library-sqlite)
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
set(ECLIPSE_APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/app_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/application.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/command_line.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/lifecycle.cpp"
)
add_library(eclipse-app STATIC ${ECLIPSE_APP_SOURCES})
target_link_libraries(eclipse-app PUBLIC eclipse-domain)
eclipse_set_warnings(eclipse-app)
eclipse_set_sanitizers(eclipse-app)
eclipse_set_hardening(eclipse-app)      # REQ-SEC-018
target_compile_features(eclipse-app PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
#  Tools
# ---------------------------------------------------------------------------

# enable_testing() before the UI subdirectory: ui/tests-ui registers test_ui
# via add_test, and a test registered before enable_testing() takes effect is
# silently dropped from ctest — which is exactly what the first UI-enabled run
# proved (the off-screen suite executed 6/6 locally while ctest listed 211, not
# 212). Testing is on whenever tests are built; the block below is only about
# ordering, so the call lives here rather than next to add_subdirectory(tests).
if(ECLIPSE_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
#  Qt UI  (optional; §6.1)
# ---------------------------------------------------------------------------
if(ECLIPSE_BUILD_UI AND ECLIPSE_HAVE_QT)
    add_subdirectory(ui)
endif()

# ---------------------------------------------------------------------------
#  The player executable  (§28 Phase 0: "CMake + presets producing a Qt hello
#  window"; exit gate 1 is "window opens", exit gate 7 "version shown in About")
#
#  Declared after the ui/ subdirectory on purpose: `TARGET eclipse-ui` is only
#  answerable once that directory has been processed, and the alternative —
#  re-deriving "will the UI be built?" from ECLIPSE_BUILD_UI and ECLIPSE_HAVE_QT
#  here — is a second copy of a condition, which is a copy that drifts.
#
#  Without the UI this still builds and still links, and main.cpp reports that no
#  window can open rather than exiting 0 in silence.
# ---------------------------------------------------------------------------
add_executable(eclipse-player "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(eclipse-player PRIVATE eclipse-app eclipse-audio-wav)
if(ECLIPSE_HAVE_ALSA)
    # The factory's compilation unit lives in eclipse-audio-alsa when ALSA is
    # on, so the executable must link that library explicitly to resolve
    # make_sink() and the AlsaSink vtable.
    target_link_libraries(eclipse-player PRIVATE eclipse-audio-alsa)
else()
    target_link_libraries(eclipse-player PRIVATE eclipse-audio-null)
endif()
eclipse_set_warnings(eclipse-player)
eclipse_set_sanitizers(eclipse-player)
eclipse_set_hardening(eclipse-player)   # REQ-SEC-018
if(TARGET eclipse-ui)
    target_link_libraries(eclipse-player PRIVATE eclipse-ui)
    target_compile_definitions(eclipse-player PRIVATE ECLIPSE_WITH_UI=1)
endif()

# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------
if(ECLIPSE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# tests/ adds tests/fuzz itself, because the corpus replay cases are ordinary
# tests (REQ-SEC-012). This branch covers the fuzzers-without-tests build only;
# adding the same directory twice is a hard CMake error, hence the NOT.
if(ECLIPSE_BUILD_FUZZERS AND NOT ECLIPSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/fuzz)
endif()

# ---------------------------------------------------------------------------
#  Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "======== Eclipse Player ${ECLIPSE_VERSION} (${ECLIPSE_GIT_SHA}) ========")
message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(VCPKG_TOOLCHAIN)
    message(STATUS "  deps from     : vcpkg (${VCPKG_TARGET_TRIPLET})")
else()
    message(STATUS "  deps from     : system / pkg-config")
endif()
message(STATUS "  --- adapters (dependency-gated) ---")
message(STATUS "  SQLite3       : ${ECLIPSE_HAVE_SQLITE3}")
message(STATUS "  FFmpeg        : ${ECLIPSE_HAVE_FFMPEG}")
message(STATUS "  TagLib        : ${ECLIPSE_HAVE_TAGLIB}")
message(STATUS "  ALSA          : ${ECLIPSE_HAVE_ALSA}")
message(STATUS "  libsamplerate : ${ECLIPSE_HAVE_SAMPLERATE}")
message(STATUS "  Qt 6          : ${ECLIPSE_HAVE_QT}")

# An `OFF` above is easy to misread as "this platform cannot build that
# adapter". Usually it means pkg-config was not told where to look — which is
# the exact confusion recorded as OQ-021 in docs/OPEN-QUESTIONS.md.
if(NOT ECLIPSE_HAVE_FFMPEG OR NOT ECLIPSE_HAVE_TAGLIB OR NOT ECLIPSE_HAVE_SAMPLERATE
        OR NOT ECLIPSE_HAVE_SQLITE3)
    message(STATUS "  note          : OFF means the library was not found, not that the")
    message(STATUS "                  adapter is unsupported here. With a user-local prefix,")
    message(STATUS "                  export PKG_CONFIG_PATH — see docs/BUILDING.md.")
endif()
message(STATUS "  --- toggles ---")
message(STATUS "  tests         : ${ECLIPSE_BUILD_TESTS}")
if(TARGET eclipse-ui)
message(STATUS "  window        : yes (eclipse-ui linked)")
else()
message(STATUS "  window        : NO — eclipse-player will open no window")
endif()
message(STATUS "  werror        : ${ECLIPSE_WERROR}")
message(STATUS "  asan+ubsan    : ${ECLIPSE_SANITIZE_ADDRESS}")
message(STATUS "  tsan          : ${ECLIPSE_SANITIZE_THREAD}")
message(STATUS "==================================================")
message(STATUS "")

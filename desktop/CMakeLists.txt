cmake_minimum_required(VERSION 3.28)

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
add_library(eclipse-domain STATIC
    src/core/error.cpp
    src/core/text.cpp
    src/core/json/json.cpp
    src/audio/dsp/biquad.cpp
    src/audio/dsp/equalizer.cpp
    src/audio/decode/gapless_info.cpp
)
target_include_directories(eclipse-domain PUBLIC
    include src "${CMAKE_BINARY_DIR}/generated")
eclipse_set_warnings(eclipse-domain)
eclipse_set_sanitizers(eclipse-domain)
target_compile_features(eclipse-domain PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(eclipse-domain PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
#  Layer 1 — ADAPTERS, each gated on dependency availability so the tree
#  always configures.  §7.2: reachable only through layer-2 ports.
# ---------------------------------------------------------------------------
add_library(eclipse-adapters INTERFACE)
target_link_libraries(eclipse-adapters INTERFACE eclipse-domain)






# ---------------------------------------------------------------------------
#  Tools
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
#  Qt UI  (optional; §6.1)
# ---------------------------------------------------------------------------
if(ECLIPSE_BUILD_UI AND ECLIPSE_HAVE_QT)
    add_subdirectory(ui)
endif()

# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------
if(ECLIPSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ECLIPSE_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()

# ---------------------------------------------------------------------------
#  Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "======== Eclipse Player ${ECLIPSE_VERSION} (${ECLIPSE_GIT_SHA}) ========")
message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  --- adapters (dependency-gated) ---")
message(STATUS "  SQLite3       : ${ECLIPSE_HAVE_SQLITE3}")
message(STATUS "  FFmpeg        : ${ECLIPSE_HAVE_FFMPEG}")
message(STATUS "  TagLib        : ${ECLIPSE_HAVE_TAGLIB}")
message(STATUS "  ALSA          : ${ECLIPSE_HAVE_ALSA}")
message(STATUS "  Qt 6          : ${ECLIPSE_HAVE_QT}")
message(STATUS "  --- toggles ---")
message(STATUS "  tests         : ${ECLIPSE_BUILD_TESTS}")
message(STATUS "  werror        : ${ECLIPSE_WERROR}")
message(STATUS "  asan+ubsan    : ${ECLIPSE_SANITIZE_ADDRESS}")
message(STATUS "  tsan          : ${ECLIPSE_SANITIZE_THREAD}")
message(STATUS "==================================================")
message(STATUS "")

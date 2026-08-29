# Dependency discovery — spec §6.2, §6.3, §4.4
#
# Design rule: every external dependency is OPTIONAL at configure time so the
# tree always configures on any machine. Adapters that need a missing library
# are simply not built (see desktop/CMakeLists.txt). The pure-C++20 domain
# layer never depends on anything here.
#
# Qt comes from aqtinstall, NOT vcpkg (§6.2 / ADR-0005). Everything else comes
# from vcpkg in manifest mode with a pinned baseline.

include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)

# When the vcpkg toolchain is active, the linkage rules of §4.2 live in the
# project triplets. A stock triplet builds every LGPL component statically,
# which REQ-GEN-013 forbids for anything we ship, so say so loudly rather than
# discovering it in a licence review.
if(VCPKG_TOOLCHAIN AND NOT VCPKG_TARGET_TRIPLET MATCHES "-arrow$")
    message(WARNING
        "vcpkg is active with triplet '${VCPKG_TARGET_TRIPLET}', which is not "
        "one of the project triplets in cmake/triplets/. Those encode the §4.2 "
        "linkage column — LGPL components dynamic, permissive ones static. A "
        "stock triplet may link FFmpeg or TagLib statically, which REQ-GEN-013 "
        "forbids in a shipped artifact. Pass -DVCPKG_TARGET_TRIPLET=<arch>-"
        "<os>-arrow for anything you intend to distribute.")
endif()

# --------------------------------------------------------------------- SQLite3
set(ARROW_HAVE_SQLITE3 OFF)
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    add_library(arrow::sqlite3 INTERFACE IMPORTED)
    target_link_libraries(arrow::sqlite3 INTERFACE SQLite::SQLite3)
    set(ARROW_HAVE_SQLITE3 ON)
endif()

# ---------------------------------------------------------------------- FFmpeg
# REQ-GEN-014: shipped builds MUST be LGPL-configured. REQ-GEN-015 asserts this
# at runtime in CI via the licence-assertion test; we cannot assert it here
# because a system FFmpeg may be either.
set(ARROW_HAVE_FFMPEG OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat>=60 libavcodec>=60 libavutil>=58 libswresample>=4)
    if(FFMPEG_FOUND)
        add_library(arrow::ffmpeg INTERFACE IMPORTED)
        target_link_libraries(arrow::ffmpeg INTERFACE PkgConfig::FFMPEG)
        set(ARROW_HAVE_FFMPEG ON)
    endif()
endif()

# ---------------------------------------------------------------------- TagLib
set(ARROW_HAVE_TAGLIB OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib>=1.13)
    if(TAGLIB_FOUND)
        add_library(arrow::taglib INTERFACE IMPORTED)
        target_link_libraries(arrow::taglib INTERFACE PkgConfig::TAGLIB)
        set(ARROW_HAVE_TAGLIB ON)
    endif()
endif()

# ------------------------------------------------------------------------ ALSA
set(ARROW_HAVE_ALSA OFF)
if(UNIX AND NOT APPLE AND PkgConfig_FOUND)
    pkg_check_modules(ALSA QUIET IMPORTED_TARGET alsa)
    if(ALSA_FOUND)
        add_library(arrow::alsa INTERFACE IMPORTED)
        target_link_libraries(arrow::alsa INTERFACE PkgConfig::ALSA)
        set(ARROW_HAVE_ALSA ON)
    endif()
endif()

# ------------------------------------------------------------------ libsamplerate
# REQ-GEN-012 pins libsamplerate at >= 0.2.2 in the §4.2 register. Two separate
# floors are folded into that one number and both matter: releases before 0.1.9
# were GPL and would be incompatible with our MPL-2.0 core (§4.1), and anything
# below the registered version is a dependency the register does not describe,
# which REQ-GEN-012 makes a build failure rather than a footnote.
set(ARROW_HAVE_SAMPLERATE OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(SAMPLERATE QUIET IMPORTED_TARGET samplerate>=0.2.2)
    if(SAMPLERATE_FOUND)
        add_library(arrow::samplerate INTERFACE IMPORTED)
        target_link_libraries(arrow::samplerate INTERFACE PkgConfig::SAMPLERATE)
        set(ARROW_HAVE_SAMPLERATE ON)
    endif()
endif()

# ------------------------------------------------------------------ nlohmann_json
set(ARROW_HAVE_NLOHMANN_JSON OFF)
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    add_library(arrow::nlohmann_json INTERFACE IMPORTED)
    target_link_libraries(arrow::nlohmann_json INTERFACE nlohmann_json::nlohmann_json)
    set(ARROW_HAVE_NLOHMANN_JSON ON)
endif()

# ------------------------------------------------------------------ SoundTouch
set(ARROW_HAVE_SOUNDTOUCH OFF)
find_package(SoundTouch QUIET)
if(SoundTouch_FOUND)
    add_library(arrow::soundtouch INTERFACE IMPORTED)
    target_link_libraries(arrow::soundtouch INTERFACE SoundTouch::SoundTouch)
    set(ARROW_HAVE_SOUNDTOUCH ON)
endif()

# ------------------------------------------------------------------ Chromaprint
set(ARROW_HAVE_CHROMAPRINT OFF)
find_package(Chromaprint QUIET)
if(Chromaprint_FOUND)
    add_library(arrow::chromaprint INTERFACE IMPORTED)
    target_link_libraries(arrow::chromaprint INTERFACE Chromaprint::Chromaprint)
    set(ARROW_HAVE_CHROMAPRINT ON)
endif()

# ------------------------------------------------------------------ projectM
set(ARROW_HAVE_PROJECTM OFF)
find_package(projectM QUIET)
if(projectM_FOUND)
    add_library(arrow::projectm INTERFACE IMPORTED)
    target_link_libraries(arrow::projectm INTERFACE projectM::projectM)
    set(ARROW_HAVE_PROJECTM ON)
endif()

# -------------------------------------------------------------------------- Qt
set(ARROW_HAVE_QT OFF)
find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Widgets Quick Svg)
if(Qt6_FOUND)
    set(ARROW_HAVE_QT ON)
    # REQ-GEN-013: Qt MUST be dynamically linked (LGPL-3.0 relinking duty).
    get_target_property(_qt_core_type Qt6::Core TYPE)
    if(_qt_core_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "Qt is statically linked. This violates REQ-GEN-013 (LGPL-3.0 "
            "requires the user be able to replace Qt). Use a shared Qt build.")
    endif()
endif()

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
if(VCPKG_TOOLCHAIN AND NOT VCPKG_TARGET_TRIPLET MATCHES "-eclipse$")
    message(WARNING
        "vcpkg is active with triplet '${VCPKG_TARGET_TRIPLET}', which is not "
        "one of the project triplets in cmake/triplets/. Those encode the §4.2 "
        "linkage column — LGPL components dynamic, permissive ones static. A "
        "stock triplet may link FFmpeg or TagLib statically, which REQ-GEN-013 "
        "forbids in a shipped artifact. Pass -DVCPKG_TARGET_TRIPLET=<arch>-"
        "<os>-eclipse for anything you intend to distribute.")
endif()

# --------------------------------------------------------------------- SQLite3
set(ECLIPSE_HAVE_SQLITE3 OFF)
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    add_library(eclipse::sqlite3 INTERFACE IMPORTED)
    target_link_libraries(eclipse::sqlite3 INTERFACE SQLite::SQLite3)
    set(ECLIPSE_HAVE_SQLITE3 ON)
endif()

# ---------------------------------------------------------------------- FFmpeg
# REQ-GEN-014: shipped builds MUST be LGPL-configured. REQ-GEN-015 asserts this
# at runtime in CI via the licence-assertion test; we cannot assert it here
# because a system FFmpeg may be either.
set(ECLIPSE_HAVE_FFMPEG OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat>=60 libavcodec>=60 libavutil>=58 libswresample>=4)
    if(FFMPEG_FOUND)
        add_library(eclipse::ffmpeg INTERFACE IMPORTED)
        target_link_libraries(eclipse::ffmpeg INTERFACE PkgConfig::FFMPEG)
        set(ECLIPSE_HAVE_FFMPEG ON)
    endif()
endif()

# ---------------------------------------------------------------------- TagLib
set(ECLIPSE_HAVE_TAGLIB OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib>=1.13)
    if(TAGLIB_FOUND)
        add_library(eclipse::taglib INTERFACE IMPORTED)
        target_link_libraries(eclipse::taglib INTERFACE PkgConfig::TAGLIB)
        set(ECLIPSE_HAVE_TAGLIB ON)
    endif()
endif()

# ------------------------------------------------------------------------ ALSA
set(ECLIPSE_HAVE_ALSA OFF)
if(UNIX AND NOT APPLE AND PkgConfig_FOUND)
    pkg_check_modules(ALSA QUIET IMPORTED_TARGET alsa)
    if(ALSA_FOUND)
        add_library(eclipse::alsa INTERFACE IMPORTED)
        target_link_libraries(eclipse::alsa INTERFACE PkgConfig::ALSA)
        set(ECLIPSE_HAVE_ALSA ON)
    endif()
endif()

# ------------------------------------------------------------------ libsamplerate
# REQ-GEN-012 pins libsamplerate at >= 0.2.2 in the §4.2 register. Two separate
# floors are folded into that one number and both matter: releases before 0.1.9
# were GPL and would be incompatible with our MPL-2.0 core (§4.1), and anything
# below the registered version is a dependency the register does not describe,
# which REQ-GEN-012 makes a build failure rather than a footnote.
set(ECLIPSE_HAVE_SAMPLERATE OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(SAMPLERATE QUIET IMPORTED_TARGET samplerate>=0.2.2)
    if(SAMPLERATE_FOUND)
        add_library(eclipse::samplerate INTERFACE IMPORTED)
        target_link_libraries(eclipse::samplerate INTERFACE PkgConfig::SAMPLERATE)
        set(ECLIPSE_HAVE_SAMPLERATE ON)
    endif()
endif()

# -------------------------------------------------------------------------- Qt
set(ECLIPSE_HAVE_QT OFF)
find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Widgets Quick Svg)
if(Qt6_FOUND)
    set(ECLIPSE_HAVE_QT ON)
    # REQ-GEN-013: Qt MUST be dynamically linked (LGPL-3.0 relinking duty).
    get_target_property(_qt_core_type Qt6::Core TYPE)
    if(_qt_core_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "Qt is statically linked. This violates REQ-GEN-013 (LGPL-3.0 "
            "requires the user be able to replace Qt). Use a shared Qt build.")
    endif()
endif()

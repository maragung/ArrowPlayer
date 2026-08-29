# QtUtils.cmake — Qt runtime detection per spec §6.2, REQ-GEN-030
#
# Finds Qt6 via the path set by aqtinstall (CMAKE_PREFIX_PATH from GitHub Actions
# environment). Validates Qt >= 6.8 as spec requires, and detects missing modules
# to fail fast rather than silently disabling the UI.

#[=======================================================================[

SYNOPSIS
    QtUtils_DetectQt([MIN_VERSION <version>] [REQUIRED_COMPONENTS <comp>...])

    include(QtUtils)  # loads the module

DESCRIPTION
    Detects Qt6 via the standard find_package mechanism. The
    CMAKE_PREFIX_PATH must point to the Qt installation root (set by
    aqtinstall via GitHub Actions environment variable export).

    - Validates Qt version >= 6.8
    - Detects missing required modules
    - Exports ARROW_HAVE_QT_FOUND and ARROW_QT_VERSION variables
    - Sets ARROW_QT_PREFIX to the detected Qt prefix

OPTIONS
    MIN_VERSION           Minimum Qt version (default: 6.8)
    REQUIRED_COMPONENTS  List of required Qt components

EXAMPLE
    include(QtUtils)
    QtUtils_DetectQt(MIN_VERSION 6.8 REQUIRED_COMPONENTS Core Gui Widgets)

#]=======================================================================]

# Required Qt modules for the desktop player
set(_qt_required_components Core Gui Widgets Quick Svg)

# Optional Qt modules
set(_qt_optional_components
    WebSockets
    Network
    OpenGL
    OpenGLWidgets
)

# Detect Qt version from qt-version.txt for validation
function(QtUtils_GetExpectedVersion OUTPUT_VAR)
    set(_version_file "${CMAKE_CURRENT_SOURCE_DIR}/qt-version.txt")
    if(EXISTS "${_version_file}")
        file(STRINGS "${_version_file}" _qt_version_raw LIMIT_COUNT 1)
        string(STRIP "${_qt_version_raw}" _qt_version)
        set(${OUTPUT_VAR} "${_qt_version}" PARENT_SCOPE)
    else()
        set(${OUTPUT_VAR} "6.8.0" PARENT_SCOPE)
    endif()
endfunction()

# Validate Qt installation meets minimum requirements
function(QtUtils_ValidateQt)
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR "Qt6 not found. Set CMAKE_PREFIX_PATH to the Qt installation directory.")
    endif()

    QtUtils_GetExpectedVersion(_expected_version)

    if(Qt6_VERSION VERSION_LESS "${_expected_version}")
        message(FATAL_ERROR
            "Qt ${Qt6_VERSION} is too old. "
            "Required: >= ${_expected_version}. "
            "Use aqtinstall to install Qt ${_expected_version} or later.")
    endif()

    # Check required modules
    foreach(_comp ${_qt_required_components})
        if(NOT Qt6_${_comp}_FOUND)
            message(FATAL_ERROR
                "Required Qt component ${_comp} not found. "
                "Install Qt with all required components.")
        endif()
    endforeach()

    # Report optional modules
    foreach(_comp ${_qt_optional_components})
        if(Qt6_${_comp}_FOUND)
            message(STATUS "  Qt ${_comp}: available")
        else()
            message(STATUS "  Qt ${_comp}: NOT FOUND (optional)")
        endif()
    endforeach()
endfunction()

# Main detection function
function(QtUtils_DetectQt)
    cmake_parse_arguments(PARSE_ARGV 0 _QT
        ""
        "MIN_VERSION"
        "REQUIRED_COMPONENTS"
    )

    set(_min_version "${_QT_MIN_VERSION}")
    if(NOT _min_version)
        set(_min_version "6.8.0")
    endif()

    set(_required_components ${_QT_REQUIRED_COMPONENTS})
    if(NOT _required_components)
        set(_required_components ${_qt_required_components})
    endif()

    # Find Qt6 with required components
    find_package(Qt6 ${_min_version} REQUIRED COMPONENTS ${_required_components})

    # Validate the installation
    QtUtils_ValidateQt()

    # Export results
    set(ARROW_HAVE_QT_FOUND ON PARENT_SCOPE)
    set(ARROW_QT_VERSION "${Qt6_VERSION}" PARENT_SCOPE)
    set(ARROW_QT_PREFIX "${Qt6_DIR}/../.." PARENT_SCOPE)

    message(STATUS "Qt6 detected: ${Qt6_VERSION}")
    message(STATUS "Qt6 prefix: ${ARROW_QT_PREFIX}")
endfunction()

# Convenience function for CI: detect Qt from aqtinstall path
function(QtUtils_DetectFromAqtinstall)
    # Check CMAKE_PREFIX_PATH from environment
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
        set(_prefix_path "$ENV{CMAKE_PREFIX_PATH}")
        message(STATUS "CMAKE_PREFIX_PATH from environment: ${_prefix_path}")
    endif()

    # Also check common aqtinstall locations
    if(NOT DEFINED ENV{CMAKE_PREFIX_PATH})
        if(WIN32)
            if(DEFINED ENV{RUNNER_TOOL_CACHE})
                set(_possible_paths
                    "$ENV{RUNNER_TOOL_CACHE}/Qt"
                )
            endif()
        elseif(UNIX)
            if(DEFINED ENV{RUNNER_TOOL_CACHE})
                set(_possible_paths
                    "$ENV{RUNNER_TOOL_CACHE}/Qt"
                )
            endif()
            # Also check standard aqtinstall locations
            list(APPEND _possible_paths
                "$ENV{HOME}/Qt"
                "/opt/Qt"
                "/usr/local/Qt"
            )
        endif()

        foreach(_path IN LISTS _possible_paths)
            if(EXISTS "${_path}")
                QtUtils_GetExpectedVersion(_version)
                set(_qt_path "${_path}/${_version}")
                if(EXISTS "${_qt_path}")
                    message(STATUS "Found Qt at ${_qt_path}")
                    set(CMAKE_PREFIX_PATH "${_qt_path}" CACHE STRING "" FORCE)
                    break()
                endif()
            endif()
        endforeach()
    endif()
endfunction()

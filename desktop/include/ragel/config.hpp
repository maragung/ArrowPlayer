// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// This directory is reserved for machine-generated platform and build
// configuration. It exists so that every source file can include a stable
// absolute path regardless of whether the tree was configured with vcpkg,
// system pkg-config, or a custom toolchain.
//
// This file is the canonical guard: every header in src/audio/ includes
// <ragel/config.hpp> as its first include. Any file that violates this rule
// will fail to compile in any build configuration, making the regression
// immediate and obvious.
//
// What lives here:
//   - Compiler / platform detection (ARROW_COMPILER_*, ARROW_PLATFORM_*).
//   - Build-variant flags (ARROW_DEBUG, ARROW_SANITIZE_*).
//   - Dependency availability (ARROW_HAVE_FFMPEG, ARROW_HAVE_ALSA, ...).
//   - Feature flags derived from CMake options.
//   - Architecture constants (ARROW_CACHE_LINE_SIZE).
//
// What does NOT live here:
//   - Application-level configuration (preferences, library paths, etc.).
//   - Generated source (version.hpp lives in include/arrow/).

#ifndef RAGEL_CONFIG_HPP
#define RAGEL_CONFIG_HPP

// ---------------------------------------------------------------------------
//  Compiler detection
// ---------------------------------------------------------------------------
#if defined(__clang__)
#define ARROW_COMPILER_CLANG 1
#define ARROW_COMPILER_CLANG_VERSION (__clang_major__ * 100 + __clang_minor__)
#else
#define ARROW_COMPILER_CLANG 0
#define ARROW_COMPILER_CLANG_VERSION 0
#endif

#if defined(__GNUC__)
#define ARROW_COMPILER_GCC 1
#define ARROW_COMPILER_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#else
#define ARROW_COMPILER_GCC 0
#define ARROW_COMPILER_GCC_VERSION 0
#endif

#if defined(_MSC_VER)
#define ARROW_COMPILER_MSVC 1
#define ARROW_COMPILER_MSVC_VERSION _MSC_VER
#else
#define ARROW_COMPILER_MSVC 0
#define ARROW_COMPILER_MSVC_VERSION 0
#endif

// ---------------------------------------------------------------------------
//  Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define ARROW_PLATFORM_WINDOWS 1
#define ARROW_PLATFORM_POSIX 0
#elif defined(__unix__)
#define ARROW_PLATFORM_WINDOWS 0
#define ARROW_PLATFORM_POSIX 1
#if defined(__linux__)
#define ARROW_PLATFORM_LINUX 1
#else
#define ARROW_PLATFORM_LINUX 0
#endif
#if defined(__APPLE__)
#define ARROW_PLATFORM_MACOS 1
#else
#define ARROW_PLATFORM_MACOS 0
#endif
#else
#define ARROW_PLATFORM_WINDOWS 0
#define ARROW_PLATFORM_POSIX 0
#define ARROW_PLATFORM_LINUX 0
#define ARROW_PLATFORM_MACOS 0
#endif

// ---------------------------------------------------------------------------
//  Build-variant flags (set by CMake)
// ---------------------------------------------------------------------------
#if !defined(ARROW_DEBUG)
#define ARROW_DEBUG 0
#endif

#if !defined(ARROW_SANITIZE_ADDRESS)
#define ARROW_SANITIZE_ADDRESS 0
#endif

#if !defined(ARROW_SANITIZE_THREAD)
#define ARROW_SANITIZE_THREAD 0
#endif

#if !defined(ARROW_SANITIZE_UNDEFINED)
#define ARROW_SANITIZE_UNDEFINED 0
#endif

// ---------------------------------------------------------------------------
//  Architecture constants
// ---------------------------------------------------------------------------
#if !defined(ARROW_CACHE_LINE_SIZE)
#if defined(__SSE2__) || ARROW_COMPILER_MSVC
// x86 cache lines are 64 bytes. The alignas(64) on the SPSC ring indices
// is the primary user of this constant.
#define ARROW_CACHE_LINE_SIZE 64
#else
#define ARROW_CACHE_LINE_SIZE 64  // conservative default
#endif
#endif

// ---------------------------------------------------------------------------
//  Dependency availability (set by CMake via add_definitions)
//
//  These flags are 0 or 1, derived from the ARROW_HAVE_* CMake cache
//  variables in cmake/ArrowDependencies.cmake.
// ---------------------------------------------------------------------------
#if !defined(ARROW_HAVE_FFMPEG)
#define ARROW_HAVE_FFMPEG 0
#endif

#if !defined(ARROW_HAVE_TAGLIB)
#define ARROW_HAVE_TAGLIB 0
#endif

#if !defined(ARROW_HAVE_SAMPLERATE)
#define ARROW_HAVE_SAMPLERATE 0
#endif

#if !defined(ARROW_HAVE_SQLITE3)
#define ARROW_HAVE_SQLITE3 0
#endif

#if !defined(ARROW_HAVE_ALSA)
#define ARROW_HAVE_ALSA 0
#endif

#if !defined(ARROW_HAVE_CURL)
#define ARROW_HAVE_CURL 0
#endif

#if !defined(ARROW_HAVE_QT)
#define ARROW_HAVE_QT 0
#endif

#if !defined(ARROW_WITH_UI)
#define ARROW_WITH_UI 0
#endif

// ---------------------------------------------------------------------------
//  Utility macros
// ---------------------------------------------------------------------------

// [[likely]] / [[unlikely]] availability.
#if defined(__GNUC__) && (__GNUC__ >= 10) || ARROW_COMPILER_CLANG
#define ARROW_LIKELY(x) __builtin_expect(!!(x), 1)
#define ARROW_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define ARROW_LIKELY(x) (!!(x))
#define ARROW_UNLIKELY(x) (!!(x))
#endif

// [[nodiscard]] availability — all supported compilers have it in C++17+.
#define ARROW_NODISCARD [[nodiscard]]

#endif  // RAGEL_CONFIG_HPP

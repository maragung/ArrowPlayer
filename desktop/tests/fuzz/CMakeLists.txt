# Fuzz targets — spec §21.6 (REQ-SEC-011, REQ-SEC-012)
#
# Each target is built twice, for two different jobs:
#
#   <name>          the real libFuzzer binary. Built only where the compiler
#                   supports -fsanitize=fuzzer (clang), which is what the §25.2
#                   clang matrix entry and §25.4's nightly fuzzing job use.
#   <name>_replay   the same harness with a plain main() that feeds it the
#                   committed corpus. Built everywhere, registered as a CTest
#                   case, and therefore run by every preset — including
#                   linux-asan, whose ASan+UBSan is exactly what REQ-SEC-012
#                   requires the targets to build with.
#
# The split exists because REQ-SEC-011 asks for two separable things: targets
# that explore new inputs, and a corpus that keeps old crashes dead. Only the
# first needs a fuzzing engine.

# ---------------------------------------------------------------------------
#  Does this compiler have libFuzzer?
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
check_cxx_source_compiles("
#include <cstddef>
#include <cstdint>
extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
" ECLIPSE_HAVE_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

# ---------------------------------------------------------------------------
#  The domain sources, recompiled with coverage instrumentation.
#
#  -fsanitize=fuzzer-no-link instruments without providing main(), so the code
#  under test is visible to the mutator while the harness supplies the entry
#  point. Linking the ordinary eclipse-domain instead would still find crashes,
#  but only by luck: libFuzzer would get no coverage feedback from the parsers,
#  which are the entire attack surface.
# ---------------------------------------------------------------------------
if(ECLIPSE_HAVE_LIBFUZZER)
    add_library(eclipse-domain-fuzz STATIC ${ECLIPSE_DOMAIN_SOURCES})
    target_include_directories(eclipse-domain-fuzz PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/generated")
    # PRIVATE: instrumentation is a property of compiling these translation
    # units, not something consumers should inherit — the harness compiles with
    # the stronger -fsanitize=fuzzer and the two flags must not collide.
    target_compile_options(eclipse-domain-fuzz PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_features(eclipse-domain-fuzz PUBLIC cxx_std_20)
    eclipse_set_warnings(eclipse-domain-fuzz)
    eclipse_set_sanitizers(eclipse-domain-fuzz)
    target_link_libraries(eclipse-domain-fuzz PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
#  eclipse_add_fuzz_target(<name>)
#
#  <name>.cpp must define LLVMFuzzerTestOneInput, and corpus/<name>/ must hold
#  at least one seed. Both are checked here rather than left to fail later: a
#  fuzz target with no corpus is the failure mode REQ-SEC-011 names explicitly.
# ---------------------------------------------------------------------------
function(eclipse_add_fuzz_target name)
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}")
    file(GLOB seeds "${corpus}/*")
    list(LENGTH seeds seed_count)
    if(seed_count EQUAL 0)
        message(FATAL_ERROR
            "Fuzz target ${name} has no seeds in ${corpus}. REQ-SEC-011 requires a "
            "committed corpus; add seeds to make-seeds.py and run it.")
    endif()

    # Replay: everywhere, and part of the ordinary test run.
    add_executable(${name}_replay ${name}.cpp replay_main.cpp)
    target_link_libraries(${name}_replay PRIVATE eclipse-domain)
    target_include_directories(${name}_replay PRIVATE "${CMAKE_SOURCE_DIR}/src")
    eclipse_set_warnings(${name}_replay)
    eclipse_set_sanitizers(${name}_replay)
    # REQ-SEC-018. Under linux-release these four are ordinary release binaries,
    # and tools/check-hardening.py found them unhardened the first time it ran:
    # PIE and a stack protector by the distro compiler's own defaults, but only
    # partial RELRO, because -z now is nobody's default. Under the sanitizer and
    # fuzz presets the call deliberately no-ops — see eclipse_set_hardening.
    eclipse_set_hardening(${name}_replay)
    # The corpus path is passed as a directory, not a file list: the driver walks
    # it at run time, so a seed added by REQ-SEC-012's regression rule becomes a
    # test case without re-running CMake.
    add_test(NAME fuzz_corpus.${name} COMMAND ${name}_replay "${corpus}")
    set_tests_properties(fuzz_corpus.${name} PROPERTIES
        LABELS "fuzz-corpus"
        TIMEOUT 300)

    # libFuzzer: only where it exists.
    if(ECLIPSE_HAVE_LIBFUZZER)
        add_executable(${name} ${name}.cpp)
        target_link_libraries(${name} PRIVATE eclipse-domain-fuzz)
        target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
        eclipse_set_warnings(${name})
        eclipse_set_sanitizers(${name})
        # No-ops today, since REQ-SEC-012 has this preset build with ASan. Called
        # anyway so the rule stays "every executable the build produces gets the
        # call", which is one fewer thing to remember than "except these".
        eclipse_set_hardening(${name})
    endif()
endfunction()

# ---------------------------------------------------------------------------
#  The targets that exist today.
#
#  REQ-SEC-011 names seventeen; one of them exists (fuzz_xinglame), because a
#  fuzz target needs a parser to point at and sixteen of those parsers are Phase 2
#  and later work. The other three here are supporting targets covering parsers
#  that do exist and are reached by untrusted bytes today; they are not counted as
#  standing in for any of the seventeen. See README.md in this directory and OQ-043
#  in docs/OPEN-QUESTIONS.md — the gap is recorded rather than papered over with
#  empty harnesses.
# ---------------------------------------------------------------------------
set(ECLIPSE_FUZZ_TARGETS fuzz_json fuzz_text fuzz_xinglame fuzz_gapless)
foreach(target IN LISTS ECLIPSE_FUZZ_TARGETS)
    eclipse_add_fuzz_target(${target})
endforeach()

list(LENGTH ECLIPSE_FUZZ_TARGETS eclipse_fuzz_count)
if(ECLIPSE_HAVE_LIBFUZZER)
    message(STATUS "  fuzzing       : ${eclipse_fuzz_count} libFuzzer target(s) + corpus replay")
else()
    message(STATUS "  fuzzing       : corpus replay only "
                   "(${CMAKE_CXX_COMPILER_ID} has no -fsanitize=fuzzer)")
endif()

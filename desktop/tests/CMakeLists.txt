# Test suites — spec §23
#
# GoogleTest is vendored as a pinned tarball rather than fetched from the
# network at configure time, so the build is reproducible and works offline
# (REQ-SEC-013: no floating versions).

include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
    URL      "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest-1.15.2.tar.gz"
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ---------------------------------------------------------------------------
#  arrow_add_test(<name> SOURCES ... [LIBS ...])
#
#  Registers a GoogleTest binary and discovers its cases individually so a
#  single failing assertion is reported as a single failing CTest case.
# ---------------------------------------------------------------------------
function(arrow_add_test name)
    cmake_parse_arguments(A "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${A_SOURCES})
    target_link_libraries(${name} PRIVATE arrow-domain GTest::gtest_main GTest::gmock ${A_LIBS})
    if(name STREQUAL "test_library_database" AND TARGET arrow-library-sqlite)
        target_link_libraries(${name} PRIVATE arrow-library-sqlite)
    endif()
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src")
    arrow_set_warnings(${name})
    arrow_set_sanitizers(${name})
    # REQ-SEC-018. A test executable is a produced binary, so the hardening gate
    # has something to inspect here too. It was the *only* such binary until
    # src/main.cpp landed; arrow-player is now the one that ships, and both are
    # checked, because a flag applied to the shipped binary and not to the tests
    # is a flag whose effect on the code under test is untested.
    arrow_set_hardening(${name})
    gtest_discover_tests(${name}
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit")
endfunction()

# ---------------------------------------------------------------------------
#  Unit suites — one binary per domain area (§23.2)
# ---------------------------------------------------------------------------
arrow_add_test(test_core    SOURCES unit/test_text.cpp unit/test_error.cpp unit/test_json.cpp)
arrow_add_test(test_dsp     SOURCES unit/test_equalizer.cpp)
arrow_add_test(test_gapless SOURCES unit/test_gapless.cpp)
arrow_add_test(test_audio_ports SOURCES unit/test_audio_ports.cpp)
arrow_add_test(test_null_sink SOURCES unit/test_null_sink.cpp LIBS arrow-audio-null)
arrow_add_test(test_sink_factory SOURCES unit/test_sink_factory.cpp LIBS arrow-audio-null)
if(TARGET arrow-audio-wav)
    arrow_add_test(test_wav_decoder SOURCES unit/test_wav_decoder.cpp LIBS arrow-audio-wav)
    arrow_add_test(test_playback_service SOURCES unit/test_playback_service.cpp LIBS arrow-audio-wav arrow-audio-null)
endif()
arrow_add_test(test_playback_graph SOURCES unit/test_playback_graph.cpp)
if(ARROW_HAVE_SQLITE3)
    arrow_add_test(test_library_database SOURCES unit/test_library_database.cpp)
    arrow_add_test(test_library_robustness SOURCES unit/test_library_robustness.cpp LIBS arrow-library-sqlite)
endif()
arrow_add_test(test_app     SOURCES unit/test_app.cpp     LIBS arrow-app)
arrow_add_test(test_command_line SOURCES unit/test_command_line.cpp LIBS arrow-app arrow-audio-null)
arrow_add_test(test_format_parser SOURCES unit/test_format_parser.cpp)
arrow_add_test(test_ring_buffer SOURCES unit/test_ring_buffer.cpp)
if(ARROW_HAVE_SQLITE3)
    arrow_add_test(test_smart_playlist SOURCES unit/test_smart_playlist.cpp LIBS arrow-library-sqlite)
endif()

# ---------------------------------------------------------------------------
#  Fuzz targets and their corpus (§21.6, REQ-SEC-011, REQ-SEC-012)
#
#  Added unconditionally rather than under ARROW_BUILD_FUZZERS, because corpus
#  replay is an ordinary regression test: REQ-SEC-012 requires the input behind
#  every sanitizer finding to enter the corpus, and a corpus only the nightly
#  fuzzing job replays would let such a finding come back unnoticed for a day.
#  The libFuzzer binaries themselves stay gated on compiler support inside.
# ---------------------------------------------------------------------------
add_subdirectory(fuzz)

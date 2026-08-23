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
#  eclipse_add_test(<name> SOURCES ... [LIBS ...])
#
#  Registers a GoogleTest binary and discovers its cases individually so a
#  single failing assertion is reported as a single failing CTest case.
# ---------------------------------------------------------------------------
function(eclipse_add_test name)
    cmake_parse_arguments(A "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${A_SOURCES})
    target_link_libraries(${name} PRIVATE eclipse-domain GTest::gtest_main GTest::gmock ${A_LIBS})
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src")
    eclipse_set_warnings(${name})
    eclipse_set_sanitizers(${name})
    gtest_discover_tests(${name}
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit")
endfunction()

# ---------------------------------------------------------------------------
#  Unit suites — one binary per domain area (§23.2)
# ---------------------------------------------------------------------------
eclipse_add_test(test_core    SOURCES unit/test_text.cpp unit/test_error.cpp unit/test_json.cpp)
eclipse_add_test(test_dsp     SOURCES unit/test_equalizer.cpp)
eclipse_add_test(test_gapless SOURCES unit/test_gapless.cpp)

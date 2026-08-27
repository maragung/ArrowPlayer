# ---------------------------------------------------------------------------
#  Qt Widgets shell — spec §7.1 layer 5 (PRESENTATION), §28 Phase 0
#  (exit gate 1 "window opens", exit gate 7 "version shown in About").
#
#  Added from desktop/CMakeLists.txt only when Qt 6.8+ was found and
#  ECLIPSE_BUILD_UI is ON, so a machine without Qt still configures — the
#  domain layer is Qt-free by design (REQ-GEN-050) and the whole tree must
#  always configure.
#
#  Qt is dynamically linked by requirement (REQ-GEN-013); EclipseDependencies
#  already refused a static Qt at configure time, so nothing repeats that here.
# ---------------------------------------------------------------------------

# Widgets is known to be present (ECLIPSE_HAVE_QT required it), but
# LinguistTools is only needed here and is requested only by this directory.
find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

# Q_OBJECT (MainWindow) needs the meta-object compiler. AUTOMOC only mocs
# headers that are listed as target sources (or pulled in via `moc_X.cpp`
# includes); a Q_OBJECT header merely reachable through the include path is
# silently skipped, which the first green Linux build proved by linking a UI
# whose MainWindow vtable and staticMetaObject were missing.
set(CMAKE_AUTOMOC ON)

add_library(eclipse-ui STATIC
    src/shell.cpp
    src/main_window.cpp
    include/eclipse/ui/main_window.hpp
)
target_include_directories(eclipse-ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(eclipse-ui PUBLIC Qt6::Widgets eclipse-domain)
eclipse_set_warnings(eclipse-ui)
eclipse_set_hardening(eclipse-ui)   # REQ-SEC-018
target_compile_features(eclipse-ui PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
#  Translations — §12.7 pipeline, Phase 0 ships `en` and `id` (translation unit
#  B). The .ts sources are committed in desktop/resources/i18n/; qt_add_lrelease
#  compiles them to .qm in the build directory and qt_add_resources embeds them
#  as the `:/i18n/eclipse_<locale>.qm` resource that shell.cpp loads. The
#  resource path is the only thing shared between the two, and it is written
#  once here.
# ---------------------------------------------------------------------------
set(ECLIPSE_TS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/i18n/eclipse_en.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/i18n/eclipse_id.ts"
)
qt_add_lrelease(ECLIPSE_QM_FILES ${ECLIPSE_TS_FILES})
qt_add_resources(eclipse-ui "eclipse-i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${ECLIPSE_QM_FILES}
)

# ---------------------------------------------------------------------------
#  Off-screen QTest suite — §23, REQ-TST-018/019's future home. Runs under the
#  `offscreen` Qt platform plugin, so it executes on a headless CI runner.
# ---------------------------------------------------------------------------
if(ECLIPSE_BUILD_TESTS)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tests/ui"
                     "${CMAKE_CURRENT_BINARY_DIR}/tests-ui")
endif()

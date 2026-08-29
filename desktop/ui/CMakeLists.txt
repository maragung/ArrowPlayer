# ---------------------------------------------------------------------------
#  Qt Widgets shell — spec §7.1 layer 5 (PRESENTATION), §28 Phase 0
#  (exit gate 1 "window opens", exit gate 7 "version shown in About").
#
#  Added from desktop/CMakeLists.txt only when Qt 6.8+ was found and
#  ARROW_BUILD_UI is ON, so a machine without Qt still configures — the
#  domain layer is Qt-free by design (REQ-GEN-050) and the whole tree must
#  always configure.
#
#  Qt is dynamically linked by requirement (REQ-GEN-013); ArrowDependencies
#  already refused a static Qt at configure time, so nothing repeats that here.
# ---------------------------------------------------------------------------

# Widgets is known to be present (ARROW_HAVE_QT required it), but
# LinguistTools is only needed here and is requested only by this directory.
find_package(Qt6 REQUIRED COMPONENTS Widgets Qml Quick LinguistTools)

# Q_OBJECT (MainWindow) needs the meta-object compiler. AUTOMOC only mocs
# headers that are listed as target sources (or pulled in via `moc_X.cpp`
# includes); a Q_OBJECT header merely reachable through the include path is
# silently skipped, which the first green Linux build proved by linking a UI
# whose MainWindow vtable and staticMetaObject were missing.
set(CMAKE_AUTOMOC ON)

# QML files are compiled into the Qt Resource System and embedded in the
# binary via qt_add_resources.  The FILES list lives in qml/CMakeLists.txt.
qt_add_resources(arrow-ui "arrow-qml"
    PREFIX "/qml"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/NowPlaying/Main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/NowPlaying/TransportControls.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/NowPlaying/AlbumArt.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/NowPlaying/TrackInfo.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/SeekBar/Main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/Main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/ArtistList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/AlbumList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/SongList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/GenreList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Library/PlaylistList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Queue/Main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Equalizer/Main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Equalizer/BandSlider.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Equalizer/FrequencyGraph.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/Templates.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/qmldir"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-play.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-pause.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-skip-previous.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-skip-next.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-shuffle.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-repeat.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-repeat-one.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-volume-high.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-volume-low.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-volume-mute.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-grid.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-list.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/icon-sort.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../qml/Theme/assets/placeholder-album.svg"
)

# ---------------------------------------------------------------------------
#  Layer 5 — PRESENTATION: Qt Widgets shell + QML surfaces
# ---------------------------------------------------------------------------
add_library(arrow-ui STATIC
    src/shell.cpp
    src/main_window.cpp
    src/ui/playlist_tabs.cpp
    src/ui/settings_dialog.cpp
    include/arrow/ui/main_window.hpp
    include/arrow/ui/shell.hpp
    include/arrow/ui/playlist_tabs.hpp
    include/arrow/ui/settings_dialog.hpp
)
target_include_directories(arrow-ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(arrow-ui PUBLIC Qt6::Widgets Qt6::Qml Qt6::Quick arrow-domain)
arrow_set_warnings(arrow-ui)
arrow_set_hardening(arrow-ui)   # REQ-SEC-018
target_compile_features(arrow-ui PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
#  Translations — §12.7 pipeline, Phase 0 ships `en` and `id` (translation unit
#  B). The .ts sources are committed in desktop/resources/i18n/; qt_add_lrelease
#  compiles them to .qm in the build directory and qt_add_resources embeds them
#  as the `:/i18n/arrow_<locale>.qm` resource that shell.cpp loads. The
#  resource path is the only thing shared between the two, and it is written
#  once here.
# ---------------------------------------------------------------------------
set(ARROW_TS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/i18n/arrow_en.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/i18n/arrow_id.ts"
)
qt_add_lrelease(ARROW_QM_FILES ${ARROW_TS_FILES})
qt_add_resources(arrow-ui "arrow-i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${ARROW_QM_FILES}
)

# ---------------------------------------------------------------------------
#  Off-screen QTest suite — §23, REQ-TST-018/019's future home. Runs under the
#  `offscreen` Qt platform plugin, so it executes on a headless CI runner.
# ---------------------------------------------------------------------------
if(ARROW_BUILD_TESTS)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tests/ui"
                     "${CMAKE_CURRENT_BINARY_DIR}/tests-ui")
endif()

# ---------------------------------------------------------------------------
#  Off-screen UI tests — spec §23.2, §28 Phase 0 exit gates 1 and 7.
#
#  Added from ui/CMakeLists.txt, so this directory is only configured when Qt
#  is present. Every case runs under the `offscreen` Qt platform plugin (set
#  below as a CTest environment), so the suite executes on headless CI runners
#  exactly as it does on a desktop — no display, no Xvfb, no special-casing.
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_ui test_shell.cpp)
target_link_libraries(test_ui PRIVATE eclipse-ui Qt6::Test)
eclipse_set_warnings(test_ui)
eclipse_set_hardening(test_ui)   # REQ-SEC-018

add_test(NAME test_ui COMMAND test_ui)
set_tests_properties(test_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "ui")

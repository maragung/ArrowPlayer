# Warning configuration — spec §24.1 REQ-BLD-003
# Formatting is never a review topic; warnings are never negotiable.

function(eclipse_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            # The tests carry non-ASCII as universal-character-names in narrow
            # literals (e.g. "\u00E9") and the domain layer is UTF-8 end to end.
            # MSVC's default execution charset is the system code page (1252 on
            # the CI runner): UCNs outside it emit C4566 and, worse, encode the
            # string wrongly. /utf-8 makes source and execution charset match
            # GCC/Clang's UTF-8 default. REQ-GEN-046 is the UTF-8 requirement.
            /utf-8
            /W4 /permissive-
            /w14242 /w14254 /w14263 /w14265 /w14287 /we4289
            /w14296 /w14311 /w14545 /w14546 /w14547 /w14549
            /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
            /wd4251   # dll-interface: not applicable, we ship static internals
        )
        if(ECLIPSE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            -Wnull-dereference
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wduplicated-cond -Wduplicated-branches -Wlogical-op
                -Wuseless-cast)
        endif()
        if(ECLIPSE_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
#  Hardening — REQ-SEC-018
#
#  This function existed for several commits and was called by nothing, so the
#  flags were present in the build files and absent from every binary the build
#  produced. REQ-SEC-018 anticipates exactly that and says CI MUST verify the
#  flags "in the produced binaries, not merely in the build files" —
#  tools/check-hardening.py is that check, and it reads the ELF and PE headers
#  rather than the compiler command line for the same reason.
#
#  Applied to every target the project produces, including test executables. A
#  test binary is a produced binary, and today it is the only kind there is; when
#  the application binary arrives it inherits the same treatment rather than
#  needing a second mechanism.
#
#  On _FORTIFY_SOURCE: the level is probed rather than asserted. Ubuntu's GCC 13
#  predefines _FORTIFY_SOURCE=3 when optimising, and redefining it to 2 emits a
#  redefinition warning that -Werror turns into a failed build — while *undefining*
#  the distro's 3 to set 2 would be a downgrade of hardening dressed up as
#  compliance with a spec that names 2 as a floor. So the definition is added only
#  where the toolchain has not already made one, and the gate checks the effect
#  (fortified glibc entry points in the binary) rather than the flag string.
# ---------------------------------------------------------------------------
if(NOT MSVC AND NOT DEFINED ECLIPSE_FORTIFY_PREDEFINED)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-O2")
    check_cxx_source_compiles("
        #ifndef _FORTIFY_SOURCE
        #error the toolchain does not predefine it
        #endif
        int main() { return 0; }
    " ECLIPSE_FORTIFY_PREDEFINED)
    unset(CMAKE_REQUIRED_FLAGS)
endif()

function(eclipse_set_hardening target)
    # A sanitizer build is not a release build. ASan replaces the allocator and
    # the interceptors _FORTIFY_SOURCE hooks, and the pair reports failures that
    # belong to neither tool; TSan's instrumentation is likewise not what
    # REQ-SEC-018 governs. Declining here keeps the sanitizer suites meaningful
    # instead of quietly weakening the flags for everyone to make them coexist.
    if(ECLIPSE_SANITIZE_ADDRESS OR ECLIPSE_SANITIZE_THREAD)
        return()
    endif()

    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    # Optimised configurations only, and for one hard reason rather than
    # caution: glibc's features.h raises `#warning _FORTIFY_SOURCE requires
    # compiling with optimization` at -O0, which -Werror promotes to an error.
    # Fortification is also genuinely inert there — the checks it substitutes are
    # visible only to the optimiser — so nothing is given up by the condition.
    set(_optimised "$<NOT:$<CONFIG:Debug>>")

    if(MSVC)
        target_compile_options(${target} PRIVATE "$<${_optimised}:/GS;/guard:cf>")
        if(NOT _type STREQUAL "STATIC_LIBRARY")
            # /CETCOMPAT is x86/x64 only; on ARM64 the linker reports it as an
            # unrecognised option. Untestable here — there is no Windows host on
            # this machine — so it is scoped by architecture rather than assumed
            # portable. Tracked as OQ-022.
            set(_pe_flags /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^([Aa][Mm][Dd]64|x86_64|X86)$")
                list(APPEND _pe_flags /CETCOMPAT)
            endif()
            target_link_options(${target} PRIVATE "$<${_optimised}:${_pe_flags}>")
        endif()
    else()
        target_compile_options(${target} PRIVATE
            "$<${_optimised}:-fstack-protector-strong>")
        if(NOT ECLIPSE_FORTIFY_PREDEFINED)
            target_compile_definitions(${target} PRIVATE
                "$<${_optimised}:_FORTIFY_SOURCE=2>")
        endif()

        # The property rather than a bare -fPIE: CMake then emits -fPIC for the
        # static library and -fPIE for the executable that links it, which is the
        # combination -pie actually requires. A static library built without it
        # makes every executable downstream non-PIE no matter what they pass.
        set_property(TARGET ${target} PROPERTY POSITION_INDEPENDENT_CODE ON)

        if(NOT _type STREQUAL "STATIC_LIBRARY" AND NOT _type STREQUAL "OBJECT_LIBRARY")
            target_link_options(${target} PRIVATE
                "$<${_optimised}:-pie>"
                "$<${_optimised}:LINKER:-z,relro>"
                "$<${_optimised}:LINKER:-z,now>"
                "$<${_optimised}:LINKER:-z,noexecstack>")
        endif()
    endif()
endfunction()

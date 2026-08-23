# Warning configuration — spec §24.1 REQ-BLD-003
# Formatting is never a review topic; warnings are never negotiable.

function(eclipse_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
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

# Hardening flags for release artifacts — REQ-SEC-018
function(eclipse_set_hardening target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GS /guard:cf)
        target_link_options(${target} PRIVATE
            /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT /GUARD:CF)
    else()
        target_compile_options(${target} PRIVATE
            -fstack-protector-strong -fPIE)
        target_compile_definitions(${target} PRIVATE _FORTIFY_SOURCE=2)
        target_link_options(${target} PRIVATE
            -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    endif()
endfunction()

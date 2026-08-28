# Sanitizer configuration — spec §24.1 REQ-BLD-005, §23.5

if(ARROW_SANITIZE_ADDRESS AND ARROW_SANITIZE_THREAD)
    message(FATAL_ERROR "ASan and TSan are mutually exclusive. Use separate presets.")
endif()

function(arrow_set_sanitizers target)
    if(MSVC)
        if(ARROW_SANITIZE_ADDRESS)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        endif()
        return()
    endif()

    if(ARROW_SANITIZE_ADDRESS)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()

    if(ARROW_SANITIZE_THREAD)
        target_compile_options(${target} PRIVATE
            -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

function(astraea_enable_sanitizers target)
    if(NOT ASTRAEA_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(FATAL_ERROR "ASTRAEA_ENABLE_SANITIZERS is not configured for MSVC yet")
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endfunction()

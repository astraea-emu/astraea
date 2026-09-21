function(astraea_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
        )
        if(ASTRAEA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:/WX>
            )
        endif()
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
        )
        if(ASTRAEA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-Werror>
            )
        endif()
    endif()
endfunction()

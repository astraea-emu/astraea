function(astraea_add_fuzzer target)
    if(NOT ASTRAEA_BUILD_FUZZERS)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Astraea fuzz targets currently require Clang/libFuzzer")
    endif()

    add_executable(${target} ${ARGN})
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
endfunction()

include_guard(GLOBAL)

function(astraea_verify_explicit_cpp_sources)
    set(options)
    set(one_value_args ROOT)
    set(multi_value_args TARGETS)
    cmake_parse_arguments(
        ARG
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    if(NOT ARG_ROOT)
        message(FATAL_ERROR
            "astraea_verify_explicit_cpp_sources requires ROOT"
        )
    endif()

    if(NOT ARG_TARGETS)
        message(FATAL_ERROR
            "astraea_verify_explicit_cpp_sources requires TARGETS"
        )
    endif()

    cmake_path(
        ABSOLUTE_PATH ARG_ROOT
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE root_absolute
    )

    # This glob is validation-only. Target membership remains explicit.
    # CONFIGURE_DEPENDS intentionally causes a newly added .cpp file to
    # reconfigure and fail until its owning target lists it.
    file(
        GLOB_RECURSE discovered_cpp
        CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${root_absolute}/*.cpp"
    )

    set(listed_sources)
    foreach(target_name IN LISTS ARG_TARGETS)
        if(NOT TARGET "${target_name}")
            message(FATAL_ERROR
                "source-list guard target does not exist: ${target_name}"
            )
        endif()

        get_target_property(
            target_sources
            "${target_name}"
            SOURCES
        )
        if(NOT target_sources)
            continue()
        endif()

        foreach(source IN LISTS target_sources)
            if(source MATCHES "^\\$<")
                continue()
            endif()

            cmake_path(
                ABSOLUTE_PATH source
                BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                NORMALIZE
                OUTPUT_VARIABLE source_absolute
            )
            cmake_path(
                RELATIVE_PATH source_absolute
                BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE source_relative
            )
            list(APPEND listed_sources "${source_relative}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES listed_sources)

    set(missing_sources)
    foreach(source IN LISTS discovered_cpp)
        list(FIND listed_sources "${source}" source_index)
        if(source_index EQUAL -1)
            list(APPEND missing_sources "${source}")
        endif()
    endforeach()

    if(missing_sources)
        list(SORT missing_sources)
        string(JOIN "\n  - " formatted_missing ${missing_sources})
        message(FATAL_ERROR
            "Unlisted C++ source files under ${ARG_ROOT}:\n"
            "  - ${formatted_missing}\n"
            "Add every file to an explicit owning target source list."
        )
    endif()
endfunction()

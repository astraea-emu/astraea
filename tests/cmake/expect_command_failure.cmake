if(NOT DEFINED ASTRAEA_COMMAND)
    message(FATAL_ERROR "ASTRAEA_COMMAND is required")
endif()
if(NOT DEFINED ASTRAEA_EXPECT_REGEX)
    message(FATAL_ERROR "ASTRAEA_EXPECT_REGEX is required")
endif()

execute_process(
    COMMAND "${ASTRAEA_COMMAND}" ${ASTRAEA_COMMAND_ARGS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

set(combined "${stdout}${stderr}")

if(result EQUAL 0)
    message(FATAL_ERROR
        "Command unexpectedly succeeded. Output:\n${combined}")
endif()

if(NOT combined MATCHES "${ASTRAEA_EXPECT_REGEX}")
    message(FATAL_ERROR
        "Expected pattern not found: ${ASTRAEA_EXPECT_REGEX}\n"
        "Output:\n${combined}")
endif()

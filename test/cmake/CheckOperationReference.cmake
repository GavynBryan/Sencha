# The skill's operation reference must be what the registry says today.
# -DBATCH=<kyusu_batch> -DEXPECTED=<reference.md>
execute_process(COMMAND "${BATCH}" --reference OUTPUT_VARIABLE actual RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "kyusu_batch --reference failed with ${result}")
endif()
file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${EXPECTED} is out of date; regenerate it with kyusu_batch --reference")
endif()

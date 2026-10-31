# Applies PATCH to the current directory with git, unless it is in already: FetchContent runs the patch step again
# when it reconfigures, and git apply fails on a patch that is applied.
execute_process(COMMAND git apply --reverse --check ${PATCH} RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
if(NOT applied EQUAL 0)
    execute_process(COMMAND git apply ${PATCH} RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "could not apply ${PATCH}")
    endif()
endif()

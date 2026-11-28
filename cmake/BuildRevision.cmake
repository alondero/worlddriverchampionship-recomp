# Refresh on incremental builds and rewrite the header only when its contents change.
set(revision "unknown")
set(tracked_changes "false")
if(EXISTS "${GIT}")
    execute_process(COMMAND "${GIT}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${ROOT}" RESULT_VARIABLE result OUTPUT_VARIABLE head
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(result EQUAL 0 AND head MATCHES "^[0-9a-f]+$")
        set(revision "${head}")
        execute_process(COMMAND "${GIT}" diff --quiet --ignore-submodules=dirty HEAD --
            WORKING_DIRECTORY "${ROOT}" RESULT_VARIABLE diff_result ERROR_QUIET)
        if(diff_result EQUAL 1)
            set(tracked_changes "true")
        endif()
    endif()
endif()
file(CONFIGURE OUTPUT "${OUTPUT}" @ONLY CONTENT [=[
#pragma once
#define WDC_BUILD_REVISION "@revision@"
#define WDC_BUILD_TRACKED_CHANGES @tracked_changes@
]=])

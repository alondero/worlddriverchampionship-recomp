# Read the gitlink on each build. file(CONFIGURE) preserves the stamp's
# timestamp when an unrelated parent commit leaves the RT64 pin unchanged.
if(NOT DEFINED HEADER)
    execute_process(COMMAND git rev-parse HEAD:lib/rt64 WORKING_DIRECTORY "${ROOT}"
        OUTPUT_VARIABLE pin OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    file(CONFIGURE OUTPUT "${STAMP}" CONTENT "@pin@\n" @ONLY)
else()
    file(STRINGS "${STAMP}" pin LIMIT_COUNT 1)
    execute_process(COMMAND git show "${pin}:src/common/rt64_tmem_hasher.h"
        WORKING_DIRECTORY "${ROOT}/lib/rt64" OUTPUT_FILE "${HEADER}.tmp" COMMAND_ERROR_IS_FATAL ANY)
    file(RENAME "${HEADER}.tmp" "${HEADER}")
endif()

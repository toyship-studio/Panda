if (NOT DEFINED TARGET_PATH OR TARGET_PATH STREQUAL "")
    message(FATAL_ERROR "TARGET_PATH is required")
endif()

if (APPLE AND EXISTS "${TARGET_PATH}")
    execute_process(
        COMMAND /usr/bin/find "${TARGET_PATH}" -name .DS_Store -delete
        OUTPUT_QUIET
        ERROR_QUIET
    )

    execute_process(
        COMMAND /usr/bin/xattr -cr "${TARGET_PATH}"
        OUTPUT_QUIET
        ERROR_QUIET
    )
endif()

if (NOT DEFINED PANDA_SDK_SOURCE OR NOT DEFINED PANDA_SDK_DESTINATION)
    message(FATAL_ERROR "PANDA_SDK_SOURCE and PANDA_SDK_DESTINATION are required")
endif ()

file(REMOVE_RECURSE "${PANDA_SDK_DESTINATION}")

if (NOT IS_DIRECTORY "${PANDA_SDK_SOURCE}")
    message(STATUS "PandaSDK is not packaged; PandaEditor will not embed it")
    return()
endif ()

foreach (_PANDA_SDK_ENTRY include lib resources docs modules plugins)
    if (IS_DIRECTORY "${PANDA_SDK_SOURCE}/${_PANDA_SDK_ENTRY}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${PANDA_SDK_SOURCE}/${_PANDA_SDK_ENTRY}"
                "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_ENTRY}"
            RESULT_VARIABLE _PANDA_SDK_COPY_RESULT
        )
        if (NOT _PANDA_SDK_COPY_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to copy PandaSDK ${_PANDA_SDK_ENTRY}")
        endif ()
    endif ()
endforeach ()

# The license and the third-party notices at the package root: the game export copies the notices
# from here into every exported game.
foreach (_PANDA_SDK_FILE LICENSE.txt THIRD_PARTY_NOTICES.txt)
    if (EXISTS "${PANDA_SDK_SOURCE}/${_PANDA_SDK_FILE}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PANDA_SDK_SOURCE}/${_PANDA_SDK_FILE}"
                "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FILE}"
            RESULT_VARIABLE _PANDA_SDK_COPY_RESULT
        )
        if (NOT _PANDA_SDK_COPY_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to copy PandaSDK ${_PANDA_SDK_FILE}")
        endif ()
    endif ()
endforeach ()

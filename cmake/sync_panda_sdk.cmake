if (NOT DEFINED PANDA_SDK_SOURCE OR NOT DEFINED PANDA_SDK_DESTINATION)
    message(FATAL_ERROR "PANDA_SDK_SOURCE and PANDA_SDK_DESTINATION are required")
endif ()

if (NOT IS_DIRECTORY "${PANDA_SDK_SOURCE}")
    file(REMOVE_RECURSE "${PANDA_SDK_DESTINATION}")
    message(STATUS "PandaSDK is not packaged; PandaEditor will not embed it")
    return()
endif ()

# The package is mirrored, not copied anew: a file whose bytes did not change is not touched and
# keeps its time. A build of the scripts against the embedded SDK then recompiles only what a
# changed header reaches, and the editor sees the scripts behind their SDK only when it really
# changed — not after every build of the editor. What the package no longer has goes.
set(_PANDA_SDK_FILES "")
foreach (_PANDA_SDK_ENTRY include vendor lib resources docs modules plugins)
    if (IS_DIRECTORY "${PANDA_SDK_SOURCE}/${_PANDA_SDK_ENTRY}")
        file(GLOB_RECURSE _PANDA_SDK_ENTRY_FILES LIST_DIRECTORIES false RELATIVE "${PANDA_SDK_SOURCE}"
            "${PANDA_SDK_SOURCE}/${_PANDA_SDK_ENTRY}/*")
        list(APPEND _PANDA_SDK_FILES ${_PANDA_SDK_ENTRY_FILES})
    endif ()
endforeach ()
# The license and the third-party notices at the package root: the game export copies the notices
# from here into every exported game.
foreach (_PANDA_SDK_FILE LICENSE.txt THIRD_PARTY_NOTICES.txt)
    if (EXISTS "${PANDA_SDK_SOURCE}/${_PANDA_SDK_FILE}")
        list(APPEND _PANDA_SDK_FILES "${_PANDA_SDK_FILE}")
    endif ()
endforeach ()

foreach (_PANDA_SDK_FILE IN LISTS _PANDA_SDK_FILES)
    get_filename_component(_PANDA_SDK_FOLDER "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_PANDA_SDK_FOLDER}")
    file(COPY_FILE "${PANDA_SDK_SOURCE}/${_PANDA_SDK_FILE}" "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FILE}"
        ONLY_IF_DIFFERENT RESULT _PANDA_SDK_COPY_RESULT)
    if (NOT _PANDA_SDK_COPY_RESULT STREQUAL "0")
        message(FATAL_ERROR "Failed to copy PandaSDK ${_PANDA_SDK_FILE}: ${_PANDA_SDK_COPY_RESULT}")
    endif ()
endforeach ()

# A file of the mirror the package does not have (removed or renamed since): out, and the folders
# left empty with it.
file(GLOB_RECURSE _PANDA_SDK_PRESENT LIST_DIRECTORIES false RELATIVE "${PANDA_SDK_DESTINATION}" "${PANDA_SDK_DESTINATION}/*")
foreach (_PANDA_SDK_FILE IN LISTS _PANDA_SDK_PRESENT)
    if (NOT EXISTS "${PANDA_SDK_SOURCE}/${_PANDA_SDK_FILE}")
        file(REMOVE "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FILE}")
    endif ()
endforeach ()
file(GLOB_RECURSE _PANDA_SDK_FOLDERS LIST_DIRECTORIES true RELATIVE "${PANDA_SDK_DESTINATION}" "${PANDA_SDK_DESTINATION}/*")
list(SORT _PANDA_SDK_FOLDERS ORDER DESCENDING) # the deepest first: a parent is emptied by its children
foreach (_PANDA_SDK_FOLDER IN LISTS _PANDA_SDK_FOLDERS)
    if (IS_DIRECTORY "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FOLDER}")
        file(GLOB _PANDA_SDK_CHILDREN "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FOLDER}/*")
        if (NOT _PANDA_SDK_CHILDREN)
            file(REMOVE_RECURSE "${PANDA_SDK_DESTINATION}/${_PANDA_SDK_FOLDER}")
        endif ()
    endif ()
endforeach ()

include(cmake/target_platform_app.cmake)
include(CMakeParseArguments)

function(add_app APP_NAME SOURCE INCLUDE RESOURCE_FOLDERS)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs RESOURCE_DEPENDS RESOURCE_TARGET_DEPENDS)
    cmake_parse_arguments(ADD_APP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_target_platform_app(${APP_NAME} "${SOURCE}" "${INCLUDE}" "${RESOURCE_FOLDERS}")

    if (PLATFORM_WIN32 OR PLATFORM_LINUX)
        set(RESOURCE_OUTPUT_DIR "${BIN_DIRECTORY}")
    elseif (PLATFORM_MACOS AND (CMAKE_GENERATOR STREQUAL Xcode))
        set(RESOURCE_OUTPUT_DIR "$<TARGET_BUNDLE_CONTENT_DIR:${APP_NAME}>/Resources")
        set(COPY_RESOURCES_POST_BUILD ON)
    elseif (PLATFORM_MACOS)
        set(RESOURCE_OUTPUT_DIR "${BIN_DIRECTORY}/${APP_NAME}.app/Contents/Resources")
    elseif (PLATFORM_IOS AND (CMAKE_GENERATOR STREQUAL Xcode))
        set(RESOURCE_OUTPUT_DIR "$<TARGET_BUNDLE_CONTENT_DIR:${APP_NAME}>")
        set(COPY_RESOURCES_POST_BUILD ON)
    elseif (PLATFORM_IOS)
        set(RESOURCE_OUTPUT_DIR "${BIN_DIRECTORY}/${APP_NAME}.app")
    endif ()

    if (ADD_APP_RESOURCE_TARGET_DEPENDS)
        add_dependencies(${APP_NAME} ${ADD_APP_RESOURCE_TARGET_DEPENDS})
    endif ()

    if (DEFINED RESOURCE_OUTPUT_DIR)
        set(RESOURCE_COPY_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCE_OUTPUT_DIR}")
        foreach (RESOURCE_FOLDER IN LISTS RESOURCE_FOLDERS)
            if (RESOURCE_FOLDER STREQUAL "")
                continue()
            endif ()
            get_filename_component(RESOURCE_FOLDER_NAME "${RESOURCE_FOLDER}" NAME)
            get_filename_component(RESOURCE_FOLDER_EXTENSION "${RESOURCE_FOLDER}" EXT)
            # A directory, or a generated directory that does not exist yet at configure time; a plain
            # file without an extension (LICENSE) is copied as a file.
            if (IS_DIRECTORY "${RESOURCE_FOLDER}" OR (RESOURCE_FOLDER_EXTENSION STREQUAL "" AND NOT EXISTS "${RESOURCE_FOLDER}"))
                list(APPEND RESOURCE_COPY_COMMANDS
                    COMMAND ${CMAKE_COMMAND} -E remove_directory "${RESOURCE_OUTPUT_DIR}/${RESOURCE_FOLDER_NAME}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${RESOURCE_FOLDER}" "${RESOURCE_OUTPUT_DIR}/${RESOURCE_FOLDER_NAME}"
                )
            else()
                list(APPEND RESOURCE_COPY_COMMANDS
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RESOURCE_FOLDER}" "${RESOURCE_OUTPUT_DIR}/${RESOURCE_FOLDER_NAME}"
                )
            endif ()
        endforeach ()
        if (PLATFORM_MACOS OR PLATFORM_IOS)
            if (COPY_RESOURCES_POST_BUILD)
                list(APPEND RESOURCE_COPY_COMMANDS
                    COMMAND ${CMAKE_COMMAND}
                        "-DTARGET_PATH=$<TARGET_BUNDLE_DIR:${APP_NAME}>"
                        -P "${PANDA_ROOT_DIRECTORY}/cmake/clear_xattrs.cmake"
                )
            else()
                list(APPEND RESOURCE_COPY_COMMANDS
                    COMMAND ${CMAKE_COMMAND}
                        "-DTARGET_PATH=${RESOURCE_OUTPUT_DIR}"
                        -P "${PANDA_ROOT_DIRECTORY}/cmake/clear_xattrs.cmake"
                )
            endif ()
        endif ()

        if (COPY_RESOURCES_POST_BUILD)
            add_custom_command(TARGET ${APP_NAME} POST_BUILD ${RESOURCE_COPY_COMMANDS})
            if (ADD_APP_RESOURCE_DEPENDS)
                add_custom_target(${APP_NAME}_ResourceInputs DEPENDS ${ADD_APP_RESOURCE_DEPENDS})
                add_dependencies(${APP_NAME} ${APP_NAME}_ResourceInputs)
            endif ()
        else()
            add_custom_target(${APP_NAME}_Resources DEPENDS ${ADD_APP_RESOURCE_DEPENDS} ${RESOURCE_COPY_COMMANDS})
            if (ADD_APP_RESOURCE_TARGET_DEPENDS)
                add_dependencies(${APP_NAME}_Resources ${ADD_APP_RESOURCE_TARGET_DEPENDS})
            endif ()
            # Desktop applications share BIN_DIRECTORY. Serialize their resource targets so that one target cannot
            # remove a directory while another target is copying the same shared resources into it.
            if (PLATFORM_WIN32 OR PLATFORM_LINUX)
                get_property(PANDA_PREVIOUS_SHARED_RESOURCE_TARGET GLOBAL PROPERTY PANDA_PREVIOUS_SHARED_RESOURCE_TARGET)
                if (PANDA_PREVIOUS_SHARED_RESOURCE_TARGET)
                    add_dependencies(${APP_NAME}_Resources ${PANDA_PREVIOUS_SHARED_RESOURCE_TARGET})
                endif ()
                set_property(GLOBAL PROPERTY PANDA_PREVIOUS_SHARED_RESOURCE_TARGET ${APP_NAME}_Resources)
            endif ()
            add_dependencies(${APP_NAME} ${APP_NAME}_Resources)
        endif ()
    endif ()

endfunction()

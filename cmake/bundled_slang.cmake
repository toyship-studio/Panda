# Bundling of slangc into the distributions (PandaEditor.app, PandaSDK) — with the same mechanism
# as the bundled CMake: from Vendor/tools (fetch_shader_tools.sh) into
# Tools/slang/<platform>/<arch>. The editor compiles user shaders by itself (MirenTools) — with
# the bundle it is self-contained, the Vulkan SDK and the user's PATH are not needed. Resolved at
# runtime by SystemTools::findSlangExecutable.
# Requires PANDA_SLANGC_EXECUTABLE and PANDA_TOOLS_HOST_* from shader_bake.cmake.

set(PANDA_BUNDLED_SLANG_AVAILABLE FALSE)

if (PANDA_SLANGC_EXECUTABLE AND PANDA_TOOLS_HOST_PLATFORM)
    get_filename_component(_PANDA_BUNDLED_SLANG_BIN_DIR "${PANDA_SLANGC_EXECUTABLE}" DIRECTORY)
    get_filename_component(PANDA_BUNDLED_SLANG_SOURCE_PREFIX "${_PANDA_BUNDLED_SLANG_BIN_DIR}" DIRECTORY)
    if (EXISTS "${PANDA_BUNDLED_SLANG_SOURCE_PREFIX}/bin")
        set(PANDA_BUNDLED_SLANG_AVAILABLE TRUE)
        set(PANDA_BUNDLED_SLANG_RELATIVE_DIR
            "slang/${PANDA_TOOLS_HOST_PLATFORM}/${PANDA_TOOLS_HOST_ARCH}"
        )
    else ()
        message(WARNING "Cannot bundle slangc: unexpected layout at ${PANDA_BUNDLED_SLANG_SOURCE_PREFIX}")
    endif ()
endif ()

function(panda_bundled_slang_copy_commands OUT_VAR DESTINATION_ROOT)
    if (NOT PANDA_BUNDLED_SLANG_AVAILABLE)
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif ()

    # The prefix is copied as a whole (bin + lib + LICENSE): slangc finds libslang-compiler
    # through @rpath relative to itself — the layout of the Slang distribution is preserved.
    set(_DESTINATION_DIR "${DESTINATION_ROOT}/${PANDA_BUNDLED_SLANG_RELATIVE_DIR}")
    set(_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${DESTINATION_ROOT}/slang"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PANDA_BUNDLED_SLANG_SOURCE_PREFIX}"
            "${_DESTINATION_DIR}"
    )
    set(${OUT_VAR} ${_COMMANDS} PARENT_SCOPE)
endfunction()

function(panda_add_bundled_slang_copy_target TARGET_NAME DESTINATION_ROOT)
    panda_bundled_slang_copy_commands(_PANDA_BUNDLED_SLANG_COPY_COMMANDS "${DESTINATION_ROOT}")
    if (_PANDA_BUNDLED_SLANG_COPY_COMMANDS)
        add_custom_target(${TARGET_NAME} ${_PANDA_BUNDLED_SLANG_COPY_COMMANDS}
            COMMENT "Bundling slangc"
            VERBATIM
        )
    endif ()
endfunction()

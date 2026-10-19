# The single point where the graphics API of the build is chosen: the shaders are baked for it,
# and Fern::createWindow creates a context of the same type by default.
if (WIN32)
    set(PANDA_SHADER_BACKEND_DEFAULT "D3D11")
elseif (PLATFORM_MACOS)
    # The native macOS API; OpenGL remains an option (-DPANDA_SHADER_BACKEND=OpenGL).
    set(PANDA_SHADER_BACKEND_DEFAULT "Metal")
else ()
    set(PANDA_SHADER_BACKEND_DEFAULT "OpenGL")
endif ()
set(PANDA_SHADER_BACKEND "${PANDA_SHADER_BACKEND_DEFAULT}" CACHE STRING "Graphics backend: cooked shaders + default drawing context")
set_property(CACHE PANDA_SHADER_BACKEND PROPERTY STRINGS OpenGL Vulkan D3D11 Metal)

# Foundation::ACTIVE_GRAPHICS_API (Foundation/GraphicsAPI.hpp) follows the option.
if (PANDA_SHADER_BACKEND STREQUAL "D3D11")
    add_compile_definitions(PANDA_GRAPHICS_D3D11)
elseif (PANDA_SHADER_BACKEND STREQUAL "Metal")
    add_compile_definitions(PANDA_GRAPHICS_METAL)
endif ()

# Host platform/architecture — the Vendor/tools layout matches that of the bundled CMake.
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(PANDA_TOOLS_HOST_PLATFORM "mac")
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(PANDA_TOOLS_HOST_PLATFORM "linux")
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(PANDA_TOOLS_HOST_PLATFORM "windows")
else ()
    set(PANDA_TOOLS_HOST_PLATFORM "")
endif ()
if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(PANDA_TOOLS_HOST_ARCH "aarch64")
else ()
    set(PANDA_TOOLS_HOST_ARCH "x86_64")
endif ()

# The HLSL → SPIR-V compiler is slangc (Khronos Slang), the only external tool of the shader
# toolchain: the SPIR-V → GLSL/MSL conversion is done by the vendored SPIRV-Cross inside
# MirenTools (the MirenShaderTool host tool). The Vulkan SDK is no longer needed; for details
# see context/rendering.md.
set(PANDA_SLANGC_HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/tools/slang/${PANDA_TOOLS_HOST_PLATFORM}/${PANDA_TOOLS_HOST_ARCH}/bin"
)
# slangc flags for the project's HLSL: column-major — parity with dxc (the UBOs are the
# column-major glm matrices of the ShaderBlocks.hpp structures). Kept in sync with
# MirenTools::compileHlslShaderToSpv.
set(PANDA_SLANGC_ARGS -lang hlsl -matrix-layout-column-major -entry main -target spirv)

# Shader includes (`#include "panda/…"`): the root is default-shaders/include. They are expanded
# by the host tool `MirenShaderTool expand` BEFORE any compiler — a single mechanism on all
# backends (slangc and the runtime D3DCompile do not see the includes, the cooked shaders are
# self-contained). Any edit of an include re-bakes all shaders (~80, seconds) — no depfile is
# needed. The includes are copied into the generated root: the editor compiles project shaders
# with this root from Resources, and the generated blocks (ShaderInterface) are checked against
# the C++ structures by the editor and the smoke test.
set(PANDA_SHADER_INCLUDE_ROOT "${PANDA_DIRECTORY}/resources/default-shaders/include")
file(GLOB_RECURSE PANDA_SHADER_INCLUDE_SOURCES CONFIGURE_DEPENDS "${PANDA_SHADER_INCLUDE_ROOT}/*.hlsli")
list(SORT PANDA_SHADER_INCLUDE_SOURCES)

if (NOT PANDA_SHADER_BACKEND STREQUAL "D3D11")
# The D3D11 backend compiles HLSL at runtime (D3DCompile); slangc is only
# required for the SPIR-V based backends.

find_program(PANDA_SLANGC_EXECUTABLE NAMES slangc HINTS ${PANDA_SLANGC_HINTS})
if (NOT PANDA_SLANGC_EXECUTABLE)
    message(FATAL_ERROR
        "slangc was not found. Run scripts/fetch_shader_tools.sh (Windows: scripts\\fetch_shader_tools.ps1) "
        "to download it into Vendor/tools, or pass -DPANDA_SLANGC_EXECUTABLE=<path-to-slangc>."
    )
endif ()

# Probe with a real compilation at configure time: a broken/incompatible slangc should fail
# here with a clear message instead of failing later mid-build.
set(PANDA_SLANGC_PROBE_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/panda_slangc_probe")
file(MAKE_DIRECTORY "${PANDA_SLANGC_PROBE_DIR}")
file(WRITE "${PANDA_SLANGC_PROBE_DIR}/probe.frag.hlsl"
    "float4 main() : SV_Target { return float4(0.0, 0.0, 0.0, 1.0); }\n"
)
execute_process(
    COMMAND "${PANDA_SLANGC_EXECUTABLE}" ${PANDA_SLANGC_ARGS} -stage fragment
            -o "${PANDA_SLANGC_PROBE_DIR}/probe.spv" "${PANDA_SLANGC_PROBE_DIR}/probe.frag.hlsl"
    RESULT_VARIABLE PANDA_SLANGC_PROBE_RESULT
    OUTPUT_VARIABLE PANDA_SLANGC_PROBE_OUTPUT
    ERROR_VARIABLE PANDA_SLANGC_PROBE_OUTPUT
)
if (NOT PANDA_SLANGC_PROBE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "slangc at '${PANDA_SLANGC_EXECUTABLE}' cannot compile HLSL to SPIR-V. "
        "Re-run scripts/fetch_shader_tools.sh --force or pass -DPANDA_SLANGC_EXECUTABLE=<path>. "
        "slangc output: ${PANDA_SLANGC_PROBE_OUTPUT}"
    )
endif ()

else () # D3D11
# A soft search for the editor bundle: with Vendor/tools, slangc is baked into the distribution,
# and the export of games for Metal/OpenGL from Windows works out of the box; without it the
# build does not break.
find_program(PANDA_SLANGC_EXECUTABLE NAMES slangc HINTS ${PANDA_SLANGC_HINTS})
endif () # NOT PANDA_SHADER_BACKEND STREQUAL "D3D11"

# The MirenShaderTool host tool (include expansion on all backends, SPIR-V → GLSL/MSL) is built
# by this same tree. When cross-compiling (iOS/Android) it cannot be built — the path to the tool
# from a desktop build is passed through -DPANDA_SHADER_TOOL=<path>.
if (CMAKE_CROSSCOMPILING)
    set(PANDA_SHADER_TOOL "" CACHE FILEPATH "Path to a host-built MirenShaderTool (cross builds only)")
    if (NOT PANDA_SHADER_TOOL)
        message(FATAL_ERROR
            "Cross-compiling: pass -DPANDA_SHADER_TOOL=<path to MirenShaderTool built by a desktop build>."
        )
    endif ()
    set(PANDA_SHADER_TOOL_COMMAND "${PANDA_SHADER_TOOL}")
else ()
    set(PANDA_SHADER_TOOL_COMMAND "$<TARGET_FILE:MirenShaderTool>")
endif ()

function(_panda_get_shader_source_root SOURCE_FILE OUT_ROOT OUT_RELATIVE OUT_GROUP)
    set(PANDA_SHADER_SOURCE_ROOT "${PANDA_DIRECTORY}/resources")
    set(PANDA_EDITOR_SHADER_SOURCE_ROOT "${PANDA_ROOT_DIRECTORY}/PandaEditor/resources")

    string(FIND "${SOURCE_FILE}" "${PANDA_SHADER_SOURCE_ROOT}/" PANDA_ROOT_POS)
    if (NOT PANDA_ROOT_POS EQUAL -1)
        file(RELATIVE_PATH RELATIVE_PATH_VALUE "${PANDA_SHADER_SOURCE_ROOT}" "${SOURCE_FILE}")
        set(${OUT_ROOT} "${PANDA_SHADER_SOURCE_ROOT}" PARENT_SCOPE)
        set(${OUT_RELATIVE} "${RELATIVE_PATH_VALUE}" PARENT_SCOPE)
        set(${OUT_GROUP} ENGINE PARENT_SCOPE)
        return()
    endif ()

    string(FIND "${SOURCE_FILE}" "${PANDA_EDITOR_SHADER_SOURCE_ROOT}/" PANDA_EDITOR_ROOT_POS)
    if (NOT PANDA_EDITOR_ROOT_POS EQUAL -1)
        file(RELATIVE_PATH RELATIVE_PATH_VALUE "${PANDA_EDITOR_SHADER_SOURCE_ROOT}" "${SOURCE_FILE}")
        set(${OUT_ROOT} "${PANDA_EDITOR_SHADER_SOURCE_ROOT}" PARENT_SCOPE)
        set(${OUT_RELATIVE} "${RELATIVE_PATH_VALUE}" PARENT_SCOPE)
        set(${OUT_GROUP} EDITOR PARENT_SCOPE)
        return()
    endif ()

    # Shaders of an application that builds the engine as a subproject: PANDA_APP_SHADER_ROOTS is
    # the list of its resource roots, set by the host before add_subdirectory. <root>/<folder>/*.hlsl
    # is baked to <folder>/<file>, like the core's, and the generated folders go into the host's
    # bundle (PANDA_APP_GENERATED_RESOURCE_FOLDERS).
    foreach (APP_SHADER_ROOT IN LISTS PANDA_APP_SHADER_ROOTS)
        string(FIND "${SOURCE_FILE}" "${APP_SHADER_ROOT}/" APP_ROOT_POS)
        if (NOT APP_ROOT_POS EQUAL -1)
            file(RELATIVE_PATH RELATIVE_PATH_VALUE "${APP_SHADER_ROOT}" "${SOURCE_FILE}")
            set(${OUT_ROOT} "${APP_SHADER_ROOT}" PARENT_SCOPE)
            set(${OUT_RELATIVE} "${RELATIVE_PATH_VALUE}" PARENT_SCOPE)
            set(${OUT_GROUP} APP PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    # Module shaders (Modules/<Name>/engine/resources/<folder>/*.hlsl, context/plugins.md): baked
    # to the same relative path as the core's — <folder>/<file> — so a module loads them by its
    # own folder ("terrain-shaders/terrain.vert") and the generated folder takes the place of the
    # source one in the bundles, like default-shaders of the core.
    string(FIND "${SOURCE_FILE}" "${PANDA_MODULES_DIRECTORY}/" MODULES_ROOT_POS)
    if (NOT MODULES_ROOT_POS EQUAL -1)
        file(RELATIVE_PATH MODULE_RELATIVE "${PANDA_MODULES_DIRECTORY}" "${SOURCE_FILE}")
        string(REGEX REPLACE "^[^/]+/engine/resources/" "" RELATIVE_PATH_VALUE "${MODULE_RELATIVE}")
        set(${OUT_ROOT} "${PANDA_MODULES_DIRECTORY}" PARENT_SCOPE)
        set(${OUT_RELATIVE} "${RELATIVE_PATH_VALUE}" PARENT_SCOPE)
        set(${OUT_GROUP} ENGINE PARENT_SCOPE)
        return()
    endif ()

    message(FATAL_ERROR "Unknown shader source root for ${SOURCE_FILE}")
endfunction()

function(_panda_get_shader_stage SOURCE_FILE OUT_STAGE)
    if (SOURCE_FILE MATCHES "\\.vert\\.hlsl$")
        set(${OUT_STAGE} "vertex" PARENT_SCOPE)
    elseif (SOURCE_FILE MATCHES "\\.frag\\.hlsl$")
        set(${OUT_STAGE} "fragment" PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "Unsupported shader stage for ${SOURCE_FILE}")
    endif ()
endfunction()

function(_panda_collect_engine_shader_sources OUT_VAR)
    # The editor's shaders exist only where the editor is built (the engine as the top-level
    # project); an application that builds the engine as a subproject adds its own roots.
    set(APP_SHADER_GLOBS "")
    if (PROJECT_IS_TOP_LEVEL)
        list(APPEND APP_SHADER_GLOBS "${PANDA_ROOT_DIRECTORY}/PandaEditor/resources/*.hlsl")
    endif ()
    foreach (APP_SHADER_ROOT IN LISTS PANDA_APP_SHADER_ROOTS)
        list(APPEND APP_SHADER_GLOBS "${APP_SHADER_ROOT}/*.hlsl")
    endforeach ()
    file(GLOB_RECURSE PANDA_ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS
        "${PANDA_DIRECTORY}/resources/*.hlsl"
        "${PANDA_MODULES_DIRECTORY}/*/engine/resources/*.hlsl"
        ${APP_SHADER_GLOBS}
    )
    list(SORT PANDA_ENGINE_SHADER_SOURCES)
    set(${OUT_VAR} "${PANDA_ENGINE_SHADER_SOURCES}" PARENT_SCOPE)
endfunction()

function(_panda_prepare_shader_outputs BACKEND)
    if (BACKEND STREQUAL "OpenGL" AND (PLATFORM_IOS OR PLATFORM_ANDROID))
        set(BACKEND_LOWER "opengles")
        set(SHADER_TOOL_MODE "gles")
    elseif (BACKEND STREQUAL "OpenGL")
        set(BACKEND_LOWER "opengl")
        set(SHADER_TOOL_MODE "glsl")
    elseif (BACKEND STREQUAL "Vulkan")
        set(BACKEND_LOWER "vulkan")
    elseif (BACKEND STREQUAL "Metal")
        # MSL 2.1 covers macOS 10.14+; the runtime compiles the source (newLibraryWithSource).
        # The MSL version is hardcoded in MirenTools::compileSpvShaderToMsl.
        set(BACKEND_LOWER "metal")
        set(SHADER_TOOL_MODE "msl")
    elseif (BACKEND STREQUAL "D3D11")
        # D3D11 compiles HLSL at runtime via D3DCompile: cooked shader = the source.
        set(BACKEND_LOWER "d3d11")
    else ()
        message(FATAL_ERROR "Unsupported shader backend ${BACKEND}")
    endif ()
    set(GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated_resources/${BACKEND_LOWER}")

    _panda_collect_engine_shader_sources(SHADER_SOURCES)

    set(ALL_OUTPUTS "")
    set(ENGINE_OUTPUTS "")
    set(EDITOR_OUTPUTS "")
    set(APP_OUTPUTS "")
    set(APP_FOLDERS "") # the generated folders of the application shaders: <root>/<folder>

    foreach (SOURCE_FILE IN LISTS SHADER_SOURCES)
        _panda_get_shader_source_root("${SOURCE_FILE}" SOURCE_ROOT RELATIVE_SOURCE_PATH SHADER_GROUP)
        _panda_get_shader_stage("${SOURCE_FILE}" SHADER_STAGE)

        string(REGEX REPLACE "\\.hlsl$" "" OUTPUT_RELATIVE_PATH "${RELATIVE_SOURCE_PATH}")
        set(OUTPUT_FILE "${GENERATED_ROOT}/${OUTPUT_RELATIVE_PATH}")
        get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
        # Self-contained HLSL after the include expansion — the input of any compiler.
        set(INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/generated_resources/intermediate/${BACKEND_LOWER}")
        set(EXPANDED_FILE "${INTERMEDIATE_DIR}/${OUTPUT_RELATIVE_PATH}.hlsl")
        get_filename_component(EXPANDED_DIR "${EXPANDED_FILE}" DIRECTORY)
        set(SHADER_DEPENDS "${SOURCE_FILE}" ${PANDA_SHADER_INCLUDE_SOURCES} "${PANDA_SHADER_TOOL_COMMAND}")

        if (BACKEND STREQUAL "Vulkan")
            add_custom_command(
                OUTPUT "${OUTPUT_FILE}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPANDED_DIR}"
                COMMAND ${PANDA_SHADER_TOOL_COMMAND} expand "${SOURCE_FILE}" "${EXPANDED_FILE}" "${PANDA_SHADER_INCLUDE_ROOT}"
                COMMAND ${PANDA_SLANGC_EXECUTABLE} ${PANDA_SLANGC_ARGS} -stage ${SHADER_STAGE}
                        -o "${OUTPUT_FILE}" "${EXPANDED_FILE}"
                DEPENDS ${SHADER_DEPENDS} "${PANDA_SLANGC_EXECUTABLE}"
                VERBATIM
            )
        elseif (BACKEND STREQUAL "D3D11")
            # D3D11 compiles HLSL at runtime (D3DCompile without an include handler): the cooked
            # shader is the expanded source.
            add_custom_command(
                OUTPUT "${OUTPUT_FILE}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
                COMMAND ${PANDA_SHADER_TOOL_COMMAND} expand "${SOURCE_FILE}" "${OUTPUT_FILE}" "${PANDA_SHADER_INCLUDE_ROOT}"
                DEPENDS ${SHADER_DEPENDS}
                VERBATIM
            )
        else () # OpenGL / OpenGLES / Metal: expand → slangc → SPIR-V → MirenShaderTool → GLSL/MSL
            set(SPV_FILE "${INTERMEDIATE_DIR}/${OUTPUT_RELATIVE_PATH}.spv")
            add_custom_command(
                OUTPUT "${OUTPUT_FILE}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPANDED_DIR}"
                COMMAND ${PANDA_SHADER_TOOL_COMMAND} expand "${SOURCE_FILE}" "${EXPANDED_FILE}" "${PANDA_SHADER_INCLUDE_ROOT}"
                COMMAND ${PANDA_SLANGC_EXECUTABLE} ${PANDA_SLANGC_ARGS} -stage ${SHADER_STAGE}
                        -o "${SPV_FILE}" "${EXPANDED_FILE}"
                COMMAND ${PANDA_SHADER_TOOL_COMMAND} ${SHADER_TOOL_MODE} ${SHADER_STAGE}
                        "${SPV_FILE}" "${OUTPUT_FILE}"
                DEPENDS ${SHADER_DEPENDS} "${PANDA_SLANGC_EXECUTABLE}"
                VERBATIM
            )
        endif ()

        list(APPEND ALL_OUTPUTS "${OUTPUT_FILE}")
        list(APPEND ${SHADER_GROUP}_OUTPUTS "${OUTPUT_FILE}")
        if (SHADER_GROUP STREQUAL APP)
            string(REGEX REPLACE "/.*$" "" APP_FOLDER "${RELATIVE_SOURCE_PATH}")
            list(APPEND APP_FOLDERS "${GENERATED_ROOT}/${APP_FOLDER}")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES APP_FOLDERS)

    # The includes go into the generated root as is (the editor's include root in Resources; no
    # bake is needed for project shaders). copy, not copy_if_different: the freshness of the
    # output is determined by mtime.
    foreach (INCLUDE_FILE IN LISTS PANDA_SHADER_INCLUDE_SOURCES)
        file(RELATIVE_PATH INCLUDE_RELATIVE_PATH "${PANDA_DIRECTORY}/resources" "${INCLUDE_FILE}")
        set(OUTPUT_FILE "${GENERATED_ROOT}/${INCLUDE_RELATIVE_PATH}")
        get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
        add_custom_command(
            OUTPUT "${OUTPUT_FILE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${INCLUDE_FILE}" "${OUTPUT_FILE}"
            DEPENDS "${INCLUDE_FILE}"
            VERBATIM
        )
        list(APPEND ALL_OUTPUTS "${OUTPUT_FILE}")
        list(APPEND ENGINE_OUTPUTS "${OUTPUT_FILE}")
    endforeach ()

    # Directory-based packaging must not retain shaders whose sources moved or were deleted.
    foreach (PREVIOUS_OUTPUT IN LISTS PANDA_SHADER_BAKE_OUTPUTS)
        string(FIND "${PREVIOUS_OUTPUT}" "${GENERATED_ROOT}/" CURRENT_ROOT_POS)
        if (CURRENT_ROOT_POS EQUAL 0 AND NOT PREVIOUS_OUTPUT IN_LIST ALL_OUTPUTS)
            file(REMOVE "${PREVIOUS_OUTPUT}")
        endif ()
    endforeach ()

    set(PANDA_SHADER_GENERATED_ROOT "${GENERATED_ROOT}" CACHE INTERNAL "")
    set(PANDA_SHADER_RESOURCE_BACKEND "${BACKEND_LOWER}" CACHE INTERNAL "")
    set(PANDA_SHADER_BAKE_OUTPUTS "${ALL_OUTPUTS}" CACHE INTERNAL "")
    set(PANDA_SHADER_BAKE_TARGET "PandaShaderBake" CACHE INTERNAL "")
    set(PANDA_ENGINE_SHADER_BAKE_OUTPUTS "${ENGINE_OUTPUTS}" CACHE INTERNAL "")
    set(PANDA_EDITOR_SHADER_BAKE_OUTPUTS "${EDITOR_OUTPUTS}" CACHE INTERNAL "")
    set(PANDA_APP_SHADER_BAKE_OUTPUTS "${APP_OUTPUTS}" CACHE INTERNAL "")
    set(PANDA_ENGINE_GENERATED_RESOURCE_FOLDERS
        "${GENERATED_ROOT}/default-shaders"
        "${GENERATED_ROOT}/panda-ui"
        CACHE INTERNAL ""
    )
    set(PANDA_EDITOR_GENERATED_RESOURCE_FOLDERS
        "${GENERATED_ROOT}/editor-shaders"
        CACHE INTERNAL ""
    )
    set(PANDA_APP_GENERATED_RESOURCE_FOLDERS "${APP_FOLDERS}" CACHE INTERNAL "")
endfunction()

if (NOT PANDA_SHADER_BACKEND STREQUAL "OpenGL" AND NOT PANDA_SHADER_BACKEND STREQUAL "Vulkan" AND
    NOT PANDA_SHADER_BACKEND STREQUAL "D3D11" AND NOT PANDA_SHADER_BACKEND STREQUAL "Metal")
    message(FATAL_ERROR "Unsupported PANDA_SHADER_BACKEND='${PANDA_SHADER_BACKEND}'. Use OpenGL, Vulkan, D3D11 or Metal.")
endif ()
if (PANDA_SHADER_BACKEND STREQUAL "D3D11" AND NOT PLATFORM_WIN32)
    message(FATAL_ERROR "PANDA_SHADER_BACKEND=D3D11 is only supported on Windows.")
endif ()
if (PANDA_SHADER_BACKEND STREQUAL "Metal" AND NOT PLATFORM_MACOS AND NOT PLATFORM_IOS)
    message(FATAL_ERROR "PANDA_SHADER_BACKEND=Metal is only supported on macOS and iOS.")
endif ()

if (PANDA_SHADER_BACKEND STREQUAL "OpenGL" AND (PLATFORM_IOS OR PLATFORM_ANDROID))
    set(PANDA_SHADER_TARGET_NAME "OpenGLES")
else ()
    set(PANDA_SHADER_TARGET_NAME "${PANDA_SHADER_BACKEND}")
endif ()

message(STATUS "Engine shader resources are baked for ${PANDA_SHADER_TARGET_NAME}.")
_panda_prepare_shader_outputs("${PANDA_SHADER_BACKEND}")

add_custom_target(${PANDA_SHADER_BAKE_TARGET} DEPENDS ${PANDA_SHADER_BAKE_OUTPUTS})

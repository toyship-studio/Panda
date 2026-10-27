project(PandaEditor)

set(CMAKE_CXX_STANDARD 20)
set(EDITOR_VENDOR_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Vendor)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        src/*.cpp
        )

file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS
        src/*.h
        src/*.hpp
        )

# CONFIGURE_DEPENDS: a new resource folder gets into the bundle without a reconfigure.
file(GLOB RESOURCE_FOLDERS CONFIGURE_DEPENDS
        resources/*
        ${PANDA_DIRECTORY}/resources/*
        )

if (DESKTOP)
    if (NOT PANDA_SHADER_BACKEND STREQUAL "OpenGL" AND NOT PANDA_SHADER_BACKEND STREQUAL "D3D11" AND NOT PANDA_SHADER_BACKEND STREQUAL "Metal")
        message(FATAL_ERROR "PandaEditor supports PANDA_SHADER_BACKEND=OpenGL, D3D11 or Metal.")
    endif ()
    list(APPEND RESOURCE_FOLDERS ${PANDA_ENGINE_GENERATED_RESOURCE_FOLDERS})
    list(APPEND RESOURCE_FOLDERS ${PANDA_EDITOR_GENERATED_RESOURCE_FOLDERS})
    # Resources of the built-in modules (panda_package()): the engine parts' data folders and
    # baked shader folders, and the editor parts' folders, each as Resources/<folder>.
    get_property(PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS)
    get_property(PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS)
    get_property(PANDA_BUILTIN_MODULE_EDITOR_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_EDITOR_RESOURCE_FOLDERS)
    list(APPEND RESOURCE_FOLDERS
        ${PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS}
        ${PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS}
        ${PANDA_BUILTIN_MODULE_EDITOR_RESOURCE_FOLDERS}
    )
else()
    message(FATAL_ERROR "Only desktop platform supported for editor")
endif()

# The PandaUI gallery (Help → UI Gallery, PandaUI/examples/gallery): its images come along as
# Resources/panda-ui-gallery.
get_target_property(PANDA_UI_GALLERY_RESOURCE_FOLDER PandaUIGallery PANDAUI_GALLERY_RESOURCE_FOLDER)
list(APPEND RESOURCE_FOLDERS "${PANDA_UI_GALLERY_RESOURCE_FOLDER}")

list(FILTER RESOURCE_FOLDERS EXCLUDE REGEX "\\.DS_Store")

# The license and the third-party notices ship inside the bundle (scripts/third_party_notices.py).
list(APPEND RESOURCE_FOLDERS "${PANDA_ROOT_DIRECTORY}/LICENSE.txt" "${PANDA_ROOT_DIRECTORY}/THIRD_PARTY_NOTICES.txt")

# Application icon: the ready-made .icns is copied into Contents/Resources (see
# CFBundleIconFile in Info.plist); Windows takes the .ico through the .rc in the sources.
# Regeneration from native/AppIcon-1024.png: scripts/make_app_icons.sh
if (PLATFORM_MACOS)
    list(APPEND RESOURCE_FOLDERS "${PROJECT_SOURCE_DIR}/native/OSX/PandaEditor.icns")
elseif (PLATFORM_WIN32)
    list(APPEND SOURCES "${PROJECT_SOURCE_DIR}/native/Windows/PandaEditor.rc")
endif ()

set(PANDA_EDITOR_RESOURCE_TARGET_DEPENDS ${PANDA_SHADER_BAKE_TARGET})
set(PANDA_EDITOR_GENERATED_TOOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/GeneratedResources/Tools")
set(PANDA_EDITOR_HAS_BUNDLED_TOOLS FALSE)
if (PANDA_BUNDLED_CMAKE_AVAILABLE)
    panda_add_bundled_cmake_copy_target(PandaEditorBundledCMake "${PANDA_EDITOR_GENERATED_TOOLS_DIR}")
    list(APPEND PANDA_EDITOR_RESOURCE_TARGET_DEPENDS PandaEditorBundledCMake)
    set(PANDA_EDITOR_HAS_BUNDLED_TOOLS TRUE)
endif()
# slangc — the compiler of user shaders: with it the editor is self-sufficient (no Vulkan SDK/PATH).
if (PANDA_BUNDLED_SLANG_AVAILABLE)
    panda_add_bundled_slang_copy_target(PandaEditorBundledSlang "${PANDA_EDITOR_GENERATED_TOOLS_DIR}")
    list(APPEND PANDA_EDITOR_RESOURCE_TARGET_DEPENDS PandaEditorBundledSlang)
    set(PANDA_EDITOR_HAS_BUNDLED_TOOLS TRUE)
endif()
# Windows: the update helper finishes an update after the editor exited (App/EditorUpdater.cpp).
# It ships in Tools/ next to the other bundled tools, so it is built into the generated Tools folder.
if (PLATFORM_WIN32)
    add_executable(PandaUpdateHelper WIN32 UpdateHelper/PandaUpdateHelper.cpp)
    target_link_libraries(PandaUpdateHelper PRIVATE shell32)
    set_target_properties(PandaUpdateHelper PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PANDA_EDITOR_GENERATED_TOOLS_DIR}")
    foreach (OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
        set_target_properties(PandaUpdateHelper PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${PANDA_EDITOR_GENERATED_TOOLS_DIR}")
    endforeach ()
    list(APPEND PANDA_EDITOR_RESOURCE_TARGET_DEPENDS PandaUpdateHelper)
    set(PANDA_EDITOR_HAS_BUNDLED_TOOLS TRUE)
endif ()
if (PANDA_EDITOR_HAS_BUNDLED_TOOLS)
    list(APPEND RESOURCE_FOLDERS "${PANDA_EDITOR_GENERATED_TOOLS_DIR}")
endif()

# Native modal dialogs (NSAlert) — macOS only; the build already compiles .mm (Fern) and links Cocoa.
if (APPLE)
    list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Project/System/SystemToolsDialog.mm)
    list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Project/System/SystemColorSampler.mm)
endif()

# ---- Built-in modules (context/plugins.md): linked statically, the table of passports and entry
# points is generated from panda_package() — the editor registers the contributions of the enabled
# modules when a project is opened (ProjectModules), the editor code itself does not name modules.
get_property(PANDA_STATIC_MODULE_TARGETS GLOBAL PROPERTY PANDA_STATIC_MODULE_TARGETS)
get_property(PANDA_STATIC_MODULE_ENTRIES GLOBAL PROPERTY PANDA_STATIC_MODULE_ENTRIES)
get_property(PANDA_STATIC_MODULE_IDS GLOBAL PROPERTY PANDA_STATIC_MODULE_IDS)
get_property(PANDA_STATIC_MODULE_VERSIONS GLOBAL PROPERTY PANDA_STATIC_MODULE_VERSIONS)
get_property(PANDA_STATIC_MODULE_DEPENDENCIES GLOBAL PROPERTY PANDA_STATIC_MODULE_DEPENDENCIES)
get_property(PANDA_BUILTIN_MODULE_EDITOR_TARGETS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_EDITOR_TARGETS)
get_property(PANDA_BUILTIN_MODULE_EDITOR_ENTRIES GLOBAL PROPERTY PANDA_BUILTIN_MODULE_EDITOR_ENTRIES)
set(PANDA_BUILTIN_MODULES_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/PandaBuiltinModules.cpp")
set(_PANDA_MODULES_DECLARATIONS "")
set(_PANDA_MODULES_ROWS "")
list(LENGTH PANDA_STATIC_MODULE_ENTRIES _PANDA_MODULE_COUNT)
if (_PANDA_MODULE_COUNT GREATER 0)
    math(EXPR _PANDA_MODULE_LAST "${_PANDA_MODULE_COUNT} - 1")
    foreach (_INDEX RANGE ${_PANDA_MODULE_LAST})
        list(GET PANDA_STATIC_MODULE_ENTRIES ${_INDEX} _ENGINE_ENTRY)
        list(GET PANDA_STATIC_MODULE_IDS ${_INDEX} _MODULE_ID)
        list(GET PANDA_STATIC_MODULE_VERSIONS ${_INDEX} _MODULE_VERSION)
        list(GET PANDA_BUILTIN_MODULE_EDITOR_ENTRIES ${_INDEX} _EDITOR_ENTRY)
        list(GET PANDA_STATIC_MODULE_DEPENDENCIES ${_INDEX} _MODULE_DEPENDENCIES)
        set(_MODULE_DEPENDENCIES_ROW "")
        if (NOT _MODULE_DEPENDENCIES STREQUAL none)
            string(REPLACE "," ";" _MODULE_DEPENDENCY_LIST "${_MODULE_DEPENDENCIES}")
            foreach (_DEPENDENCY ${_MODULE_DEPENDENCY_LIST})
                string(APPEND _MODULE_DEPENDENCIES_ROW "\"${_DEPENDENCY}\", ")
            endforeach ()
        endif ()
        string(APPEND _PANDA_MODULES_DECLARATIONS "void ${_ENGINE_ENTRY}(Panda::Extensions &);\n")
        if (_EDITOR_ENTRY STREQUAL none)
            set(_EDITOR_ENTRY nullptr)
        else ()
            string(APPEND _PANDA_MODULES_DECLARATIONS "void ${_EDITOR_ENTRY}(Editor::EditorExtensions &);\n")
        endif ()
        string(APPEND _PANDA_MODULES_ROWS
            "        {\"${_MODULE_ID}\", \"${_MODULE_VERSION}\", ${_ENGINE_ENTRY}, ${_EDITOR_ENTRY}, {${_MODULE_DEPENDENCIES_ROW}}},\n")
    endforeach ()
endif ()
file(WRITE ${PANDA_BUILTIN_MODULES_SOURCE}
    "// Generated by PandaEditor/CMakeLists.txt: built-in modules linked statically into the editor.\n"
    "#include \"Project/Modules/ProjectModules.hpp\"\n\n"
    "#include <Panda/Extensions/Extensions.hpp>\n"
    "#include <PandaEditorKit/EditorExtensions.hpp>\n\n"
    "${_PANDA_MODULES_DECLARATIONS}\n"
    "namespace Panda {\n\n"
    "const std::vector<BuiltinModule> &builtinModules() {\n"
    "    static const std::vector<BuiltinModule> modules = {\n"
    "${_PANDA_MODULES_ROWS}"
    "    };\n"
    "    return modules;\n"
    "}\n\n"
    "} // namespace Panda\n"
)
list(APPEND SOURCES ${PANDA_BUILTIN_MODULES_SOURCE})

# PandaEditor target

add_app(
    PandaEditor
    "${SOURCES}"
    "${INCLUDE}"
    "${RESOURCE_FOLDERS}"
    RESOURCE_TARGET_DEPENDS
    ${PANDA_EDITOR_RESOURCE_TARGET_DEPENDS}
)

target_include_directories(PandaEditor PRIVATE ${PROJECT_SOURCE_DIR}/src)

if (MSVC)
    target_compile_options(PandaEditor PRIVATE /FS)
endif()

# set output directories for all builds (Debug, Release, etc.)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set_target_properties(PandaEditor
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIRECTORY}
    )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

set_target_properties(PandaEditor
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BIN_DIRECTORY}
)

set_target_properties(PandaEditor
    PROPERTIES 
    VS_DEBUGGER_WORKING_DIRECTORY ${BIN_DIRECTORY}
)

# Dependencies - Panda + PandaEditorKit (the editor layer) + built-in modules (both parts statically).
target_link_libraries(PandaEditor Panda PandaEditorKit ${PANDA_STATIC_MODULE_TARGETS} ${PANDA_BUILTIN_MODULE_EDITOR_TARGETS})

# Dependencies - PandaAsync
target_link_libraries(PandaEditor PandaAsync)

# Dependencies - PandaUI and its gallery (the UI Gallery tab)
target_link_libraries(PandaEditor PandaUI PandaUIGallery)

# Dependencies - Miren
target_link_libraries(PandaEditor Miren)

# Dependencies - MirenTools
target_link_libraries(PandaEditor MirenTools)

# Dependencies - nfd (Vendor/nfd, added by the root)
target_link_libraries(PandaEditor nfd)

# Dependencies - miniz (Vendor/miniz, added by the root): zip archives (Project/System/ZipArchive)
target_link_libraries(PandaEditor miniz)

if (PLATFORM_LINUX)
    # The blocking save changes dialog uses GTK3 independently of the nfd backend (GTK or portal).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PANDA_EDITOR_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_link_libraries(PandaEditor PkgConfig::PANDA_EDITOR_GTK3)
endif ()

if (PLATFORM_WIN32)
    # D3D11 shader reflection for the material inspector (D3D11ShaderReflection.cpp)
    target_link_libraries(PandaEditor d3dcompiler)
endif ()

# The editor-side API of the engine (PND_EDITOR) is the option PANDA_EDITOR_API of the root, carried
# by the Panda target to everything that links it; the editor cannot be built without it.
if (NOT PANDA_EDITOR_API)
    message(FATAL_ERROR "PandaEditor needs PANDA_EDITOR_API=ON: the editor-side API of the engine libraries.")
endif ()

# AppKit/Cocoa for native NSAlert dialogs (SystemToolsDialog.mm)
if (APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(PandaEditor ${COCOA_LIBRARY})
endif()

# ---- macOS: the existing PandaSDK into the bundle, without building it ----
# The canonical package in bin/PandaSDK is built only by the explicit SDK scripts. A regular
# editor build syncs the ready package if it exists, but does not run
# PandaSDKCorePackage/PandaSDKPackage. tools/ are not copied: the editor takes the tools
# from its own Contents/Resources/Tools.
if (PLATFORM_MACOS)
    set(PANDA_EDITOR_EMBEDDED_SDK_DIR "$<TARGET_BUNDLE_CONTENT_DIR:PandaEditor>/Resources/PandaSDK")
    set(PANDA_EDITOR_SDK_PACKAGE_DIR "${BIN_DIRECTORY}/PandaSDK")
    add_custom_target(PandaEditorEmbedSdk ALL
        COMMAND ${CMAKE_COMMAND}
            "-DPANDA_SDK_SOURCE=${PANDA_EDITOR_SDK_PACKAGE_DIR}"
            "-DPANDA_SDK_DESTINATION=${PANDA_EDITOR_EMBEDDED_SDK_DIR}"
            -P "${PANDA_ROOT_DIRECTORY}/cmake/sync_panda_sdk.cmake"
        COMMENT "Syncing existing PandaSDK into PandaEditor.app"
        VERBATIM
    )
    add_dependencies(PandaEditorEmbedSdk PandaEditor)
endif ()

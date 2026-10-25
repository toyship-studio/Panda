cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)

# The oldest macOS the editor, the SDK and exported games run on (the export scripts of a game pass
# the same value). It has to be known before project(); an explicit -DCMAKE_OSX_DEPLOYMENT_TARGET
# wins, iOS builds pass their own, and a parent project that builds the engine decides for itself.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
        AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
endif ()

project("Panda")

set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")

set(FOUNDATION_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Foundation)
set(PANDA_ASYNC_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PandaAsync)
set(FERN_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Fern)
set(MIREN_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Miren)
set(PANDA_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Panda)
set(PANDA_MODULES_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Modules)
set(PANDA_PACKAGE_CMAKE ${CMAKE_CURRENT_SOURCE_DIR}/PandaSDK/cmake/PandaPackage.cmake) # panda_package() for in-tree packages
set(PANDA_UI_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PandaUI)
set(VENDOR_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Vendor)
# The engine root for the CMake of the libraries and the applications. CMAKE_SOURCE_DIR is not it
# when an application builds the engine as a subproject (add_subdirectory of a checkout); such a
# host sets BIN_DIRECTORY and PANDA_APP_SHADER_ROOTS (cmake/shader_bake.cmake) before it.
set(PANDA_ROOT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "The Panda engine source tree")
# -DBIN_DIRECTORY=... keeps a second build (CI without modules) apart. Scripts read the
# same absolute output directory from CMakeCache rather than assuming the shared bin/.
set(BIN_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH "Application and SDK output directory")
get_filename_component(BIN_DIRECTORY "${BIN_DIRECTORY}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BIN_DIRECTORY "${BIN_DIRECTORY}" CACHE PATH "Application and SDK output directory" FORCE)

# The engine version is ENGINE_VERSION in Panda/include/Panda/Base/Version.hpp, edited by hand on a
# release; the build reads it for the bundles and the release manifest. The build number and the
# commit come from the release workflow (-DPANDA_BUILD_NUMBER=<run number>,
# -DPANDA_BUILD_COMMIT=<sha>) and reach the code as ENGINE_BUILD_NUMBER and ENGINE_BUILD_COMMIT;
# a local build is number 0 without a commit (context/infrastructure.md).
set(PANDA_VERSION_HEADER "${PANDA_DIRECTORY}/include/Panda/Base/Version.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PANDA_VERSION_HEADER}")
file(STRINGS "${PANDA_VERSION_HEADER}" _PANDA_VERSION_LINE REGEX "ENGINE_VERSION = \"")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PANDA_VERSION "${_PANDA_VERSION_LINE}")
if (NOT PANDA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "ENGINE_VERSION was not read from ${PANDA_VERSION_HEADER}: '${PANDA_VERSION}'")
endif ()
set(PANDA_BUILD_NUMBER 0 CACHE STRING "Build number of a release workflow run; 0 for a local build")
set(PANDA_BUILD_COMMIT "" CACHE STRING "Commit the release workflow built; empty for a local build")
set(PANDA_RELEASE_CHANNEL "" CACHE STRING "Release channel of a release workflow run (preview, stable); empty for a local build")
if (NOT PANDA_BUILD_NUMBER MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PANDA_BUILD_NUMBER must be a non-negative integer: '${PANDA_BUILD_NUMBER}'")
endif ()
message(STATUS "Panda ${PANDA_VERSION}, build ${PANDA_BUILD_NUMBER}")

include(cmake/vendor_warnings.cmake)
include(cmake/platform.cmake)
# The editor-side API of the engine libraries (PND_EDITOR: material inputs, asset documents and
# the like, context/architecture.md). The editor and the authoring tools need it; the SDK and the
# games do not: the SDK build scripts switch it off, so a game carries no editor code on any
# platform. Panda/CMakeLists.txt applies it; the editor is configured only with it on.
option(PANDA_EDITOR_API "Compile the engine libraries with the editor-side API (PND_EDITOR)" ${DESKTOP})
include(cmake/shader_bake.cmake)
include(cmake/bundled_cmake.cmake)
include(cmake/bundled_slang.cmake)

if (PLATFORM_LINUX)
    add_subdirectory(Vendor/glfw)
endif ()
add_subdirectory(Vendor/glad)
add_subdirectory(Vendor/glm)
add_subdirectory(Vendor/stb_image)
add_subdirectory(Vendor/stb_truetype)
add_subdirectory(Vendor/entt)
add_subdirectory(Vendor/yoga)
# Yoga is part of PandaUI's static SDK graph, linked into shared script libraries.
set_target_properties(yogacore PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory(Vendor/dylib)
add_subdirectory(Vendor/box2d)
add_subdirectory(Vendor/box3d)
add_subdirectory(Vendor/cgltf)
add_subdirectory(Vendor/spirv-cross)

if (PLATFORM_LINUX)
    panda_silence_target_warnings(glfw)
endif ()
panda_silence_target_warnings(glad)
panda_silence_target_warnings(stb_image)
panda_silence_target_warnings(stb_truetype)
panda_silence_target_warnings(yogacore)
panda_silence_target_warnings(box2d)
panda_silence_target_warnings(box3d)
# Physics is vendor code: it is built optimized in Debug (the cost of b3CreateHeightField on large
# terrains and of the world step is several times lower; debugging box3d internals is not needed).
if (NOT MSVC)
    target_compile_options(box3d PRIVATE $<$<CONFIG:Debug>:-O2>)
endif ()
panda_silence_target_warnings(cgltf)

include(cmake/add_app.cmake)

add_subdirectory(Foundation)
add_subdirectory(PandaAsync)
add_subdirectory(Fern)
add_subdirectory(Rain)
add_subdirectory(Miren)
add_subdirectory(MirenTools)
add_subdirectory(PandaUI)
add_subdirectory(Panda)
if (DESKTOP AND PANDA_EDITOR_API AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(PandaEditorKit) # the editor layer for the editor parts of modules: the editor's alone
endif ()
# Modules (context/plugins.md) are the folders of Modules/ with a module.json: cmake/modules.cmake
# finds them, gives each an option PANDA_MODULE_<FOLDER> (ON) and orders them by the dependencies
# of their manifests; PANDA_MODULES=OFF switches all of them off. The hosts take the set from the
# panda_package() properties, so a module switched off is unknown to the editor, the runtime and
# the SDK package. CI builds the engine with every module off.
include(cmake/modules.cmake)
panda_scan_modules(PANDA_ENABLED_MODULES "${PANDA_MODULES_DIRECTORY}")
foreach (_PANDA_MODULE IN LISTS PANDA_ENABLED_MODULES)
    add_subdirectory(Modules/${_PANDA_MODULE})
endforeach ()
add_subdirectory(PandaSDK)
add_subdirectory(PandaRuntime)

foreach(PANDA_ENGINE_TARGET
    Foundation
    PandaAsync
    Fern
    Rain
    Miren
    MirenTools
    Panda
    PandaSDK
    PandaUI
    PandaRuntime
)
    panda_silence_ios_precision_warnings(${PANDA_ENGINE_TARGET})
endforeach()

if (DESKTOP)
    # Native file dialogs of the desktop applications: the editor and the applications that build
    # the engine as a subproject.
    add_subdirectory(Vendor/nfd)
    set_target_properties(nfd PROPERTIES LINKER_LANGUAGE CXX)
    panda_silence_target_warnings(nfd)
endif ()
# The applications of the engine tree. An application that builds the engine as a subproject
# brings its own and gets none of these (a configured bundle target leaves an empty .app shell in
# BIN_DIRECTORY even when it is never built).
if (DESKTOP AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(PandaUITest)
    add_subdirectory(Panda/Smoke)
    if (PANDA_EDITOR_API) # the SDK trees build the engine without the editor
        add_subdirectory(PandaEditor)
    endif ()
endif ()

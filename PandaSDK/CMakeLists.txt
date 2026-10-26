project(PandaSDK)

if (POLICY CMP0090)
    cmake_policy(SET CMP0090 NEW)
endif ()

set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-DPROFILE)
endif ()

set(CMAKE_CXX_STANDARD 20)

# ---- Package directories ----
set(PANDA_SDK_PACKAGE_DIR ${BIN_DIRECTORY}/PandaSDK)
set(PANDA_SDK_PACKAGE_INCLUDE_DIR ${PANDA_SDK_PACKAGE_DIR}/include)
set(PANDA_SDK_PACKAGE_LIB_DIR ${PANDA_SDK_PACKAGE_DIR}/lib)
set(PANDA_SDK_PACKAGE_CMAKE_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/cmake/PandaSDK)
set(PANDA_SDK_PACKAGE_RESOURCES_DIR ${PANDA_SDK_PACKAGE_DIR}/resources)
set(PANDA_SDK_PACKAGE_TOOLS_DIR ${PANDA_SDK_PACKAGE_DIR}/tools)
set(PANDA_SDK_COMMON_RESOURCES_DIR ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/common)
set(PANDA_SDK_SHADER_RESOURCES_DIR ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/shaders/${PANDA_SHADER_RESOURCE_BACKEND})
if(PLATFORM_IOS)
    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/ios-simulator)
    else()
        set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/ios-device)
    endif()
elseif(PLATFORM_MACOS)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/macos)
elseif(PLATFORM_WIN32)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/windows)
elseif(PLATFORM_LINUX)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_PACKAGE_LIB_DIR}/linux)
else()
    message(FATAL_ERROR "Unsupported PandaSDK packaging platform")
endif()

# ---- PandaSDK static library (scripting API) ----
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/*.hpp include/*.hpp)

# SDK parts of built-in modules (context/plugins.md): sdk/include/Bamboo — the module's scripting
# API, sdk/src — its implementation against the host table (by name and version); registered by
# panda_package(). The headers ship in the package as include/Bamboo/…
get_property(PANDA_BUILTIN_MODULE_SDK_DIRS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_SDK_DIRS)
get_property(PANDA_BUILTIN_MODULE_DIRS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_DIRS)
set(PANDA_SDK_MODULE_INCLUDE_DIRS "")
set(PANDA_SDK_MODULE_PRIVATE_INCLUDE_DIRS "")
set(PANDA_SDK_MODULE_HEADER_COPY_COMMANDS "")
foreach (_SDK_DIR ${PANDA_BUILTIN_MODULE_SDK_DIRS})
    file(GLOB_RECURSE _SDK_SOURCES CONFIGURE_DEPENDS ${_SDK_DIR}/src/*.cpp)
    file(GLOB_RECURSE _SDK_HEADERS CONFIGURE_DEPENDS ${_SDK_DIR}/include/*.hpp)
    list(APPEND SOURCES ${_SDK_SOURCES})
    list(APPEND INCLUDE ${_SDK_HEADERS})
    list(APPEND PANDA_SDK_MODULE_INCLUDE_DIRS ${_SDK_DIR}/include)
    list(APPEND PANDA_SDK_MODULE_PRIVATE_INCLUDE_DIRS ${_SDK_DIR}/src) # the part's own headers
    list(APPEND PANDA_SDK_MODULE_HEADER_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_SDK_DIR}/include/Bamboo ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Bamboo)
endforeach ()
set(PANDA_SDK_MODULE_ENGINE_INCLUDE_DIRS "")
foreach (_MODULE_DIR ${PANDA_BUILTIN_MODULE_DIRS})
    list(APPEND PANDA_SDK_MODULE_ENGINE_INCLUDE_DIRS ${_MODULE_DIR}/engine/include) # ABI of the module's tables
endforeach ()

add_library(PandaSDK STATIC "${SOURCES}" "${INCLUDE}")
if (NOT MSVC)
    target_compile_options(PandaSDK PUBLIC -fPIC)
endif ()
add_dependencies(PandaSDK ${PANDA_SHADER_BAKE_TARGET})

set_target_properties(PandaSDK
        PROPERTIES
        # Keep each platform/configuration's archive in its own build tree. Only packaging
        # writes bin/PandaSDK: a shared archive can otherwise hide another tree's relink.
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(PandaSDK PRIVATE ${PROJECT_SOURCE_DIR}/src ${PANDA_SDK_MODULE_PRIVATE_INCLUDE_DIRS})
target_include_directories(PandaSDK PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PANDA_DIRECTORY}/include>
        $<BUILD_INTERFACE:${PANDA_UI_DIRECTORY}/include>
        $<INSTALL_INTERFACE:include>
)
foreach (_DIR ${PANDA_SDK_MODULE_INCLUDE_DIRS} ${PANDA_SDK_MODULE_ENGINE_INCLUDE_DIRS})
    target_include_directories(PandaSDK PUBLIC $<BUILD_INTERFACE:${_DIR}>)
endforeach ()
target_link_libraries(PandaSDK PUBLIC PandaUI PandaAsync Fern) # Fern: the network layer of Bamboo::Net

# ---- Configure CMake package configs ----
configure_file(
        ${PROJECT_SOURCE_DIR}/cmake/PandaSDKConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/PandaSDKConfig.cmake
        @ONLY
)

configure_file(
        ${PROJECT_SOURCE_DIR}/cmake/PandaRuntimeConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/PandaRuntimeConfig.cmake
        @ONLY
)

# Built-in modules of the distribution (context/plugins.md): the folders are registered by
# panda_package() (a global property); the SDK package carries their sources and manifest — the game
# export links the engine parts from sources (the Export/CMakeLists.txt template).
set(PANDA_SDK_MODULE_COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_DIR}/modules
)
foreach (_MODULE_DIR ${PANDA_BUILTIN_MODULE_DIRS})
    get_filename_component(_MODULE_DIR_NAME "${_MODULE_DIR}" NAME)
    list(APPEND PANDA_SDK_MODULE_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_MODULE_DIR} ${PANDA_SDK_PACKAGE_DIR}/modules/${_MODULE_DIR_NAME}
    )
endforeach ()

# Resources of the engine parts of the built-in modules: data folders into resources/common/
# <folder>, baked shader folders into resources/shaders/<backend>/<folder>, next to the core's.
# The Export template drops the folders of the modules a game does not list.
get_property(PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS)
get_property(PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS GLOBAL PROPERTY PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS)
set(PANDA_SDK_MODULE_RESOURCE_COPY_COMMANDS "")
foreach (_FOLDER ${PANDA_BUILTIN_MODULE_RUNTIME_RESOURCE_FOLDERS})
    get_filename_component(_FOLDER_NAME ${_FOLDER} NAME)
    list(APPEND PANDA_SDK_MODULE_RESOURCE_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_FOLDER} ${PANDA_SDK_COMMON_RESOURCES_DIR}/${_FOLDER_NAME})
endforeach ()
foreach (_FOLDER ${PANDA_BUILTIN_MODULE_SHADER_RESOURCE_FOLDERS})
    get_filename_component(_FOLDER_NAME ${_FOLDER} NAME)
    list(APPEND PANDA_SDK_MODULE_RESOURCE_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_FOLDER} ${PANDA_SDK_SHADER_RESOURCES_DIR}/${_FOLDER_NAME})
endforeach ()

export(PACKAGE PandaSDK)

panda_bundled_cmake_copy_commands(PANDA_SDK_BUNDLED_CMAKE_COPY_COMMANDS "${PANDA_SDK_PACKAGE_TOOLS_DIR}")
panda_bundled_slang_copy_commands(PANDA_SDK_BUNDLED_SLANG_COPY_COMMANDS "${PANDA_SDK_PACKAGE_TOOLS_DIR}")

# ---- Package everything into PandaSDK ----
# Not POST_BUILD: the package lives in bin/ and gets wiped (bin is cleaned as a whole), while
# POST_BUILD is not rerun on an up-to-date target — a wiped package would not be restored until the
# library is rebuilt. The stamp lies IN THE PACKAGE: the package is gone → the stamp is gone →
# repackaging on the next build. The same scheme as for the runtime part (PandaSDKPackage in
# PandaRuntime/CMakeLists with PandaRuntime.stamp). The dependency on bake — the package takes the
# baked shaders.
# Everything the package copies as files: the header trees of the libraries, the docs, the
# sources and manifests of the modules. A change of any of them repacks — the library alone does
# not relink on a header-only change of another library or on a module source change.
set(PANDA_SDK_PACKAGED_FILE_GLOBS
        ${PROJECT_SOURCE_DIR}/include/Bamboo/*
        ${PROJECT_SOURCE_DIR}/docs/*
        ${PANDA_ROOT_DIRECTORY}/LICENSE.txt
        ${PANDA_ROOT_DIRECTORY}/THIRD_PARTY_NOTICES.txt
        ${PROJECT_SOURCE_DIR}/cmake/PandaPackage.cmake
        ${PANDA_UI_DIRECTORY}/include/PandaUI/*
        ${PANDA_ASYNC_DIRECTORY}/include/PandaAsync/*
        ${PANDA_DIRECTORY}/include/Panda/*
        ${FOUNDATION_DIRECTORY}/include/Foundation/*
        ${MIREN_DIRECTORY}/include/Miren/*
        ${FERN_DIRECTORY}/include/Fern/*
        ${PANDA_ROOT_DIRECTORY}/Rain/include/Rain/*
        ${PANDA_ROOT_DIRECTORY}/Rain/Vendor/rapidjson/include/rapidjson/*
        ${PANDA_ROOT_DIRECTORY}/Rain/Vendor/yaml-cpp/include/yaml-cpp/*
        ${VENDOR_DIRECTORY}/glm/glm/*
        ${VENDOR_DIRECTORY}/entt/src/entt/*
        ${VENDOR_DIRECTORY}/box2d/include/box2d/*
        ${VENDOR_DIRECTORY}/box3d/include/box3d/*
)
foreach (_SDK_DIR ${PANDA_BUILTIN_MODULE_SDK_DIRS})
    list(APPEND PANDA_SDK_PACKAGED_FILE_GLOBS ${_SDK_DIR}/include/*)
endforeach ()
foreach (_MODULE_DIR ${PANDA_BUILTIN_MODULE_DIRS})
    list(APPEND PANDA_SDK_PACKAGED_FILE_GLOBS ${_MODULE_DIR}/*)
endforeach ()
file(GLOB_RECURSE PANDA_SDK_PACKAGED_FILES CONFIGURE_DEPENDS ${PANDA_SDK_PACKAGED_FILE_GLOBS})
list(FILTER PANDA_SDK_PACKAGED_FILES EXCLUDE REGEX "\\.DS_Store$")

add_custom_command(OUTPUT ${PANDA_SDK_PACKAGE_DIR}/.package.stamp
        # === 1. Scripting API headers (Bamboo) + Script ABI ===
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Bamboo
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/PandaUI
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Foundation
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/glm
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/PandaAsync
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/include/Bamboo ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Bamboo
        ${PANDA_SDK_MODULE_HEADER_COPY_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_UI_DIRECTORY}/include/PandaUI ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/PandaUI
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_ASYNC_DIRECTORY}/include/PandaAsync ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/PandaAsync
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VENDOR_DIRECTORY}/glm/glm ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/glm
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Panda/ScriptEngine/ABI
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PANDA_DIRECTORY}/include/Panda/ScriptEngine/ABI/ScriptManifest.hpp
                ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Panda/ScriptEngine/ABI/ScriptManifest.hpp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PANDA_DIRECTORY}/include/Panda/ScriptEngine/ABI/ScriptApi.hpp
                ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Panda/ScriptEngine/ABI/ScriptApi.hpp

        # === 1c. Engine and dependency headers (the game export builds modules from sources) ===
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Panda
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Foundation
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Miren
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Fern
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Rain
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/rapidjson
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/yaml-cpp
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/entt
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/box2d
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/box3d
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_DIRECTORY}/include/Panda ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Panda
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${FOUNDATION_DIRECTORY}/include/Foundation ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Foundation
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${MIREN_DIRECTORY}/include/Miren ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Miren
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${FERN_DIRECTORY}/include/Fern ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Fern
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_ROOT_DIRECTORY}/Rain/include/Rain ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/Rain
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_ROOT_DIRECTORY}/Rain/Vendor/rapidjson/include/rapidjson ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/rapidjson
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PANDA_ROOT_DIRECTORY}/Rain/Vendor/yaml-cpp/include/yaml-cpp ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/yaml-cpp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VENDOR_DIRECTORY}/entt/src/entt ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/entt
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VENDOR_DIRECTORY}/box2d/include/box2d ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/box2d
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VENDOR_DIRECTORY}/box3d/include/box3d ${PANDA_SDK_PACKAGE_INCLUDE_DIR}/box3d
        ${PANDA_SDK_MODULE_COPY_COMMANDS}

        # === 1b. Docs — the agent package (.world/.mat formats, scripting, overview) ===
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_DIR}/docs
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/docs ${PANDA_SDK_PACKAGE_DIR}/docs
        # The license and the third-party notices at the package root (scripts/third_party_notices.py).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PANDA_ROOT_DIRECTORY}/LICENSE.txt ${PANDA_SDK_PACKAGE_DIR}/LICENSE.txt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PANDA_ROOT_DIRECTORY}/THIRD_PARTY_NOTICES.txt ${PANDA_SDK_PACKAGE_DIR}/THIRD_PARTY_NOTICES.txt

        # === 2. CMake config files ===
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_PACKAGE_CMAKE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_PLATFORM_LIB_DIR}
        # Xcode's iOS TARGET_FILE may contain EFFECTIVE_PLATFORM_NAME, as in runtime packaging.
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT_FILE=$<TARGET_FILE:PandaSDK>"
                "-DOUTPUT_FILE=${PANDA_SDK_PLATFORM_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}PandaSDK${CMAKE_STATIC_LIBRARY_SUFFIX}"
                -P "${PANDA_ROOT_DIRECTORY}/cmake/copy_if_different_resolved.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_BINARY_DIR}/PandaSDKConfig.cmake
                ${PANDA_SDK_PACKAGE_CMAKE_DIR}/PandaSDKConfig.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_BINARY_DIR}/PandaRuntimeConfig.cmake
                ${PANDA_SDK_PACKAGE_CMAKE_DIR}/PandaRuntimeConfig.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/cmake/PandaPackage.cmake
                ${PANDA_SDK_PACKAGE_CMAKE_DIR}/PandaPackage.cmake
        ${PANDA_SDK_BUNDLED_CMAKE_COPY_COMMANDS}
        ${PANDA_SDK_BUNDLED_SLANG_COPY_COMMANDS}

        # === 3. Runtime resources ===
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/macos
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/ios-simulator
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/ios-device
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/windows
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/linux
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/default-shaders
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_PACKAGE_RESOURCES_DIR}/panda-ui
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_COMMON_RESOURCES_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_COMMON_RESOURCES_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${PANDA_DIRECTORY}/resources
                ${PANDA_SDK_COMMON_RESOURCES_DIR}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_COMMON_RESOURCES_DIR}/default-shaders
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_COMMON_RESOURCES_DIR}/panda-ui
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_SHADER_RESOURCES_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_SHADER_RESOURCES_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${PANDA_SHADER_GENERATED_ROOT}/default-shaders
                ${PANDA_SDK_SHADER_RESOURCES_DIR}/default-shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${PANDA_SHADER_GENERATED_ROOT}/panda-ui
                ${PANDA_SDK_SHADER_RESOURCES_DIR}/panda-ui
        ${PANDA_SDK_MODULE_RESOURCE_COPY_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E touch ${PANDA_SDK_PACKAGE_DIR}/.package.stamp
        DEPENDS PandaSDK ${PANDA_SHADER_BAKE_TARGET} ${PANDA_SDK_PACKAGED_FILES}
                "${PANDA_ROOT_DIRECTORY}/cmake/copy_if_different_resolved.cmake"
        VERBATIM
)
add_custom_target(PandaSDKCorePackage DEPENDS ${PANDA_SDK_PACKAGE_DIR}/.package.stamp)
set_target_properties(PandaSDK PandaSDKCorePackage PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---- Smoke test of the public Bamboo::Async ----
if (DESKTOP)
    add_executable(BambooAsyncSmoke test/AsyncSmokeTest.cpp)
    target_link_libraries(BambooAsyncSmoke PRIVATE PandaSDK)
    set_target_properties(BambooAsyncSmoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif ()

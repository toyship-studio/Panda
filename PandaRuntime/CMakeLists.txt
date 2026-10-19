project(PandaRuntime)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE RUNTIME_INCLUDE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

add_library(PandaRuntime STATIC "${RUNTIME_SOURCES}" "${RUNTIME_INCLUDE}")

target_include_directories(PandaRuntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(PandaRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${PANDA_DIRECTORY}/src)

target_link_libraries(PandaRuntime Panda)

# ---- Package runtime libraries into PandaSDK ----
# Exported games link PandaSDK::PandaRuntime. The SDK ships separate static
# libraries, while PandaRuntimeConfig.cmake wires them together as one target.
set(PANDA_SDK_LIB_DIR ${BIN_DIRECTORY}/PandaSDK/lib)
set(PANDA_SDK_CMAKE_DIR ${PANDA_SDK_LIB_DIR}/cmake/PandaSDK)
set(PANDA_SDK_RUNTIME_INCLUDE_DIR ${BIN_DIRECTORY}/PandaSDK/include/PandaRuntime)
if(PLATFORM_IOS)
    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_LIB_DIR}/ios-simulator)
    else()
        set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_LIB_DIR}/ios-device)
    endif()
elseif(PLATFORM_MACOS)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_LIB_DIR}/macos)
elseif(PLATFORM_WIN32)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_LIB_DIR}/windows)
elseif(PLATFORM_LINUX)
    set(PANDA_SDK_PLATFORM_LIB_DIR ${PANDA_SDK_LIB_DIR}/linux)
else()
    message(FATAL_ERROR "Unsupported PandaRuntime SDK packaging platform")
endif()
set(PANDA_RUNTIME_PACKAGE_STAMP ${PANDA_SDK_PLATFORM_LIB_DIR}/PandaRuntime.stamp)

# The actual shader backend of this SDK build. Placed next to the platform
# libraries and read by PandaRuntimeConfig when an exported game is built,
# so that the export follows the backend the libraries and shaders are built for.
file(WRITE ${CMAKE_BINARY_DIR}/PandaSDK/PandaSDKBackend.cmake
    "set(_PANDA_SDK_SHADER_BACKEND \"${PANDA_SHADER_RESOURCE_BACKEND}\")\n"
)

set(PANDA_RUNTIME_PACKAGE_TARGETS
    PandaRuntime
    Panda
    Fern
    Miren
    Rain
    Foundation
    PandaAsync
    PandaUI
    PandaSDK
    box2d
    box3d
    cgltf
    glad
    stb_image
    yogacore
    yaml-cpp
)

if(PLATFORM_LINUX)
    list(APPEND PANDA_RUNTIME_PACKAGE_TARGETS glfw)
endif()

set(PANDA_RUNTIME_PACKAGE_LIB_NAMES
    PandaRuntime
    Panda
    Fern
    Miren
    Rain
    Foundation
    PandaAsync
    PandaUI
    PandaSDK
    box2d
    box3d
    cgltf
    glad
    stb_image
    yogacore
    yaml-cpp
)

if(PLATFORM_LINUX)
    list(APPEND PANDA_RUNTIME_PACKAGE_LIB_NAMES glfw)
endif()

set(PANDA_RUNTIME_PACKAGE_COMMANDS "")
list(LENGTH PANDA_RUNTIME_PACKAGE_TARGETS _PANDA_RUNTIME_PACKAGE_TARGET_COUNT)
math(EXPR _PANDA_RUNTIME_PACKAGE_TARGET_LAST_INDEX "${_PANDA_RUNTIME_PACKAGE_TARGET_COUNT} - 1")
foreach(_INDEX RANGE ${_PANDA_RUNTIME_PACKAGE_TARGET_LAST_INDEX})
    list(GET PANDA_RUNTIME_PACKAGE_TARGETS ${_INDEX} _TARGET)
    list(GET PANDA_RUNTIME_PACKAGE_LIB_NAMES ${_INDEX} _LIB_NAME)
    list(APPEND PANDA_RUNTIME_PACKAGE_COMMANDS
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT_FILE=$<TARGET_FILE:${_TARGET}>"
            "-DOUTPUT_FILE=${PANDA_SDK_PLATFORM_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            -P "${PANDA_ROOT_DIRECTORY}/cmake/copy_if_different_resolved.cmake"
    )
endforeach()

add_custom_command(OUTPUT ${PANDA_RUNTIME_PACKAGE_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_PLATFORM_LIB_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PANDA_SDK_CMAKE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaSDKConfig.cmake
        ${PANDA_SDK_CMAKE_DIR}/PandaSDKConfig.cmake
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaRuntimeConfig.cmake
        ${PANDA_SDK_CMAKE_DIR}/PandaRuntimeConfig.cmake
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaSDKBackend.cmake
        ${PANDA_SDK_PLATFORM_LIB_DIR}/PandaSDKBackend.cmake
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PANDA_SDK_RUNTIME_INCLUDE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/include/PandaRuntime
        ${PANDA_SDK_RUNTIME_INCLUDE_DIR}
    ${PANDA_RUNTIME_PACKAGE_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E touch ${PANDA_RUNTIME_PACKAGE_STAMP}
    DEPENDS
        ${PANDA_RUNTIME_PACKAGE_TARGETS}
       
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaSDKConfig.cmake
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaRuntimeConfig.cmake
        ${CMAKE_BINARY_DIR}/PandaSDK/PandaSDKBackend.cmake
    COMMENT "Packaging runtime libraries into PandaSDK"
    VERBATIM
)

add_custom_target(PandaSDKPackage DEPENDS ${PANDA_RUNTIME_PACKAGE_STAMP})
set_target_properties(PandaRuntime PandaSDKPackage PROPERTIES EXCLUDE_FROM_ALL TRUE)

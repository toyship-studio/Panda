project(Fern)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-DPROFILE)
endif ()

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES src/Fern/*.cpp)
file(GLOB_RECURSE INCLUDE src/Fern/*.hpp include/Fern/*.hpp)

# Other platform specific realizations
if (PLATFORM_WIN32)
    file(GLOB_RECURSE GLOB_RESULT_INCLUDE src/Platform/System/Win32_System/*.hpp src/Platform/System/Win32_System/*.h)
    file(GLOB_RECURSE GLOB_RESULT_SOURCES src/Platform/System/Win32_System/*.cpp)
    list(APPEND INCLUDE ${GLOB_RESULT_INCLUDE})
    list(APPEND SOURCES ${GLOB_RESULT_SOURCES})
elseif (PLATFORM_LINUX)
    file(GLOB_RECURSE GLOB_RESULT_INCLUDE src/Platform/System/GLFW_System/*.hpp src/Platform/System/GLFW_System/*.h)
    file(GLOB_RECURSE GLOB_RESULT_SOURCES src/Platform/System/GLFW_System/*.cpp src/Platform/System/GLFW_System/*.mm)
    list(APPEND INCLUDE ${GLOB_RESULT_INCLUDE})
    list(APPEND SOURCES ${GLOB_RESULT_SOURCES})
elseif (PLATFORM_MACOS)
    file(GLOB_RECURSE GLOB_RESULT_INCLUDE src/Platform/System/OSX_System/*.hpp src/Platform/System/OSX_System/*.h)
    file(GLOB_RECURSE GLOB_RESULT_SOURCES src/Platform/System/OSX_System/*.cpp src/Platform/System/OSX_System/*.mm)
    list(APPEND INCLUDE ${GLOB_RESULT_INCLUDE})
    list(APPEND SOURCES ${GLOB_RESULT_SOURCES})
elseif (PLATFORM_IOS)
    file(GLOB_RECURSE GLOB_RESULT_INCLUDE src/Platform/System/IOS_System/*.hpp src/Platform/System/IOS_System/*.h)
    file(GLOB_RECURSE GLOB_RESULT_SOURCES src/Platform/System/IOS_System/*.cpp src/Platform/System/IOS_System/*.mm)
    list(APPEND INCLUDE ${GLOB_RESULT_INCLUDE})
    list(APPEND SOURCES ${GLOB_RESULT_SOURCES})
else ()
    message(FATAL_ERROR "Unknown platform")
endif ()

# Fern target

add_library(Fern STATIC "${SOURCES}" "${INCLUDE}" "${RESOURCE_FOLDERS}")

target_compile_definitions(Fern PUBLIC GL_SILENCE_DEPRECATION)
target_compile_definitions(Fern PUBLIC GLES_SILENCE_DEPRECATION)
target_include_directories(Fern PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(Fern PUBLIC ${PROJECT_SOURCE_DIR}/include)

if (PLATFORM_WIN32)
    # Native Win32 backend: WGL needs opengl32, the D3D11 context needs d3d11+dxgi.
    target_link_libraries(Fern opengl32 d3d11 dxgi)
    target_include_directories(Fern SYSTEM PRIVATE ${VENDOR_DIRECTORY}/glad/include)
elseif (PLATFORM_LINUX)
    # Dependencies - GLFW, only for Linux
    target_link_libraries(Fern glfw)
    target_include_directories(Fern SYSTEM PRIVATE ${VENDOR_DIRECTORY}/glfw/include)
elseif (PLATFORM_MACOS)
    find_library(COCOA Cocoa)
    find_library(IOKIT IOKit)
    find_library(OPENGL OpenGL)
    find_library(COREFOUNDATION CoreFoundation)
    find_library(METAL_LIBRARY Metal)
    find_library(QUARTZ_CORE QuartzCore)
    target_link_libraries(Fern ${COCOA} ${IOKIT} ${OPENGL} ${COREFOUNDATION} ${METAL_LIBRARY} ${QUARTZ_CORE})
elseif (PLATFORM_IOS)
    # add_definitions("-x objective-c++")
    # Locate system libraries on iOS
    find_library(UIKIT UIKit)
    find_library(OPENGLES OpenGLES)
    find_library(METAL_LIBRARY Metal)
    find_library(METALKIT_LIBRARY MetalKit)
    find_library(QUARTZ_CORE QuartzCore)
    find_library(FOUNDATION Foundation)
    find_library(MOBILECORESERVICES MobileCoreServices)
    find_library(CFNETWORK CFNetwork)
    find_library(SYSTEMCONFIGURATION SystemConfiguration)

    target_link_libraries(Fern
        ${UIKIT} ${METAL_LIBRARY} ${METALKIT_LIBRARY} ${FOUNDATION} ${CFNETWORK} ${SYSTEMCONFIGURATION} ${QUARTZ_CORE} ${OPENGLES}
    )
#    set_target_properties(
#            Fern PROPERTIES XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"
#    )
endif ()

# Dependencies - Foundation
target_link_libraries(Fern Foundation)

# Dependencies - Miren
target_link_libraries(Fern Miren)

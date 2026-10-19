project(PandaUITest)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS
    src/*.hpp
)

file(GLOB RESOURCE_FOLDERS CONFIGURE_DEPENDS
    resources/*
    ${PANDA_DIRECTORY}/resources/*
)

if (DESKTOP)
    if (NOT PANDA_SHADER_BACKEND STREQUAL "OpenGL" AND NOT PANDA_SHADER_BACKEND STREQUAL "D3D11" AND NOT PANDA_SHADER_BACKEND STREQUAL "Metal")
        message(FATAL_ERROR "PandaUITest supports PANDA_SHADER_BACKEND=OpenGL, D3D11 or Metal.")
    endif ()
    list(APPEND RESOURCE_FOLDERS ${PANDA_ENGINE_GENERATED_RESOURCE_FOLDERS})
else()
    message(FATAL_ERROR "PandaUITest currently supports only desktop platforms")
endif()

list(FILTER RESOURCE_FOLDERS EXCLUDE REGEX "\\.DS_Store")

add_app(
    PandaUITest
    "${SOURCES}"
    "${INCLUDE}"
    "${RESOURCE_FOLDERS}"
    RESOURCE_TARGET_DEPENDS
    ${PANDA_SHADER_BAKE_TARGET}
)

target_include_directories(PandaUITest PRIVATE ${PROJECT_SOURCE_DIR}/src)

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set_target_properties(PandaUITest
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_DIRECTORY}
    )
endforeach()

set_target_properties(PandaUITest
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BIN_DIRECTORY}
)

set_target_properties(PandaUITest
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${BIN_DIRECTORY}
)

target_link_libraries(PandaUITest
    PandaUI
    Panda
)

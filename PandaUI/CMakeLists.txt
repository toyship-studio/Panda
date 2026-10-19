project(PandaUI)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/PandaUI/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/PandaUI/*.hpp include/PandaUI/*.hpp)

# PandaUI target

add_library(PandaUI STATIC "${SOURCES}" "${INCLUDE}" "${RESOURCE_FOLDERS}")

# The SDK links PandaUI into shared script libraries as well as applications.
set_target_properties(PandaUI PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(PandaUI PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(PandaUI PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(PandaUI
    PUBLIC
        glm
    PRIVATE
        stb_truetype
        yogacore
)

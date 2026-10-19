project(RainSmoke)

file(GLOB_RECURSE RAIN_SMOKE_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE RAIN_SMOKE_HEADERS CONFIGURE_DEPENDS *.hpp)

add_executable(RainSmoke ${RAIN_SMOKE_SOURCES} ${RAIN_SMOKE_HEADERS})
target_link_libraries(RainSmoke PRIVATE Rain)
target_include_directories(RainSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

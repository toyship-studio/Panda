project(RainSamples)

file(GLOB_RECURSE RAIN_SAMPLE_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE RAIN_SAMPLE_HEADERS CONFIGURE_DEPENDS *.hpp)

add_executable(RainSamples ${RAIN_SAMPLE_SOURCES} ${RAIN_SAMPLE_HEADERS})
target_link_libraries(RainSamples PRIVATE Rain)
target_include_directories(RainSamples PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

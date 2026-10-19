project(MirenSmoke)

set(CMAKE_CXX_STANDARD 20)

add_executable(MirenSmoke Main.cpp)
target_link_libraries(MirenSmoke PRIVATE Miren MirenTools Fern Foundation glm)
target_include_directories(MirenSmoke PRIVATE ${VENDOR_DIRECTORY}/glm)
# Test shaders are cooked at startup from sources (slangc + MirenTools, like user shaders in the editor).
target_compile_definitions(MirenSmoke PRIVATE MIREN_SMOKE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
# Test shaders are cooked through MirenTools at runtime — slangc is the same one the build found
# (on the D3D11 backend slangc is not needed and may be absent — then it is empty).
target_compile_definitions(MirenSmoke PRIVATE MIREN_SMOKE_SLANGC="${PANDA_SLANGC_EXECUTABLE}")

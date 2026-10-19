// Host tool for shader baking (cmake/shader_bake.cmake): expansion of HLSL includes (a single
// mechanism for all backends — the compilers do not see includes) and conversion of SPIR-V to
// GLSL/MSL + reflection with the vendored SPIRV-Cross.
#include <MirenTools.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int usage() {
    std::fprintf(
        stderr,
        "usage: MirenShaderTool expand <in.hlsl> <out.hlsl> <include-root>\n"
        "       MirenShaderTool <glsl|gles|msl|msl-ios|reflect> <vertex|fragment> <in.spv> <out>\n"
    );
    return 2;
}

} // namespace

int main(int argc, const char **argv) {
    if (argc < 2) { return usage(); }
    const std::string mode = argv[1];
    if (mode == "expand") {
        if (argc != 5) { return usage(); }
        // The bake always writes the output: the CMake rule judges freshness by the output's mtime.
        const std::vector<path_t> roots = {path_t(argv[4])};
        const std::optional<std::string> text = MirenTools::expandHlslIncludes(argv[2], roots);
        if (!text) { return 1; }
        std::FILE *file = std::fopen(argv[3], "wb");
        if (!file) {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::fwrite(text->data(), 1, text->size(), file);
        std::fclose(file);
        return 0;
    }
    if (argc != 5) { return usage(); }
    const MirenTools::ShaderType stage =
        std::strcmp(argv[2], "vertex") == 0 ? MirenTools::VERTEX : MirenTools::FRAGMENT;
    const path_t input = argv[3];
    const path_t output = argv[4];
    bool ok = false;
    if (mode == "glsl") {
        ok = MirenTools::compileSpvShaderToGlsl(input, output, stage, MirenTools::GlslTarget::OpenGL);
    } else if (mode == "gles") {
        ok = MirenTools::compileSpvShaderToGlsl(input, output, stage, MirenTools::GlslTarget::OpenGLES);
    } else if (mode == "msl") {
        ok = MirenTools::compileSpvShaderToMsl(input, output, stage, MirenTools::MslTarget::MacOS);
    } else if (mode == "msl-ios") {
        ok = MirenTools::compileSpvShaderToMsl(input, output, stage, MirenTools::MslTarget::IOS);
    } else if (mode == "reflect") {
        ok = MirenTools::reflect(input, output);
    } else {
        std::fprintf(stderr, "unknown mode '%s'\n", mode.c_str());
        return 2;
    }
    return ok ? 0 : 1;
}

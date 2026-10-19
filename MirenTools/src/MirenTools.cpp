#include "MirenTools.hpp"

#include <Foundation/Foundation.hpp>

#include <spirv_cross_util.hpp>
#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_reflect.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace MirenTools {

namespace {

    std::string s_slangcExecutable = "slangc";

    std::string getSlangStage(ShaderType shaderType) {
        if (shaderType == ShaderType::VERTEX) { return "vertex"; }
        // FRAGMENT
        return "fragment";
    }

    // SPIR-V is a binary stream of 32-bit words.
    std::vector<uint32_t> readSpirvFile(const path_t &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) { return {}; }
        const std::streamsize size = file.tellg();
        if (size <= 0 || size % sizeof(uint32_t) != 0) { return {}; }
        std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char *>(words.data()), size);
        return words;
    }

    bool writeTextFile(const path_t &path, const std::string &text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) { return false; }
        file << text;
        return file.good();
    }

    std::optional<std::string> readTextFile(const path_t &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) { return std::nullopt; }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // A line of the form `#include "spec"` (whitespace is allowed) → spec; empty — not an include.
    std::string includeSpec(const std::string &line) {
        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos || line[i] != '#') { return {}; }
        i = line.find_first_not_of(" \t", i + 1);
        if (i == std::string::npos || line.compare(i, 7, "include") != 0) { return {}; }
        i = line.find_first_not_of(" \t", i + 7);
        if (i == std::string::npos || line[i] != '"') { return {}; }
        const size_t end = line.find('"', i + 1);
        if (end == std::string::npos) { return {}; }
        return line.substr(i + 1, end - i - 1);
    }

    class IncludeExpander {
    public:
        explicit IncludeExpander(const std::vector<path_t> &roots)
            : m_roots(roots) {}

        bool expand(const path_t &file, const std::string &displayName) {
            const std::optional<std::string> text = readTextFile(file);
            if (!text) {
                LOG_ERROR_EDITOR("Shader include: cannot read %s", file.string().c_str());
                return false;
            }
            m_output += "#line 1 \"" + displayName + "\"\n";
            std::istringstream lines(*text);
            std::string line;
            uint32_t lineNumber = 0;
            while (std::getline(lines, line)) {
                lineNumber++;
                if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                const std::string spec = includeSpec(line);
                if (spec.empty()) {
                    m_output += line;
                    m_output += '\n';
                    continue;
                }
                const path_t resolved = resolve(file, spec);
                if (resolved.empty()) {
                    LOG_ERROR_EDITOR("%s:%u: shader include not found: %s", displayName.c_str(), lineNumber, spec.c_str());
                    return false;
                }
                std::error_code error;
                const path_t key = std::filesystem::weakly_canonical(resolved, error);
                if (std::find(m_expanded.begin(), m_expanded.end(), key) == m_expanded.end()) {
                    m_expanded.push_back(key);
                    if (!expand(resolved, spec)) { return false; }
                }
                m_output += "#line " + std::to_string(lineNumber + 1) + " \"" + displayName + "\"\n";
            }
            return true;
        }

        std::string take() {
            return std::move(m_output);
        }

    private:
        path_t resolve(const path_t &from, const std::string &spec) const {
            const path_t local = from.parent_path() / spec;
            if (std::filesystem::is_regular_file(local)) { return local; }
            for (const path_t &root : m_roots) {
                const path_t candidate = root / spec;
                if (std::filesystem::is_regular_file(candidate)) { return candidate; }
            }
            return {};
        }

        const std::vector<path_t> &m_roots;
        std::vector<path_t> m_expanded;
        std::string m_output;
    };

    // dxc named the cbuffer TYPE "type.<variable>" — the block name resolution in the GL backend
    // (after sanitization GLSL gets "type_<variable>") and the reflection (whitelist
    // "type.MATERIAL_FIELDS") rely on this. slangc gives the type its own name
    // (SLANG_ParameterGroup_X_std140) but keeps the VARIABLE name — the dxc convention is restored
    // from the variable name. For dxc SPIR-V the renaming is idempotent.
    void restoreUboTypeNames(spirv_cross::Compiler &compiler) {
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        for (const spirv_cross::Resource &ubo : resources.uniform_buffers) {
            const std::string variableName = compiler.get_name(ubo.id);
            if (!variableName.empty()) { compiler.set_name(ubo.base_type_id, "type." + variableName); }
        }
    }

} // namespace

void setSlangcExecutable(const path_t &path) {
    if (!path.empty()) { s_slangcExecutable = path.string(); }
}

std::optional<std::string> expandHlslIncludes(const path_t &input, const std::vector<path_t> &includeRoots) {
    IncludeExpander expander(includeRoots);
    if (!expander.expand(input, input.filename().string())) { return std::nullopt; }
    return expander.take();
}

bool expandHlslIncludesToFile(const path_t &input, const path_t &output, const std::vector<path_t> &includeRoots) {
    const std::optional<std::string> text = expandHlslIncludes(input, includeRoots);
    if (!text) { return false; }
    if (readTextFile(output) == text) { return true; }
    std::filesystem::create_directories(output.parent_path());
    if (!writeTextFile(output, *text)) {
        LOG_ERROR_EDITOR("Error: expanded shader %s was not written", output.string().c_str());
        return false;
    }
    return true;
}

bool compileHlslShaderToSpv(const path_t &input, const path_t &output, ShaderType shaderType) {
    // -matrix-layout-column-major — parity with dxc: UBOs are packed by Std140Buffer from
    // column-major glm matrices.
    std::string compileCmd = "\"" + s_slangcExecutable + "\" -lang hlsl -matrix-layout-column-major -stage " +
                             getSlangStage(shaderType) + " -entry main -target spirv -o \"" + output.string() +
                             "\" \"" + input.string() + "\"";

    LOG_INFO("EXECUTING: %s", compileCmd.c_str());
    int result = system(compileCmd.c_str());

    if (result != 0) {
        LOG_ERROR_EDITOR("Error compiling HLSL to SPIR-V (code: %d)", result);
        if (std::filesystem::exists(output)) { std::filesystem::remove(output); }
        return false;
    }

    if (std::filesystem::exists(output)) {
        LOG_INFO_EDITOR("Successfully created %s", output.c_str());
    } else {
        LOG_ERROR_EDITOR("Error: SPIR-V file was not created");
    }
    return true;
}

bool compileSpvShaderToGlsl(const path_t &input, const path_t &output, ShaderType shaderType, GlslTarget target) {
    try {
        std::vector<uint32_t> spirv = readSpirvFile(input);
        if (spirv.empty()) {
            LOG_ERROR_EDITOR("Error reading SPIR-V file %s", input.string().c_str());
            return false;
        }
        spirv_cross::CompilerGLSL compiler(std::move(spirv));
        spirv_cross::CompilerGLSL::Options options;
        const bool es = target == GlslTarget::OpenGLES;
        options.version = es ? 300 : 330;
        options.es = es;
        options.enable_420pack_extension = false;
        compiler.set_common_options(options);
        restoreUboTypeNames(compiler);
        // GLSL without Vulkan semantics cannot do separate image/sampler: combining and the
        // SPIRV_Cross_Combined<texture><sampler> names — as the spirv-cross CLI did (the texture
        // binding of the GL backend relies on these names, see OpenGLShader::getOpenGlTextureName).
        const uint32_t dummySampler = compiler.build_dummy_sampler_for_combined_images();
        if (dummySampler != 0) {
            compiler.set_decoration(dummySampler, spv::DecorationDescriptorSet, 0);
            compiler.set_decoration(dummySampler, spv::DecorationBinding, 0);
        }
        compiler.build_combined_image_samplers();
        for (const spirv_cross::CombinedImageSampler &remap : compiler.get_combined_image_samplers()) {
            const std::string imageName = compiler.get_name(remap.image_id);
            // Texture.Load has no sampler in HLSL. Keep its generated GLSL
            // uniform compatible with Miren's texture-name binding convention.
            const std::string samplerName = remap.sampler_id == dummySampler
                                                ? imageName + "Sampler"
                                                : compiler.get_name(remap.sampler_id);
            compiler.set_name(
                remap.combined_id,
                "SPIRV_Cross_Combined" + imageName + samplerName
            );
        }
        // Interface variables — to the common names fragVar<location>: GL links the vertex and
        // fragment shaders by names, and the frontends name them in their own way.
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        const auto &interfaceVariables =
            shaderType == ShaderType::VERTEX ? resources.stage_outputs : resources.stage_inputs;
        for (uint32_t location = 0; location < 10; location++) {
            spirv_cross_util::rename_interface_variable(
                compiler, interfaceVariables, location, "fragVar" + std::to_string(location)
            );
        }
        std::string source = compiler.compile();
        if (es) {
            // spirv-cross does not let the default precision be set, and mediump (fp16) breaks the
            // lighting: degamma/PBR/tonemap are computed with a visible loss of precision. The
            // default is raised to highp (in ES 3.0 highp is mandatory in fragment shaders).
            const std::string mediumpFloat = "precision mediump float;";
            const size_t pos = source.find(mediumpFloat);
            if (pos != std::string::npos) { source.replace(pos, mediumpFloat.size(), "precision highp float;"); }
        }
        if (!writeTextFile(output, source)) {
            LOG_ERROR_EDITOR("Error: glsl file was not created");
            return false;
        }
        LOG_INFO_EDITOR("Successfully created %s", output.c_str());
        return true;
    } catch (const std::exception &error) {
        LOG_ERROR_EDITOR("Error compiling SPIR-V to GLSL: %s", error.what());
        return false;
    }
}

bool compileSpvShaderToMsl(const path_t &input, const path_t &output, ShaderType shaderType, MslTarget target) {
    (void)shaderType; // the MSL conversion has no stage-dependent parameters
    try {
        std::vector<uint32_t> spirv = readSpirvFile(input);
        if (spirv.empty()) {
            LOG_ERROR_EDITOR("Error reading SPIR-V file %s", input.string().c_str());
            return false;
        }
        spirv_cross::CompilerMSL compiler(std::move(spirv));
        spirv_cross::CompilerMSL::Options options;
        // MSL 2.1 = macOS 10.14+ / iOS 12+; the version is in sync with shader_bake.cmake.
        options.set_msl_version(2, 1);
        options.platform = target == MslTarget::IOS ? spirv_cross::CompilerMSL::Options::iOS
                                                    : spirv_cross::CompilerMSL::Options::macOS;
        compiler.set_msl_options(options);
        restoreUboTypeNames(compiler);
        const std::string source = compiler.compile();
        if (!writeTextFile(output, source)) {
            LOG_ERROR_EDITOR("Error: msl file was not created");
            return false;
        }
        LOG_INFO_EDITOR("Successfully created %s", output.c_str());
        return true;
    } catch (const std::exception &error) {
        LOG_ERROR_EDITOR("Error compiling SPIR-V to MSL: %s", error.what());
        return false;
    }
}

bool reflect(const path_t &input, const path_t &output) {
    try {
        std::vector<uint32_t> spirv = readSpirvFile(input);
        if (spirv.empty()) {
            LOG_ERROR_EDITOR("Error reading SPIR-V file %s", input.string().c_str());
            return false;
        }
        spirv_cross::CompilerReflection compiler(std::move(spirv));
        restoreUboTypeNames(compiler);
        const std::string json = compiler.compile();
        if (!writeTextFile(output, json)) {
            LOG_ERROR_EDITOR("Error: json reflection file was not created");
            return false;
        }
        LOG_INFO_EDITOR("Successfully created %s", output.c_str());
        return true;
    } catch (const std::exception &error) {
        LOG_ERROR_EDITOR("Error reflecting SPIR-V: %s", error.what());
        return false;
    }
}

} // namespace MirenTools

#include "MirenTools.hpp"

#include <Foundation/Foundation.hpp>

#include <spirv_cross_util.hpp>
#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_reflect.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace MirenTools {

namespace {

    std::string s_slangcExecutable = "slangc";

    // Runs a command and returns its exit code; `output` gets what it printed (stdout and stderr):
    // a compiler's diagnostics belong in the log of the caller, not in the terminal of the process.
    int runCommand(const std::string &command, std::string &output) {
#ifdef _WIN32
        // cmd.exe strips the outer pair of quotes of a command that starts with one.
        FILE *pipe = _popen(("\"" + command + " 2>&1\"").c_str(), "r");
#else
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
#endif
        if (!pipe) { return -1; }
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
#ifdef _WIN32
        return _pclose(pipe);
#else
        return pclose(pipe);
#endif
    }

    // The lines a compiler printed, each as a message of the editor log (the Output panel).
    void logCompilerOutput(const std::string &output, bool failed) {
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            // Lines of pure decoration (the frame a compiler draws around a snippet) are dropped.
            if (std::none_of(line.begin(), line.end(), [](unsigned char c) { return std::isalnum(c); })) { continue; }
            if (failed) {
                LOG_ERROR_EDITOR("%s", line.c_str());
            } else {
                LOG_WARN_EDITOR("%s", line.c_str());
            }
        }
    }

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

    // The directive of a preprocessor line ("#  endif // x" — "endif"); empty — not a directive.
    std::string directiveOf(const std::string &line) {
        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos || line[i] != '#') { return {}; }
        i = line.find_first_not_of(" \t", i + 1);
        if (i == std::string::npos) { return {}; }
        size_t end = i;
        while (end < line.size() && std::isalpha(static_cast<unsigned char>(line[end]))) { end++; }
        return line.substr(i, end - i);
    }

    class IncludeExpander {
    public:
        explicit IncludeExpander(const std::vector<path_t> &roots)
            : m_roots(roots) {}

        // Text in front of the translation, as if it were the file displayName.
        void prepend(const std::string &text, const std::string &displayName) {
            m_output += "#line 1 \"" + displayName + "\"\n";
            m_output += text;
            if (m_output.empty() || m_output.back() != '\n') { m_output += '\n'; }
        }

        // The translation: the file with every include inserted in place.
        bool expandRoot(const path_t &file) {
            m_stack.push_back(canonical(file));
            return expand(file, file.filename().string());
        }

        std::string take() {
            return std::move(m_output);
        }

    private:
        bool expand(const path_t &file, const std::string &displayName) {
            const std::optional<std::string> text = readTextFile(file);
            if (!text) {
                LOG_ERROR_EDITOR("Shader include: cannot read %s", file.string().c_str());
                return false;
            }
            m_output += lineDirective(1, displayName);
            std::istringstream lines(*text);
            std::string line;
            uint32_t lineNumber = 0;
            while (std::getline(lines, line)) {
                lineNumber++;
                if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                const std::string directive = directiveOf(line);
                const std::string spec = directive == "include" ? includeSpec(line) : std::string();
                if (spec.empty()) {
                    m_output += line;
                    m_output += '\n';
                    // The compiler skips the #line of an include together with a false branch around
                    // it: the position is restored after every directive that ends a branch.
                    if (directive == "else" || directive == "elif" || directive == "endif") {
                        m_output += lineDirective(lineNumber + 1, displayName);
                    }
                    continue;
                }
                const path_t resolved = resolve(file, spec);
                if (resolved.empty()) {
                    LOG_ERROR_EDITOR("%s:%u: shader include not found: %s", displayName.c_str(), lineNumber, spec.c_str());
                    return false;
                }
                if (!include(resolved, spec)) { return false; }
                m_output += lineDirective(lineNumber + 1, displayName);
            }
            return true;
        }

        // An included file goes in at every #include, under an include guard: the compiler, which
        // knows the branches, takes the first occurrence that is live. A file that is being
        // expanded right now (a cycle) is skipped — its guard is already defined at that point.
        bool include(const path_t &file, const std::string &displayName) {
            const path_t key = canonical(file);
            if (std::find(m_stack.begin(), m_stack.end(), key) != m_stack.end()) { return true; }
            size_t index = std::find(m_files.begin(), m_files.end(), key) - m_files.begin();
            if (index == m_files.size()) { m_files.push_back(key); }
            const std::string guard = "PANDA_INCLUDED_" + std::to_string(index);
            m_output += "#ifndef " + guard + "\n#define " + guard + "\n";
            m_stack.push_back(key);
            const bool expanded = expand(file, displayName);
            m_stack.pop_back();
            m_output += "#endif\n";
            return expanded;
        }

        static path_t canonical(const path_t &file) {
            std::error_code error;
            return std::filesystem::weakly_canonical(file, error);
        }

        static std::string lineDirective(uint32_t lineNumber, const std::string &displayName) {
            return "#line " + std::to_string(lineNumber) + " \"" + displayName + "\"\n";
        }

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
        std::vector<path_t> m_files; // the included files of the translation: the index names the guard
        std::vector<path_t> m_stack; // the files being expanded right now
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

std::optional<std::string>
expandHlslIncludes(const path_t &input, const std::vector<path_t> &includeRoots, const std::string &prelude) {
    IncludeExpander expander(includeRoots);
    if (!prelude.empty()) { expander.prepend(prelude, "panda/features.hlsli"); }
    if (!expander.expandRoot(input)) { return std::nullopt; }
    return expander.take();
}

bool expandHlslIncludesToFile(
    const path_t &input, const path_t &output, const std::vector<path_t> &includeRoots, const std::string &prelude
) {
    const std::optional<std::string> text = expandHlslIncludes(input, includeRoots, prelude);
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
    // The diagnostics point into the sources (the #line directives of the include expander).
    std::string compilerOutput;
    const int result = runCommand(compileCmd, compilerOutput);
    logCompilerOutput(compilerOutput, result != 0);

    if (result != 0) {
        LOG_ERROR_EDITOR("Shader compilation failed: %s", input.filename().string().c_str());
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

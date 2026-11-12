#include "ShaderCook.hpp"
#include "D3D11ShaderReflection.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <MirenTools.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Renderer/ShaderFeatures.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderInterface.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

#include <cctype>
#include <fstream>
#include <sstream>

namespace Panda {

namespace {

    bool isInputNewer(const path_t &inputPath, const path_t &outputPath) {
        if (!std::filesystem::exists(outputPath)) { return true; }
        return std::filesystem::last_write_time(inputPath) > std::filesystem::last_write_time(outputPath);
    }

    // Scan of the `// @color` annotations on vec4 members of MATERIAL_FIELDS: the field name is
    // taken from the declaration on the same line (`float4 baseColorFactor; // @color`).
    // Backend-independent: bytecode reflection (DXBC and SPIR-V) does not preserve comments. The
    // expanded source is scanned — annotations from includes are visible too.
    std::vector<std::string> scanColorFieldAnnotations(const path_t &fragmentSourcePath) {
        std::vector<std::string> colorFields;
        std::ifstream file(fragmentSourcePath);
        if (!file.is_open()) { return colorFields; }
        std::string line;
        while (std::getline(file, line)) {
            const size_t comment = line.find("//");
            if (comment == std::string::npos) { continue; }
            if (line.find("@color", comment) == std::string::npos) { continue; }
            // The last identifier before ';' in the declaration preceding the comment.
            std::string decl = line.substr(0, comment);
            const size_t semicolon = decl.rfind(';');
            if (semicolon == std::string::npos) { continue; }
            size_t end = semicolon;
            while (end > 0 && std::isspace(static_cast<unsigned char>(decl[end - 1]))) { --end; }
            size_t begin = end;
            while (begin > 0 &&
                   (std::isalnum(static_cast<unsigned char>(decl[begin - 1])) || decl[begin - 1] == '_')) {
                --begin;
            }
            if (begin < end) { colorFields.emplace_back(decl.substr(begin, end - begin)); }
        }
        return colorFields;
    }

    // The file the expanded text of a stage is kept in (D3D11 cooks the expanded source itself).
    path_t expandedPath(const path_t &logical, const path_t &projectPath, Foundation::GraphicsAPI backend) {
        if (backend == Foundation::GraphicsAPI::D3D11) {
            return projectPath / AssetImporterBase::getCookedShaderAssetPath(logical, backend);
        }
        return projectPath / AssetImporterBase::getIntermediateShaderExpandedPath(logical);
    }

    bool isStageStale(
        const path_t &source,
        const path_t &logical,
        const path_t &projectPath,
        Foundation::GraphicsAPI backend,
        const std::string &prelude
    ) {
        const std::optional<std::string> text =
            MirenTools::expandHlslIncludes(source, {AssetImporterBase::getShaderIncludeRoot()}, prelude);
        if (!text) { return true; }
        std::ifstream file(expandedPath(logical, projectPath, backend), std::ios::binary);
        if (!file.is_open()) { return true; }
        std::ostringstream cooked;
        cooked << file.rdbuf();
        return cooked.str() != *text;
    }

    bool cookStage(
        const path_t &source,
        const path_t &logical,
        const path_t &projectPath,
        Foundation::GraphicsAPI backend,
        MirenTools::ShaderType stage,
        const std::string &prelude,
        path_t &cooked,
        path_t &expanded,
        path_t &spv
    ) {
        const std::vector<path_t> includeRoots = {AssetImporterBase::getShaderIncludeRoot()};
        cooked = projectPath / AssetImporterBase::getCookedShaderAssetPath(logical, backend);
        expanded = projectPath / AssetImporterBase::getIntermediateShaderExpandedPath(logical);
        spv = projectPath / AssetImporterBase::getIntermediateShaderSpvPath(logical);
        std::filesystem::create_directories(cooked.parent_path());
        if (backend == Foundation::GraphicsAPI::D3D11) {
            // D3D11 compiles HLSL at runtime (D3DCompile without an include handler): the cooked
            // shader is the expanded source.
            return MirenTools::expandHlslIncludesToFile(source, cooked, includeRoots, prelude);
        }
        std::filesystem::create_directories(spv.parent_path());
        // The expanded file is written only on change — from there the chain goes by mtime.
        if (!MirenTools::expandHlslIncludesToFile(source, expanded, includeRoots, prelude)) { return false; }
        if (isInputNewer(expanded, spv) && !MirenTools::compileHlslShaderToSpv(expanded, spv, stage)) { return false; }
        if (!isInputNewer(spv, cooked)) { return true; }
        switch (backend) {
            case Foundation::GraphicsAPI::OpenGL:
                return MirenTools::compileSpvShaderToGlsl(spv, cooked, stage, MirenTools::GlslTarget::OpenGL);
            case Foundation::GraphicsAPI::OpenGLES:
                return MirenTools::compileSpvShaderToGlsl(spv, cooked, stage, MirenTools::GlslTarget::OpenGLES);
            case Foundation::GraphicsAPI::Metal:
                // The MSL source is platform-neutral for our set of shaders and is compiled on the
                // device (newLibraryWithSource): a single "metal" cook serves macOS and iOS.
                return MirenTools::compileSpvShaderToMsl(spv, cooked, stage);
            case Foundation::GraphicsAPI::Vulkan:
                std::filesystem::copy_file(spv, cooked, std::filesystem::copy_options::overwrite_existing);
                return true;
            case Foundation::GraphicsAPI::D3D11:
                break;
        }
        return false;
    }

} // namespace

ShaderCookRequest ShaderCookRequest::forProjectShader(
    const path_t &vertexSource, const path_t &fragmentSource, const path_t &projectPath
) {
    ShaderCookRequest request;
    request.vertexSource = vertexSource;
    request.fragmentSource = fragmentSource;
    request.vertexLogical = std::filesystem::relative(vertexSource, projectPath);
    request.fragmentLogical = std::filesystem::relative(fragmentSource, projectPath);
    request.projectPath = projectPath;
    return request;
}

namespace ShaderCook {

    bool isStale(const ShaderCookRequest &request, Foundation::GraphicsAPI backend) {
        const std::string prelude = featuresPrelude();
        return isStageStale(request.vertexSource, request.vertexLogical, request.projectPath, backend, prelude) ||
               isStageStale(request.fragmentSource, request.fragmentLogical, request.projectPath, backend, prelude);
    }

    std::string featuresPrelude() {
        return ShaderInterface::featuresInclude(ShaderFeatures::enabled());
    }

    bool cook(
        const ShaderCookRequest &request,
        Foundation::GraphicsAPI backend,
        ShaderCookResult &result,
        ShaderSpirvReflectionData *reflection
    ) {
        const std::string prelude = featuresPrelude();
        path_t vertexExpanded;
        path_t vertexSpv;
        path_t fragmentExpanded;
        path_t fragmentSpv;
        if (!cookStage(
                request.vertexSource, request.vertexLogical, request.projectPath, backend, MirenTools::VERTEX, prelude,
                result.cookedVertex, vertexExpanded, vertexSpv
            )) {
            return false;
        }
        if (!cookStage(
                request.fragmentSource, request.fragmentLogical, request.projectPath, backend, MirenTools::FRAGMENT,
                prelude, result.cookedFragment, fragmentExpanded, fragmentSpv
            )) {
            return false;
        }
        if (reflection == nullptr) { return true; }
        if (backend == Foundation::GraphicsAPI::D3D11) {
#ifdef PLATFORM_WINDOWS
            *reflection = reflectD3D11FragmentShader(result.cookedFragment);
            reflection->color_fields = scanColorFieldAnnotations(result.cookedFragment);
            return true;
#else
            return false;
#endif
        }
        const path_t reflectionPath =
            request.projectPath / AssetImporterBase::getShaderReflectionAssetPath(request.fragmentLogical);
        std::filesystem::create_directories(reflectionPath.parent_path());
        if (isInputNewer(fragmentSpv, reflectionPath) && !MirenTools::reflect(fragmentSpv, reflectionPath)) {
            return false;
        }
        std::ifstream file(reflectionPath);
        if (!file.is_open()) {
            LOG_ERROR_EDITOR("Error reading reflection file %s", reflectionPath.string().c_str());
            return false;
        }
        Rain::JsonDecoder decoder;
        static_cast<Rain::Decoder &>(decoder).decode(file, *reflection);
        reflection->color_fields = scanColorFieldAnnotations(fragmentExpanded);
        return true;
    }

    bool cookSceneShader(
        const SceneShader &shader, const path_t &projectPath, Foundation::GraphicsAPI backend, ShaderCookResult &result
    ) {
        const path_t resources = Fern::getStaticResourcesPath();
        ShaderCookRequest request;
        request.vertexSource = resources / shader.vertex;
        request.fragmentSource = resources / shader.fragment;
        request.vertexLogical = SceneShaderRegistry::projectSourcePath(shader.vertex);
        request.fragmentLogical = SceneShaderRegistry::projectSourcePath(shader.fragment);
        request.projectPath = projectPath;
        if (!std::filesystem::exists(request.vertexSource) || !std::filesystem::exists(request.fragmentSource)) {
            LOG_ERROR_EDITOR("Scene shader %s: the sources are not in the bundle resources", shader.vertex.c_str());
            return false;
        }
        return cook(request, backend, result);
    }

    void cookAndReloadSceneShaders(const path_t &projectPath) {
        const Foundation::GraphicsAPI backend = Foundation::ACTIVE_GRAPHICS_API;
        for (const SceneShader &shader : GameContext::getExtensions().sceneShaders.shaders()) {
            ShaderCookResult result;
            if (!cookSceneShader(shader, projectPath, backend, result)) {
                LOG_ERROR_EDITOR("Scene shader %s failed to compile for the project's features", shader.vertex.c_str());
                continue;
            }
            if (ShaderAsset *asset = shader.shader.get()) { asset->reload(result.cookedVertex, result.cookedFragment); }
        }
    }

} // namespace ShaderCook

} // namespace Panda

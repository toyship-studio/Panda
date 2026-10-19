//
// Created by Andreichev Mikhail on 26.09.2025.
//

#include "EditorShaderAsset.hpp"
#include "D3D11ShaderReflection.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <MirenTools.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <cctype>
#include <fstream>

namespace Panda {

namespace {

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

    // Empty reflection — no data (not "a shader without UBOs"): the light is bound, as with binary
    // ones.
    bool declaresSceneLights(const ShaderSpirvReflectionData &reflection) {
        if (reflection.ubos.empty()) { return true; }
        for (const auto &ubo : reflection.ubos) {
            if (ubo.name == "type.SCENE_LIGHTS" || ubo.name == "SCENE_LIGHTS") { return true; }
        }
        return false;
    }

    bool declaresImage(const ShaderSpirvReflectionData &reflection, const char *name) {
        for (const auto &image : reflection.separate_images) {
            if (image.name == name) { return true; }
        }
        return false;
    }

} // namespace

EditorShaderAsset::EditorShaderAsset(
    const path_t &vertexSourcePath, const path_t &fragmentSourcePath, const path_t &projectPath
) {
    const path_t relativeVertexSourcePath = std::filesystem::relative(vertexSourcePath, projectPath);
    const path_t relativeFragmentSourcePath = std::filesystem::relative(fragmentSourcePath, projectPath);
    const Foundation::GraphicsAPI backend = Foundation::ACTIVE_GRAPHICS_API;

    // Includes are expanded BEFORE any compiler (a single mechanism on all backends); the expanded
    // file is written only on change — after that the chain goes by mtime.
    const std::vector<path_t> includeRoots = {AssetImporterBase::getShaderIncludeRoot()};
    const path_t vertexExpandedPath =
        projectPath / AssetImporterBase::getIntermediateShaderExpandedPath(relativeVertexSourcePath);
    const path_t fragmentExpandedPath =
        projectPath / AssetImporterBase::getIntermediateShaderExpandedPath(relativeFragmentSourcePath);
    const path_t vertexSpvPath =
        projectPath / AssetImporterBase::getIntermediateShaderSpvPath(relativeVertexSourcePath);
    const path_t fragmentSpvPath =
        projectPath / AssetImporterBase::getIntermediateShaderSpvPath(relativeFragmentSourcePath);
    const path_t fragmentReflectPath =
        projectPath / AssetImporterBase::getShaderReflectionAssetPath(relativeFragmentSourcePath);
    const path_t cookedVertexPath =
        projectPath / AssetImporterBase::getCookedShaderAssetPath(relativeVertexSourcePath, backend);
    const path_t cookedFragmentPath =
        projectPath / AssetImporterBase::getCookedShaderAssetPath(relativeFragmentSourcePath, backend);

#ifdef PLATFORM_WINDOWS
    if (backend == Foundation::GraphicsAPI::D3D11) {
        // D3D11 compiles HLSL at runtime (D3DCompile without an include handler): the cooked shader
        // is the expanded source, the reflection is taken from it as well.
        if (!MirenTools::expandHlslIncludesToFile(vertexSourcePath, cookedVertexPath, includeRoots)) { return; }
        if (!MirenTools::expandHlslIncludesToFile(fragmentSourcePath, cookedFragmentPath, includeRoots)) { return; }
        m_reflection = reflectD3D11FragmentShader(cookedFragmentPath);
        m_reflection.color_fields = scanColorFieldAnnotations(cookedFragmentPath);
        setSystemInputs(
            declaresSceneLights(m_reflection), declaresImage(m_reflection, "shadowMap"),
            declaresImage(m_reflection, "occlusionVolume"), declaresImage(m_reflection, "environmentMap")
        );
        create(cookedVertexPath, cookedFragmentPath);
        return;
    }
#endif

    std::filesystem::create_directories(vertexSpvPath.parent_path());
    std::filesystem::create_directories(fragmentSpvPath.parent_path());
    std::filesystem::create_directories(fragmentReflectPath.parent_path());
    std::filesystem::create_directories(cookedVertexPath.parent_path());
    std::filesystem::create_directories(cookedFragmentPath.parent_path());

    if (!MirenTools::expandHlslIncludesToFile(vertexSourcePath, vertexExpandedPath, includeRoots)) { return; }
    if (!MirenTools::expandHlslIncludesToFile(fragmentSourcePath, fragmentExpandedPath, includeRoots)) { return; }
    if (isInputNewer(vertexExpandedPath, vertexSpvPath)) {
        if (!MirenTools::compileHlslShaderToSpv(vertexExpandedPath, vertexSpvPath, MirenTools::VERTEX)) { return; }
    }
    if (isInputNewer(fragmentExpandedPath, fragmentSpvPath)) {
        if (!MirenTools::compileHlslShaderToSpv(fragmentExpandedPath, fragmentSpvPath, MirenTools::FRAGMENT)) {
            return;
        }
    }
    if (isInputNewer(fragmentSpvPath, fragmentReflectPath)) {
        if (!MirenTools::reflect(fragmentSpvPath, fragmentReflectPath)) { return; }
    }
    std::ifstream file(fragmentReflectPath);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Error reading reflection file %s", fragmentReflectPath.string().c_str());
        return;
    }
    Rain::Decoder *decoder = new Rain::JsonDecoder;
    decoder->decode(file, m_reflection);
    delete decoder;
    m_reflection.color_fields = scanColorFieldAnnotations(fragmentExpandedPath);
    setSystemInputs(
        declaresSceneLights(m_reflection), declaresImage(m_reflection, "shadowMap"),
        declaresImage(m_reflection, "occlusionVolume"), declaresImage(m_reflection, "environmentMap")
    );
    if (backend == Foundation::GraphicsAPI::OpenGL || backend == Foundation::GraphicsAPI::OpenGLES) {
        const MirenTools::GlslTarget target = backend == Foundation::GraphicsAPI::OpenGLES
                                                  ? MirenTools::GlslTarget::OpenGLES
                                                  : MirenTools::GlslTarget::OpenGL;
        if (isInputNewer(vertexSpvPath, cookedVertexPath)) {
            if (!MirenTools::compileSpvShaderToGlsl(vertexSpvPath, cookedVertexPath, MirenTools::VERTEX, target)) {
                return;
            }
        }
        if (isInputNewer(fragmentSpvPath, cookedFragmentPath)) {
            if (!MirenTools::compileSpvShaderToGlsl(
                    fragmentSpvPath, cookedFragmentPath, MirenTools::FRAGMENT, target
                )) {
                return;
            }
        }
    } else if (backend == Foundation::GraphicsAPI::Metal) {
        if (isInputNewer(vertexSpvPath, cookedVertexPath)) {
            if (!MirenTools::compileSpvShaderToMsl(vertexSpvPath, cookedVertexPath, MirenTools::VERTEX)) {
                return;
            }
        }
        if (isInputNewer(fragmentSpvPath, cookedFragmentPath)) {
            if (!MirenTools::compileSpvShaderToMsl(fragmentSpvPath, cookedFragmentPath, MirenTools::FRAGMENT)) {
                return;
            }
        }
    } else {
        if (isInputNewer(vertexSpvPath, cookedVertexPath)) {
            std::filesystem::copy_file(
                vertexSpvPath, cookedVertexPath, std::filesystem::copy_options::overwrite_existing
            );
        }
        if (isInputNewer(fragmentSpvPath, cookedFragmentPath)) {
            std::filesystem::copy_file(
                fragmentSpvPath, cookedFragmentPath, std::filesystem::copy_options::overwrite_existing
            );
        }
    }
    create(cookedVertexPath, cookedFragmentPath);
}

ShaderSpirvReflectionData EditorShaderAsset::getReflectionData() {
    return m_reflection;
}

bool EditorShaderAsset::isInputNewer(const path_t &inputPath, const path_t &outputPath) {
    if (!std::filesystem::exists(outputPath)) { return true; }
    auto inputTime = std::filesystem::last_write_time(inputPath);
    auto outputTime = std::filesystem::last_write_time(outputPath);
    return inputTime > outputTime;
}

} // namespace Panda

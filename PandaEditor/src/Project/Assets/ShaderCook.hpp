#pragma once

#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Extensions/ResourceProviders.hpp>
#include <Panda/Renderer/ShaderSpirvReflectionData.hpp>

#include <string>

namespace Panda {

// The cooking chain of one shader pair for a project — ONE implementation for the project shaders
// (EditorShaderAsset), the engine's scene shaders (Extensions::sceneShaders) and the export: the
// includes are expanded with the project's features prelude in front
// (ShaderInterface::featuresInclude) → SPIR-V (slangc) → the text of the backend (GLSL / MSL) or
// the expanded source itself (D3D11 compiles HLSL at runtime); the fragment reflection gives the
// material fields. The cache lives under <project>/.Panda/Shaders by the logical source path
// (AssetImporterBase); every step runs only when its input is newer than its output — a changed
// source, include or feature set recompiles exactly what it touches.
struct ShaderCookRequest {
    path_t vertexSource;   // absolute path of the .vert.hlsl
    path_t fragmentSource; // absolute path of the .frag.hlsl
    // Project-relative paths that name the cache files: the source path of a project shader, or
    // `engine/<resource path>` of a scene shader (SceneShaderRegistry::projectSourcePath).
    path_t vertexLogical;
    path_t fragmentLogical;
    path_t projectPath;
};

struct ShaderCookResult {
    path_t cookedVertex; // absolute paths of the cooked pair of the backend
    path_t cookedFragment;
};

namespace ShaderCook {

    // The prelude of the project: the generated `panda/features.hlsli` of the enabled features.
    std::string featuresPrelude();
    // false — a step failed (logged: an include not found, a compiler error with file:line).
    // reflection — the fragment reflection for the material fields, when wanted.
    bool cook(
        const ShaderCookRequest &request,
        Foundation::GraphicsAPI backend,
        ShaderCookResult &result,
        ShaderSpirvReflectionData *reflection = nullptr
    );
    // A scene shader of the engine (its sources are in the bundle resources) for a backend.
    bool cookSceneShader(
        const SceneShader &shader, const path_t &projectPath, Foundation::GraphicsAPI backend, ShaderCookResult &result
    );
    // Every scene shader for the active backend: cooked into the project cache and reloaded in
    // place. Called on project open and when the feature set of the project changes; a shader
    // that fails keeps its program (the error is in the log).
    void cookAndReloadSceneShaders(const path_t &projectPath);

} // namespace ShaderCook

} // namespace Panda

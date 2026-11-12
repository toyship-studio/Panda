//
// Created by Andreichev Mikhail on 26.09.2025.
//

#include "EditorShaderAsset.hpp"
#include "ShaderCook.hpp"

#include <Foundation/GraphicsAPI.hpp>

namespace Panda {

EditorShaderAsset::EditorShaderAsset(
    const path_t &vertexSourcePath, const path_t &fragmentSourcePath, const path_t &projectPath
) {
    const ShaderCookRequest request =
        ShaderCookRequest::forProjectShader(vertexSourcePath, fragmentSourcePath, projectPath);
    ShaderCookResult result;
    if (!ShaderCook::cook(request, Foundation::ACTIVE_GRAPHICS_API, result, &m_reflection)) { return; }
    reload(result.cookedVertex, result.cookedFragment);
}

ShaderSpirvReflectionData EditorShaderAsset::getReflectionData() {
    return m_reflection;
}

} // namespace Panda

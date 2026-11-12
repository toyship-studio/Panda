//
// Created by Andreichev Mikhail on 26.09.2025.
//

#pragma once

#include <Panda/Assets/ShaderAsset.hpp>
#include <Panda/Renderer/ShaderSpirvReflectionData.hpp>

namespace Panda {

/// A project shader from its sources: cooked for the active backend with the project's shader
/// features (ShaderCook), the fragment reflection gives the material fields.
class EditorShaderAsset final : public ShaderAsset {
public:
    EditorShaderAsset(const path_t &vertexSourcePath, const path_t &fragmentSourcePath, const path_t &projectPath);
    ShaderSpirvReflectionData getReflectionData() override;

private:
    ShaderSpirvReflectionData m_reflection;
};

} // namespace Panda

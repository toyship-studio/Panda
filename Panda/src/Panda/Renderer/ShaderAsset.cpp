#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"

namespace Panda {

ShaderAsset::ShaderAsset()
    : Asset(AssetType::SHADER)
    , m_handle(MIREN_INVALID_HANDLE) {}

ShaderAsset::ShaderAsset(const path_t &vertexBinPath, const path_t &fragmentBinPath)
    : Asset(AssetType::SHADER)
    , m_handle(MIREN_INVALID_HANDLE) {
    create(vertexBinPath, fragmentBinPath);
}

ShaderAsset::ShaderAsset(ShaderAsset &&other)
    : Asset(AssetType::SHADER)
    , m_usesSceneLights(other.m_usesSceneLights)
    , m_usesShadowMap(other.m_usesShadowMap)
    , m_usesOcclusionVolume(other.m_usesOcclusionVolume) {
    m_handle = other.m_handle;
    other.m_handle = MIREN_INVALID_HANDLE;
}

void ShaderAsset::create(const path_t &vertexBinPath, const path_t &fragmentBinPath) {
    m_handle = Miren::createProgram(Panda::AssetImporterBase::loadShaderProgram(vertexBinPath, fragmentBinPath));
}

ShaderAsset::~ShaderAsset() {
    if (m_handle.isValid()) { Miren::deleteProgram(m_handle); }
}

Miren::ProgramHandle ShaderAsset::getMirenHandle() {
    return m_handle;
}

#ifdef PND_EDITOR
ShaderSpirvReflectionData ShaderAsset::getReflectionData() {
    return ShaderSpirvReflectionData();
}
#endif

} // namespace Panda

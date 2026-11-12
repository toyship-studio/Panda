#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"

namespace Panda {

ShaderAsset::ShaderAsset()
    : Asset(AssetType::SHADER)
    , m_handle(MIREN_INVALID_HANDLE) {}

ShaderAsset::ShaderAsset(const path_t &vertexBinPath, const path_t &fragmentBinPath)
    : Asset(AssetType::SHADER)
    , m_vertexBinPath(vertexBinPath)
    , m_fragmentBinPath(fragmentBinPath)
    , m_handle(MIREN_INVALID_HANDLE) {}

void ShaderAsset::reload(const path_t &vertexBinPath, const path_t &fragmentBinPath) {
    if (m_handle.isValid()) { Miren::deleteProgram(m_handle); }
    m_handle = MIREN_INVALID_HANDLE;
    m_vertexBinPath = vertexBinPath;
    m_fragmentBinPath = fragmentBinPath;
}

ShaderAsset::~ShaderAsset() {
    if (m_handle.isValid()) { Miren::deleteProgram(m_handle); }
}

Miren::ProgramHandle ShaderAsset::getMirenHandle() {
    if (!m_handle.isValid() && !m_vertexBinPath.empty()) {
        m_handle = Miren::createProgram(AssetImporterBase::loadShaderProgram(m_vertexBinPath, m_fragmentBinPath));
    }
    return m_handle;
}

#ifdef PND_EDITOR
ShaderSpirvReflectionData ShaderAsset::getReflectionData() {
    return ShaderSpirvReflectionData();
}
#endif

} // namespace Panda

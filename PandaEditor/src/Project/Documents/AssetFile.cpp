#include "Project/Documents/AssetFile.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

namespace Panda {

AssetFile::AssetFile(AssetHandlerEditor &handler, AssetId id, bool changedOnDisk)
    : m_handler(handler)
    , m_id(id)
    , m_changedOnDisk(changedOnDisk) {}

std::string AssetFile::title() const {
    return "\"" + m_handler.getAssetName(m_id) + "\"";
}

path_t AssetFile::path() const {
    return m_handler.getProjectPath() / m_handler.assetPath(m_id);
}

bool AssetFile::isDirty() const {
    return m_handler.documents().isAssetChanged(m_id);
}

bool AssetFile::save() {
    return m_handler.documents().saveAsset(m_id);
}

bool AssetFile::reload() {
    m_handler.reload(m_id); // the same path as "Don't Save" in a tab
    return true;
}

} // namespace Panda

#include "UI/Panels/ContentBrowserDataSource.hpp"

#include "Preview/ThumbnailsManager.hpp"
#include "UI/EditorModelHelper.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/GameLogic/GameContext.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>

namespace Editor {

namespace {

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // Sorting of FS entries by name, ignoring case.
    bool nameLess(const std::string &a, const std::string &b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) < std::tolower(y);
            }
        );
    }

} // namespace

ContentBrowserDataSource::ContentBrowserDataSource() {
    rebuild();
}

void ContentBrowserDataSource::rebuild() {
    const Panda::path_t previousPath = m_currentFolder ? m_currentFolder->path : Panda::path_t();
    m_root = std::make_shared<Folder>();
    m_root->name = "Assets";
    auto *handler = assetHandler();
    if (handler && !handler->getProjectPath().empty()) {
        m_root->path = handler->getProjectPath() / "Assets";
        std::function<void(Folder &)> scan = [&](Folder &folder) {
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(folder.path, ec)) {
                const std::string name = entry.path().filename().string();
                if (name.empty() || name.front() == '.') { continue; } // hidden FS entries
                if (name.ends_with(".pmeta")) { continue; } // asset sidecars — service files
                if (entry.is_directory(ec)) {
                    auto child = std::make_shared<Folder>();
                    child->name = name;
                    child->path = entry.path();
                    child->parent = &folder;
                    folder.children.push_back(std::move(child));
                } else {
                    FileEntry file;
                    file.name = name;
                    file.path = entry.path();
                    // The scan on open/Refresh imports everything supported by itself:
                    // assetId == 0 remains only for files of formats foreign to the engine.
                    file.assetId = handler->getAssetId(entry.path());
                    folder.files.push_back(std::move(file));
                }
            }
            std::sort(folder.children.begin(), folder.children.end(), [](const auto &a, const auto &b) {
                return nameLess(a->name, b->name);
            });
            std::sort(folder.files.begin(), folder.files.end(), [](const FileEntry &a, const FileEntry &b) {
                return nameLess(a.name, b.name);
            });
            for (auto &child : folder.children) { scan(*child); }
        };
        scan(*m_root);
    }
    m_currentFolder = findFolder(m_root.get(), previousPath);
    if (!m_currentFolder) { m_currentFolder = m_root.get(); }
    if (auto *thumbnails = Panda::ThumbnailsManager::shared()) { thumbnails->cancelPending(); }
}

void ContentBrowserDataSource::setCurrentFolder(Folder *folder) {
    if (!folder) { return; }
    m_currentFolder = folder;
    // Outstanding thumbnail requests are about the previous folder; configureCell/prefetch will
    // request new ones.
    if (auto *thumbnails = Panda::ThumbnailsManager::shared()) { thumbnails->cancelPending(); }
}

ContentBrowserDataSource::Folder *
ContentBrowserDataSource::findFolder(Folder *node, const Panda::path_t &path) const {
    if (!node || path.empty()) { return nullptr; }
    if (node->path == path) { return node; }
    for (const auto &child : node->children) {
        if (Folder *found = findFolder(child.get(), path)) { return found; }
    }
    return nullptr;
}

int ContentBrowserDataSource::itemCount() const {
    return m_currentFolder ? static_cast<int>(m_currentFolder->children.size() + m_currentFolder->files.size())
                           : 0;
}

int ContentBrowserDataSource::folderCount() const {
    return m_currentFolder ? static_cast<int>(m_currentFolder->children.size()) : 0;
}

ContentBrowserDataSource::Folder *ContentBrowserDataSource::folderAt(int index) const {
    if (!m_currentFolder || index < 0 || index >= folderCount()) { return nullptr; }
    return m_currentFolder->children[index].get();
}

const ContentBrowserDataSource::FileEntry *ContentBrowserDataSource::fileAt(int index) const {
    if (!m_currentFolder) { return nullptr; }
    const int fileIndex = index - folderCount();
    if (fileIndex < 0 || fileIndex >= static_cast<int>(m_currentFolder->files.size())) { return nullptr; }
    return &m_currentFolder->files[fileIndex];
}

Panda::AssetId ContentBrowserDataSource::thumbnailAssetAt(int index) const {
    const FileEntry *file = fileAt(index);
    if (!file || file->assetId == 0) { return 0; }
    auto *handler = assetHandler();
    if (!handler) { return 0; }
    const auto &registry = handler->getRegistry();
    auto it = registry.find(file->assetId);
    // ThumbnailsManager can draw thumbnails: a material as a sphere, a texture as a blit into a square.
    if (it == registry.end()) { return 0; }
    const Panda::AssetType type = it->second.type;
    if (type != Panda::AssetType::MATERIAL && type != Panda::AssetType::TEXTURE) { return 0; }
    return file->assetId;
}

int ContentBrowserDataSource::indexOfAsset(Panda::AssetId assetId) const {
    if (!m_currentFolder || assetId == 0) { return -1; }
    const int folders = folderCount();
    for (int i = 0; i < static_cast<int>(m_currentFolder->files.size()); ++i) {
        if (m_currentFolder->files[i].assetId == assetId) { return folders + i; }
    }
    return -1;
}

PandaUI::TextureHandle ContentBrowserDataSource::thumbnailFor(int index) const {
    const Panda::AssetId assetId = thumbnailAssetAt(index);
    if (!assetId) { return PandaUI::TextureHandle(); }
    auto *thumbnails = Panda::ThumbnailsManager::shared();
    return thumbnails ? thumbnails->thumbnailFor(assetId) : PandaUI::TextureHandle();
}

void ContentBrowserDataSource::prefetchItems(const std::vector<int> &indices) const {
    auto *thumbnails = Panda::ThumbnailsManager::shared();
    if (!thumbnails) { return; }
    for (int index : indices) {
        if (const Panda::AssetId assetId = thumbnailAssetAt(index)) { thumbnails->prefetch(assetId); }
    }
}

void ContentBrowserDataSource::cancelPrefetching(const std::vector<int> &) const {
    // Targeted cancellation is not needed: the thumbnail queue is reset as a whole on a folder
    // change, while the already built ones stay in the ThumbnailsManager cache.
}

} // namespace Editor

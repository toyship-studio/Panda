#include "UI/Panels/ContentBrowserDataSource.hpp"

#include "Preview/AssetPreviews.hpp"
#include "UI/EditorModelHelper.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/GameLogic/GameContext.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>

namespace Editor {

namespace {


    // The order of FS entries, as a person reads names: the case does not matter and a run of
    // digits is a number — "level2" is before "level10". It is also the order a dragged folder
    // gives its assets in (the items of a list field).
    bool nameLess(const std::string &a, const std::string &b) {
        const auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
        size_t i = 0;
        size_t j = 0;
        while (i < a.size() && j < b.size()) {
            if (digit(a[i]) && digit(b[j])) {
                size_t endA = i;
                size_t endB = j;
                while (endA < a.size() && digit(a[endA])) { endA++; }
                while (endB < b.size() && digit(b[endB])) { endB++; }
                while (i + 1 < endA && a[i] == '0') { i++; } // leading zeros do not make a number larger
                while (j + 1 < endB && b[j] == '0') { j++; }
                if (endA - i != endB - j) { return endA - i < endB - j; }
                const int order = a.compare(i, endA - i, b, j, endB - j);
                if (order != 0) { return order < 0; }
                i = endA;
                j = endB;
                continue;
            }
            const int x = std::tolower(static_cast<unsigned char>(a[i]));
            const int y = std::tolower(static_cast<unsigned char>(b[j]));
            if (x != y) { return x < y; }
            i++;
            j++;
        }
        if (a.size() - i != b.size() - j) { return a.size() - i < b.size() - j; }
        return a < b; // the same name but for the case or the zeros: any order, always the same
    }

} // namespace

ContentBrowserDataSource::ContentBrowserDataSource() {
    rebuild();
}

void ContentBrowserDataSource::rebuild() {
    const Panda::path_t previousPath = m_currentFolder ? m_currentFolder->path : Panda::path_t();
    m_root = std::make_shared<Folder>();
    m_root->name = Panda::AssetHandlerEditor::ASSETS_FOLDER;
    auto *handler = Panda::AssetHandlerEditor::current();
    if (handler && !handler->getProjectPath().empty()) {
        m_root->path = handler->assetsPath();
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
                    // The scan of the project imports everything supported by itself:
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
    if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->cancelPending(); }
}

void ContentBrowserDataSource::setCurrentFolder(Folder *folder) {
    if (!folder) { return; }
    m_currentFolder = folder;
    // Outstanding thumbnail requests are about the previous folder; configureCell/prefetch will
    // request new ones.
    if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->cancelPending(); }
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
    auto *handler = Panda::AssetHandlerEditor::current();
    if (!handler) { return 0; }
    const auto &registry = handler->getRegistry();
    auto it = registry.find(file->assetId);
    // The kinds that have a picture of their own (a material, a model, a texture); the rest
    // keep the icon of their kind.
    if (it == registry.end() || !Panda::AssetPreviews::hasPreview(it->second.type)) { return 0; }
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

int ContentBrowserDataSource::indexOfFolder(const Panda::path_t &path) const {
    if (!m_currentFolder) { return -1; }
    for (int i = 0; i < static_cast<int>(m_currentFolder->children.size()); ++i) {
        if (m_currentFolder->children[static_cast<size_t>(i)]->path == path) { return i; }
    }
    return -1;
}

PandaUI::TextureHandle ContentBrowserDataSource::thumbnailFor(int index) const {
    const Panda::AssetId assetId = thumbnailAssetAt(index);
    if (!assetId) { return PandaUI::TextureHandle(); }
    auto *thumbnails = Panda::AssetPreviews::shared();
    return thumbnails ? thumbnails->thumbnailFor(assetId) : PandaUI::TextureHandle();
}

void ContentBrowserDataSource::prefetchItems(const std::vector<int> &indices) const {
    auto *thumbnails = Panda::AssetPreviews::shared();
    if (!thumbnails) { return; }
    for (int index : indices) {
        if (const Panda::AssetId assetId = thumbnailAssetAt(index)) { thumbnails->prefetch(assetId); }
    }
}

void ContentBrowserDataSource::cancelPrefetching(const std::vector<int> &) const {
    // Targeted cancellation is not needed: the thumbnail queue is reset as a whole on a folder
    // change, while the already built ones stay in the cache of AssetPreviews.
}

} // namespace Editor

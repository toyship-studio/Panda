#pragma once

#include <PandaUI/Core/TextureHandle.hpp>

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId
#include <Panda/Assets/Base/Path.hpp>  // Panda::path_t

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// MODEL of ContentBrowser: the tree of the real file system <project>/Assets + the grid data of the
// current folder + thumbnails of its items (via ThumbnailsManager: the cache — immediately, the rest
// is prefetched). On a folder change/rescan it cancels its own outstanding thumbnail requests and
// serves the new data from then on. The View stays thin: cells/menus/breadcrumbs read everything
// from here.
class ContentBrowserDataSource final {
public:
    // A file inside Assets; assetId == 0 — not registered in the registry.
    struct FileEntry {
        std::string name;
        Panda::path_t path;
        Panda::AssetId assetId = 0;
    };
    struct Folder {
        std::string name;
        Panda::path_t path;
        std::vector<std::shared_ptr<Folder>> children;
        std::vector<FileEntry> files;
        Folder *parent = nullptr;
    };

    ContentBrowserDataSource();

    void rebuild();                       // FS scan; the current folder is restored by path
    void setCurrentFolder(Folder *folder); // folder change: cancel thumbnail prefetch
    Folder *root() const {
        return m_root.get();
    }
    Folder *currentFolder() const {
        return m_currentFolder;
    }

    // The grid of the current folder: subfolders first, then files.
    int itemCount() const;
    int folderCount() const;
    Folder *folderAt(int index) const;          // null if index is not a subfolder
    const FileEntry *fileAt(int index) const;   // null if index is not a file

    // Thumbnail of a grid item (so far materials only): cache → a valid handle, otherwise built
    // asynchronously.
    PandaUI::TextureHandle thumbnailFor(int index) const;
    // Index of a current-folder item by asset (-1 — not in this folder); for a targeted cell update.
    int indexOfAsset(Panda::AssetId assetId) const;
    // CollectionView prefetch: request thumbnails ahead of time / cancel the stale ones.
    void prefetchItems(const std::vector<int> &indices) const;
    void cancelPrefetching(const std::vector<int> &indices) const;

private:
    Folder *findFolder(Folder *node, const Panda::path_t &path) const;
    Panda::AssetId thumbnailAssetAt(int index) const; // id of the material for the thumbnail (0 — none)

    std::shared_ptr<Folder> m_root;
    Folder *m_currentFolder = nullptr;
};

} // namespace Editor

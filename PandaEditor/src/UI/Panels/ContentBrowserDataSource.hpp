#pragma once

#include <PandaUI/Core/TextureHandle.hpp>

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId
#include <Panda/Assets/Base/Path.hpp>  // Panda::path_t

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// MODEL of ContentBrowser: the tree of the real file system <project>/Assets, what the grid shows
// (the current folder, or what the search found) and the thumbnails of its items (via
// AssetPreviews: the cache — immediately, the rest is prefetched). On a folder change, a search or
// a rescan it cancels its own outstanding thumbnail requests and serves the new data from then on.
// The View stays thin: cells, rows, menus and breadcrumbs read everything from here.
class ContentBrowserDataSource final {
public:
    struct Folder;
    // A file or a folder inside Assets: a cell of the grid, a row of the list.
    struct Entry {
        std::string name;
        std::string foldedName; // the name in lower case: what the search compares
        Panda::path_t path;
        Folder *parent = nullptr; // null — the root, Assets itself
        bool isFolder = false;
    };
    // A file; assetId == 0 — not registered in the registry (a format foreign to the engine).
    struct FileEntry final : Entry {
        Panda::AssetId assetId = 0;
    };
    // A folder: its subfolders and its files, each in the order of nameLess.
    struct Folder final : Entry {
        Folder() {
            isFolder = true;
        }
        std::vector<std::shared_ptr<Folder>> children;
        std::vector<FileEntry> files;
    };

    ContentBrowserDataSource();

    // FS scan; the current folder is restored by path. Every entry is made anew: what pointed at
    // the old ones (a tree view) is to let go of them first.
    void rebuild();
    void setCurrentFolder(Folder *folder); // folder change: cancel thumbnail prefetch
    Folder *root() const {
        return m_root.get();
    }
    Folder *currentFolder() const {
        return m_currentFolder;
    }
    Folder *findFolder(const Panda::path_t &path) const; // null — no such folder (any more)
    Entry *findEntry(const Panda::path_t &path) const;   // a folder or a file
    FileEntry *findAsset(Panda::AssetId assetId) const;  // the file of a registered asset

    // The children of a folder as the list shows them: its subfolders, then its files.
    int childCount(const Folder &folder) const;
    Entry *childAt(Folder &folder, int index) const;

    // The search: every folder and file of Assets whose name has the text, in any case — the folders
    // first, each by name as a person reads it (nameLess). Empty — no search.
    void setSearch(const std::string &text);
    const std::string &search() const {
        return m_search;
    }
    bool isSearching() const {
        return !m_search.empty();
    }
    const std::vector<Entry *> &found() const {
        return m_found;
    }

    // The grid: the current folder (subfolders first, then files), or what the search found.
    int itemCount() const;
    Entry *entryAt(int index) const;
    Folder *folderAt(int index) const;        // null if index is not a folder
    const FileEntry *fileAt(int index) const; // null if index is not a file
    int indexOfFolder(const Panda::path_t &path) const; // -1 — not in the grid (any more)
    int indexOfAsset(Panda::AssetId assetId) const;     // -1 — not in the grid; for a targeted cell update

    // Thumbnail of a grid item (the kinds AssetPreviews draws): cache → a valid handle, otherwise
    // built asynchronously.
    PandaUI::TextureHandle thumbnailFor(int index) const;
    // The same for a file wherever it is shown (a row of the list).
    PandaUI::TextureHandle thumbnailOf(const FileEntry &file) const;
    // CollectionView prefetch: request thumbnails ahead of time / cancel the stale ones.
    void prefetchItems(const std::vector<int> &indices) const;
    void cancelPrefetching(const std::vector<int> &indices) const;

private:
    void relist(); // what the grid shows, after the folder, the search or the tree changed
    void orderByName(); // m_byName of the tree
    Panda::AssetId thumbnailAssetAt(int index) const; // the asset for the thumbnail (0 — none)
    Panda::AssetId thumbnailAssetOf(const FileEntry *file) const;

    std::shared_ptr<Folder> m_root;
    Folder *m_currentFolder = nullptr;
    std::string m_search;
    // Every entry but the root in the order of a search's result — folders first, by name, then by
    // folder — made by the first search of a tree: a search filters it, it sorts nothing as the
    // text is typed (20 000 files, a debug build: 1 ms a key instead of 120).
    std::vector<Entry *> m_byName;
    std::vector<Entry *> m_found; // the search's result (empty without a search)
    std::vector<Entry *> m_listing; // what the grid shows
};

} // namespace Editor

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

    std::string folded(const std::string &text) {
        std::string result = text;
        for (char &c : result) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return result;
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
                    child->foldedName = folded(name);
                    child->path = entry.path();
                    child->parent = &folder;
                    folder.children.push_back(std::move(child));
                } else {
                    FileEntry file;
                    file.name = name;
                    file.foldedName = folded(name);
                    file.path = entry.path();
                    file.parent = &folder;
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
    m_byName.clear(); // of the old tree: the next search orders the new one
    m_currentFolder = findFolder(previousPath);
    if (!m_currentFolder) { m_currentFolder = m_root.get(); }
    setSearch(m_search); // the found entries were of the old tree
    if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->cancelPending(); }
}

void ContentBrowserDataSource::setCurrentFolder(Folder *folder) {
    if (!folder) { return; }
    m_currentFolder = folder;
    relist();
    // Outstanding thumbnail requests are about the previous folder; configureCell/prefetch will
    // request new ones.
    if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->cancelPending(); }
}

ContentBrowserDataSource::Folder *ContentBrowserDataSource::findFolder(const Panda::path_t &path) const {
    Entry *entry = findEntry(path);
    return entry && entry->isFolder ? static_cast<Folder *>(entry) : nullptr;
}

ContentBrowserDataSource::Entry *ContentBrowserDataSource::findEntry(const Panda::path_t &path) const {
    if (!m_root || path.empty()) { return nullptr; }
    // Down the folders that hold the path: the entry is under the one whose path begins its own.
    Folder *folder = m_root.get();
    if (folder->path == path) { return folder; }
    for (;;) {
        Folder *next = nullptr;
        for (const auto &child : folder->children) {
            if (child->path == path) { return child.get(); }
            const auto mismatch = std::mismatch(child->path.begin(), child->path.end(), path.begin(), path.end());
            if (mismatch.first == child->path.end()) {
                next = child.get();
                break;
            }
        }
        if (!next) { break; }
        folder = next;
    }
    for (FileEntry &file : folder->files) {
        if (file.path == path) { return &file; }
    }
    return nullptr;
}

ContentBrowserDataSource::FileEntry *ContentBrowserDataSource::findAsset(Panda::AssetId assetId) const {
    if (!m_root || assetId == 0) { return nullptr; }
    // The main file of the asset (the vertex source of a shader pair: both files are its cells).
    if (auto *handler = Panda::AssetHandlerEditor::current()) {
        Entry *main = findEntry(handler->getProjectPath() / handler->assetPath(assetId));
        if (main != nullptr && !main->isFolder) { return static_cast<FileEntry *>(main); }
    }
    std::vector<Folder *> pending = {m_root.get()};
    while (!pending.empty()) {
        Folder *folder = pending.back();
        pending.pop_back();
        for (FileEntry &file : folder->files) {
            if (file.assetId == assetId) { return &file; }
        }
        for (const auto &child : folder->children) { pending.push_back(child.get()); }
    }
    return nullptr;
}

int ContentBrowserDataSource::childCount(const Folder &folder) const {
    return static_cast<int>(folder.children.size() + folder.files.size());
}

ContentBrowserDataSource::Entry *ContentBrowserDataSource::childAt(Folder &folder, int index) const {
    const int folders = static_cast<int>(folder.children.size());
    if (index < 0) { return nullptr; }
    if (index < folders) { return folder.children[static_cast<size_t>(index)].get(); }
    const size_t file = static_cast<size_t>(index - folders);
    return file < folder.files.size() ? &folder.files[file] : nullptr;
}

void ContentBrowserDataSource::orderByName() {
    // Folders first, by name; two of one name by folder.
    m_byName.clear();
    std::vector<Entry *> files;
    std::vector<Folder *> pending = {m_root.get()};
    while (!pending.empty()) {
        Folder *folder = pending.back();
        pending.pop_back();
        for (const auto &child : folder->children) {
            m_byName.push_back(child.get());
            pending.push_back(child.get());
        }
        for (FileEntry &file : folder->files) { files.push_back(&file); }
    }
    const auto byName = [](const Entry *a, const Entry *b) {
        return a->name != b->name ? nameLess(a->name, b->name) : nameLess(a->path.generic_string(), b->path.generic_string());
    };
    std::sort(m_byName.begin(), m_byName.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    m_byName.insert(m_byName.end(), files.begin(), files.end());
}

void ContentBrowserDataSource::setSearch(const std::string &text) {
    m_search = text;
    m_found.clear();
    if (!m_search.empty()) {
        if (m_byName.empty()) { orderByName(); } // the first search of this tree
        const std::string query = folded(m_search);
        for (Entry *entry : m_byName) {
            if (entry->foldedName.find(query) != std::string::npos) { m_found.push_back(entry); }
        }
    }
    relist();
    if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->cancelPending(); }
}

void ContentBrowserDataSource::relist() {
    m_listing.clear();
    if (isSearching()) {
        m_listing = m_found;
        return;
    }
    if (!m_currentFolder) { return; }
    m_listing.reserve(static_cast<size_t>(childCount(*m_currentFolder)));
    for (int i = 0; i < childCount(*m_currentFolder); ++i) { m_listing.push_back(childAt(*m_currentFolder, i)); }
}

int ContentBrowserDataSource::itemCount() const {
    return static_cast<int>(m_listing.size());
}

ContentBrowserDataSource::Entry *ContentBrowserDataSource::entryAt(int index) const {
    return index >= 0 && index < itemCount() ? m_listing[static_cast<size_t>(index)] : nullptr;
}

ContentBrowserDataSource::Folder *ContentBrowserDataSource::folderAt(int index) const {
    Entry *entry = entryAt(index);
    return entry && entry->isFolder ? static_cast<Folder *>(entry) : nullptr;
}

const ContentBrowserDataSource::FileEntry *ContentBrowserDataSource::fileAt(int index) const {
    Entry *entry = entryAt(index);
    return entry && !entry->isFolder ? static_cast<const FileEntry *>(entry) : nullptr;
}

Panda::AssetId ContentBrowserDataSource::thumbnailAssetAt(int index) const {
    return thumbnailAssetOf(fileAt(index));
}

Panda::AssetId ContentBrowserDataSource::thumbnailAssetOf(const FileEntry *file) const {
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
    if (assetId == 0) { return -1; }
    // The cell of the main file when the grid shows it (a shader shows its pair as two cells).
    const FileEntry *main = findAsset(assetId);
    int first = -1;
    for (int i = 0; i < itemCount(); ++i) {
        const FileEntry *file = fileAt(i);
        if (!file || file->assetId != assetId) { continue; }
        if (file == main) { return i; }
        if (first < 0) { first = i; }
    }
    return first;
}

int ContentBrowserDataSource::indexOfFolder(const Panda::path_t &path) const {
    for (int i = 0; i < itemCount(); ++i) {
        const Folder *folder = folderAt(i);
        if (folder && folder->path == path) { return i; }
    }
    return -1;
}

PandaUI::TextureHandle ContentBrowserDataSource::thumbnailFor(int index) const {
    const FileEntry *file = fileAt(index);
    return file != nullptr ? thumbnailOf(*file) : PandaUI::TextureHandle();
}

PandaUI::TextureHandle ContentBrowserDataSource::thumbnailOf(const FileEntry &file) const {
    const Panda::AssetId assetId = thumbnailAssetOf(&file);
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

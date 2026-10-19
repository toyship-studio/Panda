#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Assets/Base/Path.hpp"

#include <Foundation/Foundation.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Panda {

// An asset kind as a contribution to the engine registry (context/plugins.md): name and numeric
// id, folder and extensions; for plugin file assets — load, placeholder and write. For built-in
// kinds with a cook (textures, shaders, materials, meshes) the loaders live in the handlers; a
// plugin kind is a file of a single format for the editor and the runtime, export copies it as is.
struct AssetKind final {
    std::string name;                    // "material": kind string in the editor (asset picker)
    AssetType type;                      // numeric id (persisted)
    std::string displayName;             // "Material"
    std::string folder;                  // "Materials": default folder in the project and export
    std::vector<std::string> extensions; // ".mat"; kinds may share an extension — see detect
    // Kinds with a shared extension: whether the file is of this kind (by header).
    // null — the extension is enough.
    bool (*detect)(const path_t &absolutePath) = nullptr;
    // File asset: read (null — the file is unreadable), a placeholder instead of an unreadable
    // one, write the document to a file. null for built-in kinds with a cook.
    Asset *(*load)(Foundation::AllocatorI *allocator, const path_t &absolutePath) = nullptr;
    Asset *(*placeholder)(Foundation::AllocatorI *allocator) = nullptr;
    bool (*save)(Asset &asset, const path_t &absolutePath) = nullptr;

    bool fileBased() const {
        return load != nullptr;
    }
    // Plugin kind id: a stable hash of the name (FNV-1a) outside the range of built-in ones.
    static AssetType idOf(std::string_view name);
};

class AssetKindRegistry final {
public:
    void add(AssetKind kind);
    const std::vector<AssetKind> &kinds() const {
        return m_kinds;
    }
    const AssetKind *find(AssetType type) const;
    const AssetKind *find(std::string_view name) const;
    bool hasExtension(std::string_view extension) const;
    // Kind of a file: by extension; among kinds sharing it — the first one confirmed by detect.
    const AssetKind *kindOfFile(const path_t &absolutePath) const;

private:
    std::vector<AssetKind> m_kinds;
};

} // namespace Panda

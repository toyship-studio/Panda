#include "Panda/Extensions/AssetKinds.hpp"

namespace Panda {

AssetType AssetKind::idOf(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const char symbol : name) {
        hash ^= static_cast<uint8_t>(symbol);
        hash *= 16777619u;
    }
    if (hash < 256u) { hash += 256u; } // built-in ids are small numbers
    return AssetType{hash};
}

void AssetKindRegistry::add(AssetKind kind) {
    PND_ASSERT_F(find(kind.name) == nullptr, "ASSET KIND %s REGISTERED TWICE", kind.name.c_str());
    PND_ASSERT_F(find(kind.type) == nullptr, "ASSET KIND %s: TYPE ID REGISTERED TWICE", kind.name.c_str());
    PND_ASSERT_F(
        kind.load == nullptr || (kind.placeholder != nullptr && kind.save != nullptr),
        "ASSET KIND %s: FILE-BASED KIND NEEDS load, placeholder AND save", kind.name.c_str()
    );
    m_kinds.push_back(std::move(kind));
}

const AssetKind *AssetKindRegistry::find(AssetType type) const {
    for (const AssetKind &kind : m_kinds) {
        if (kind.type == type) { return &kind; }
    }
    return nullptr;
}

const AssetKind *AssetKindRegistry::find(std::string_view name) const {
    for (const AssetKind &kind : m_kinds) {
        if (kind.name == name) { return &kind; }
    }
    return nullptr;
}

bool AssetKindRegistry::hasExtension(std::string_view extension) const {
    for (const AssetKind &kind : m_kinds) {
        for (const std::string &known : kind.extensions) {
            if (known == extension) { return true; }
        }
    }
    return false;
}

const AssetKind *AssetKindRegistry::kindOfFile(const path_t &absolutePath) const {
    const std::string extension = absolutePath.extension().string();
    const AssetKind *byExtension = nullptr;
    for (const AssetKind &kind : m_kinds) {
        bool matches = false;
        for (const std::string &known : kind.extensions) {
            if (known == extension) { matches = true; }
        }
        if (!matches) { continue; }
        if (kind.detect == nullptr) { return &kind; }
        if (kind.detect(absolutePath)) { return &kind; }
        if (byExtension == nullptr) { byExtension = &kind; } // no kind confirmed the file: the first by extension
    }
    return byExtension;
}

} // namespace Panda

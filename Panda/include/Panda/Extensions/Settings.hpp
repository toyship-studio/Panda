#pragma once

#include "Panda/Base/TypeContext.hpp"
#include "Panda/Serialization/ForeignValue.hpp"

#include <Rain/Rain.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {

// Where a settings section lives (context/plugins.md): the world (`.world`, next to the entities;
// the data in World::context), the project (`.Panda/project.json`) or the editor (the user's
// `settings.json`). A scope is one JSON object of sections by name; the owner of the file keeps
// the sections of the scope in a TypeContext and reads and writes them with SettingsMapper.
enum class SettingsScope : uint8_t { World, Project, Editor };

// A settings section of the core or of a contribution: a value under its own key of the scope's
// file. A key without a type (the module is not loaded) survives load and save as is
// (ForeignSettings).
struct SettingsType final {
    SettingsScope scope = SettingsScope::World;
    std::string name;                                                           // key in the file
    void (*encode)(const char *key, Rain::Encoder &, TypeContext &) = nullptr; // value under key
    void (*decode)(const char *key, Rain::Decoder &, TypeContext &) = nullptr; // key present → apply
    void (*copy)(TypeContext &from, TypeContext &to) = nullptr;                // world copy (Play)
    void (*reset)(TypeContext &) = nullptr;                                    // to defaults (loading starts from them)
};

// A Rain-Codable value in the store — registration in one line.
template<typename T>
SettingsType settingsType(SettingsScope scope, const char *name) {
    SettingsType type;
    type.scope = scope;
    type.name = name;
    type.encode = [](const char *key, Rain::Encoder &encoder, TypeContext &store) { encoder.encode(key, store.get<T>()); };
    type.decode = [](const char *key, Rain::Decoder &decoder, TypeContext &store) { decoder.decode(key, store.get<T>()); };
    type.copy = [](TypeContext &from, TypeContext &to) { to.get<T>() = from.get<T>(); };
    type.reset = [](TypeContext &store) { store.get<T>() = T{}; };
    return type;
}

// Sections of a file whose type is not registered (the module is not loaded): kept in the store
// of the scope and written back as is.
struct ForeignSettings final {
    std::vector<ForeignValue> items;
};

class SettingsRegistry final {
public:
    void add(SettingsType type);
    const std::vector<SettingsType> &types() const {
        return m_types;
    }
    const SettingsType *find(SettingsScope scope, std::string_view name) const;

private:
    std::vector<SettingsType> m_types;
};

} // namespace Panda

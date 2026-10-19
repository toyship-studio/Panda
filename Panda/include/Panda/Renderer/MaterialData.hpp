#pragma once

#include "Panda/Serialization/AssetsDtos.hpp"

namespace Panda {

struct MaterialField {
    std::string name;
    MaterialFieldType type;
    MaterialFieldValue value;
    // The slot is fed by a system every frame (system maps, impostor atlases, frame
    // parameters): the value lives only in memory — it is not written to .mat and is not
    // shown in the inspector. Orthogonal to the type: a RUNTIME texture stays TEXTURE, a
    // vector — VEC4.
    bool runtime = false;

    operator bool() const {
        return !name.empty();
    }
};

struct MaterialData {
    std::vector<MaterialField> inputs;

    void addField(const MaterialField &field);
    MaterialField &getField(const std::string &name);
    std::vector<MaterialField>::iterator findField(const std::string &name);
    void removeField(const MaterialField &field);
    bool hasField(const std::string &name);
};

} // namespace Panda
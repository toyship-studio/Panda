#pragma once

#include "Panda/Renderer/MaterialData.hpp"

#include <cstdint>

namespace Panda {

// System values of material fields: a field with such a name in ANY material is set by the
// engine (what the world sets for every material — today its wind).
// The field stays visible in the material but is inactive in the editor, with a hint
// explaining where the value is controlled from. bindFields substitutes the system value when
// assembling the fields UBO (resolve is the single point for the renderer and the editor);
// version() grows only on a real value change — material caches are rebuilt lazily.
// UBO types (float/uint/Vec4) are supported; a system value whose type does not match the
// field type is ignored — the material's value applies.
class MaterialSystemValues final {
public:
    // hint — an explanation for the editor ("Set by the wind of the world"), component — the
    // component of the world that controls the value, its name in .world ("windComponent"; the
    // editor selects the main one from the hint); both are remembered from the last set that gives
    // them, the value can be updated without them every frame.
    static void
    set(const char *name, MaterialFieldValue value, const char *hint = nullptr, const char *component = nullptr);
    // Field value taking the system override into account: the same-named system value
    // of a matching type — otherwise the material's value.
    static const MaterialFieldValue &resolve(const MaterialField &field);
    // Whether the field is a system one (the value is set by the engine): for the editor —
    // an inactive widget + hint.
    static bool isSystem(const MaterialField &field);
    static const char *hint(const std::string &name);      // "" — the field is not a system one
    static const char *component(const std::string &name); // "" — no component of the world controls it
    static const MaterialFieldValue *find(const std::string &name);
    static uint32_t version();
};

} // namespace Panda

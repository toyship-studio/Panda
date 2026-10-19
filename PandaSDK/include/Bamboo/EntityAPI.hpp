#pragma once

#include "Bamboo/Base.hpp"
#include "Bamboo/Allocator.hpp"
#include "ScriptRegistry/ScriptRegistry.hpp"

#include <type_traits>
#include <string>

namespace Bamboo {

class Script;

// The core components a script adds by this enumeration; the components of the modules (the
// bodies and colliders of the Physics module, the terrains) — by their registry name, the key of
// the component in .world ("rigidbody2dComponent", "boxCollider3dComponent", "terrain2dComponent").
enum class ComponentType {
    MESH_COMPONENT,
    SPRITE_RENDERER_COMPONENT,
    TRANSFORM_COMPONENT,
    DIRECTIONAL_LIGHT_COMPONENT
};

namespace EntityAPI {
    template<typename T>
        requires std::is_base_of_v<Script, T>
    Shared<T> getScript(EntityHandle handle) {
        auto entries = getScriptRegistry()->getEntityScripts(handle);
        for (auto [id, entry] : entries.instances) {
            Shared<T> script = SharedCast<T>(entry.script);
            if (script) { return script; }
        }
        return {};
    }

    void addComponent(EntityHandle handle, ComponentType type);
    // A component by its registry name (an unknown name is reported by the engine and ignored).
    void addComponent(EntityHandle handle, const char *componentName);

    const char *getName(EntityHandle handle);
}; // namespace EntityAPI

} // namespace Bamboo

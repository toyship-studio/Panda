#include "Bamboo/EntityAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace EntityAPI {
    // The registry names of the core components (the keys of .world).
    const char *getComponentName(ComponentType type) {
        switch (type) {
            case ComponentType::MESH_COMPONENT:
                return "runtimeMeshComponent";
            case ComponentType::SPRITE_RENDERER_COMPONENT:
                return "spriteRendererComponent";
            case ComponentType::TRANSFORM_COMPONENT:
                return "transformComponent";
            case ComponentType::DIRECTIONAL_LIGHT_COMPONENT:
                return "directionalLightComponent";
        }
        return "unknownComponent";
    }

    const char *getName(EntityHandle entity) {
        if (!Internal::requireMainThreadApi("EntityAPI::getName")) { return nullptr; }
        return Panda::ExternalCalls::entity_GetName(entity.id);
    }

    void addComponent(EntityHandle handle, ComponentType type) {
        addComponent(handle, getComponentName(type));
    }

    void addComponent(EntityHandle handle, const char *componentName) {
        if (!Internal::requireMainThreadApi("EntityAPI::addComponent")) { return; }
        if (componentName == nullptr) { return; }
        Panda::ExternalCalls::entity_CreateComponent(handle.id, componentName);
    }

} // namespace EntityAPI

} // namespace Bamboo

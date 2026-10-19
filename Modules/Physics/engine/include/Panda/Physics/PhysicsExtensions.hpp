#pragma once

namespace Panda {

class Extensions;
class ComponentTypeRegistry;
class WorldSystemRegistry;
class ScriptHostApiRegistry;

namespace Physics {

    // Module passport from module.json (via the build).
    extern const char *const MODULE_ID;
    extern const char *const MODULE_VERSION;

    // Contributions of the physics to the engine registries: the component types (bodies,
    // colliders, joints), the "physics" and "physics.step" world systems (the 2D and 3D worlds in
    // World::context, their life cycle and the fixed step) and the "physics" script table.
    void registerExtensions(Extensions &extensions);

    void registerComponentTypes(ComponentTypeRegistry &registry);
    void registerSystems(WorldSystemRegistry &registry);
    void registerScriptApi(ScriptHostApiRegistry &registry);

} // namespace Physics

} // namespace Panda

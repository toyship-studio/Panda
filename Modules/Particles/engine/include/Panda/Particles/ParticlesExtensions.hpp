#pragma once

namespace Panda {

class Extensions;

namespace Particles {

    // Module passport from module.json (via the build).
    extern const char *const MODULE_ID;
    extern const char *const MODULE_VERSION;

    // Contributions of the particles to the engine registries: the particleEmitterComponent
    // component type, a world system (simulation + instanced submit in the scene pass) and static
    // resources (shader, material, quad, instance layout).
    void registerExtensions(Extensions &extensions);

} // namespace Particles

} // namespace Panda

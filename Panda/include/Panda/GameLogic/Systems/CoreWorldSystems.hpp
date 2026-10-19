#pragma once

namespace Panda {

class WorldSystemRegistry;

// The core's own contribution to the frame phases, registered through the same registry as the
// modules (context/plugins.md): "scripts". World::update only runs the phases; the physics is a
// module ("physics" and "physics.step").
void registerCoreWorldSystems(WorldSystemRegistry &registry);

} // namespace Panda

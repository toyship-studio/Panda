#include "Panda/GameLogic/Systems/CoreWorldSystems.hpp"
#include "Panda/Extensions/WorldSystems.hpp"
#include "Panda/GameLogic/Systems/ScriptSystem.hpp"
#include "Panda/GameLogic/World.hpp"

namespace Panda {

namespace {

    void scriptsFixed(World &world, double stepTime) {
        ScriptSystem::fixedUpdate(world, stepTime);
    }

    void scriptsUpdate(World &world, double deltaTime) {
        ScriptSystem::update(world, deltaTime);
    }

    void scriptsLateUpdate(World &world, double deltaTime) {
        ScriptSystem::lateUpdate(world, deltaTime);
    }

} // namespace

void registerCoreWorldSystems(WorldSystemRegistry &registry) {
    // Scripts run after everything else in every phase, except the physics step that consumes
    // their forces: fixedUpdate between the physics "begin" (the poses of the step start) and
    // "step" systems of the physics module, update after the modules. The names are the
    // module's; without the module the constraints name nobody.
    WorldSystem scripts;
    scripts.name = "scripts";
    scripts.fixed = scriptsFixed;
    scripts.update = scriptsUpdate;
    scripts.postUpdate = scriptsLateUpdate;
    scripts.after = {"*", "physics"};
    scripts.before = {"physics.step"};
    registry.add(std::move(scripts));
}

} // namespace Panda

#pragma once

namespace Panda {

class World;

// World scripts — functions over the ScriptListComponent view: reconciling classes and fields with
// the manifest of the loaded DLL, instances at Play start and their teardown, the
// fixedUpdate/update/lateUpdate traversals in class order (ScriptClassManifest::order) with a
// budget for applying the scripts' background tasks.
namespace ScriptSystem {

    // Classes and fields of the containers — by the manifest: unknown classes are logged, fields
    // missing from the manifest are removed, new ones get the default, those whose type changed
    // are reset.
    void syncWithManifest(World &world);
    // Play start: script instances, id binding and passing field values to the DLL.
    void instantiate(World &world);
    // Play stop: background tasks are stopped before instance teardown (workers hold references).
    void shutdown(World &world);
    void start(World &world);
    // Before every physics step, with the step length: the transforms hold the exact body poses.
    void fixedUpdate(World &world, double stepTime);
    // Frame: finished background tasks are applied before update (scripts see the result in the
    // same frame), then update and lateUpdate in class order.
    void update(World &world, double deltaTime);
    void lateUpdate(World &world, double deltaTime);

} // namespace ScriptSystem

} // namespace Panda

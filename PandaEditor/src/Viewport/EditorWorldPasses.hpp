#pragma once

#include <Panda/GameLogic/World.hpp>

#include <glm/glm.hpp>

namespace Panda {

// Editor passes on top of the world frame (after World::update of the same frame): selection
// wireframe and IDS picking. The renderers and the world traversal are the engine's, the
// selection — SelectionContext.
namespace EditorWorldPasses {

    // Wireframe of the selected entities in SELECTED_GEOMETRY_VIEW (GEOMETRY_ONLY mode).
    void renderSelectionOverlay(World &world, const glm::mat4 &viewProj);
    // IDS pass for mouse picking: a full world traversal into IDS_VIEW. Runs every frame — a
    // candidate for "on demand" (on a click/gizmo drag); the scope is visible in the Profiler panel.
    void renderPicking(World &world, const WorldFrame &frame);

} // namespace EditorWorldPasses

} // namespace Panda

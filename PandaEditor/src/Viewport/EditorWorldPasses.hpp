#pragma once

#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/WorldView.hpp>

#include <glm/glm.hpp>

namespace Panda {

// Editor passes on top of a frame of the world (after WorldView::render of the same frame): the
// mask of the selection and IDS picking. The renderers and the world traversal are the view's,
// the selection — Editor::selection(); the Miren views are the caller's (EditorPassTargets).
namespace EditorWorldPasses {

    // The selected entities into `selectionView` (GEOMETRY_ONLY mode): the mask the outline is
    // made from.
    void renderSelectionOverlay(WorldView &view, World &world, Miren::ViewId selectionView);
    // IDS pass for mouse picking: a full world traversal into `idsView`. Runs every frame — a
    // candidate for "on demand" (on a click/gizmo drag); the scope is visible in the Profiler panel.
    void renderPicking(WorldView &view, World &world, Miren::ViewId idsView);

} // namespace EditorWorldPasses

} // namespace Panda

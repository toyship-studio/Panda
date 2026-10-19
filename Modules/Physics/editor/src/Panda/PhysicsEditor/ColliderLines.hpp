#pragma once

#include <PandaEditorKit/Viewport/HelperLine.hpp>
#include <PandaUI/Core/Base.hpp>

#include <glm/glm.hpp>
#include <vector>

namespace Panda {

class World;

namespace PhysicsEditor {

    // Wireframe outlines of the colliders of the world entities (2D box; 3D box/sphere/capsule) —
    // the same scale/offset math as in Physics2D/3D → the outline matches the real shape of the
    // body. Only what the view shows: a collider outside the view frustum or a few pixels in size
    // gives no lines, a circle gets its segments by the size on screen. The lines are appended
    // collider by collider, from the nearest to the camera — the overlay may draw only the head
    // of the list. viewportFrame — the window rectangle of the scene surface; edges with a point
    // behind the camera are skipped.
    void appendColliderLines(
        World &world, std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
    );

} // namespace PhysicsEditor

} // namespace Panda

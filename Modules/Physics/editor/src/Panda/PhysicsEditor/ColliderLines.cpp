#include "Panda/PhysicsEditor/ColliderLines.hpp"

#include <Panda/GameLogic/Systems/RenderSystem.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Physics/PhysicsComponents.hpp>

#include <algorithm>

namespace Panda::PhysicsEditor {

namespace {

    // Detail by the size on screen: a collider whose bounding radius is below the minimum is not
    // outlined, and a circle gets fewer segments the smaller it looks.
    constexpr float MIN_OUTLINE_PIXELS = 4.f;
    constexpr int MIN_ARC_SEGMENTS = 6;
    constexpr int MAX_ARC_SEGMENTS = 24;

    int arcSegments(float radiusPixels) {
        return glm::clamp(static_cast<int>(radiusPixels * 0.8f), MIN_ARC_SEGMENTS, MAX_ARC_SEGMENTS);
    }

    // Edges of one collider in the common list and its distance from the camera.
    struct Outline {
        float depth = 0.f;
        size_t first = 0;
        size_t count = 0;
    };

} // namespace

void appendColliderLines(World &world, std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame) {

    const PandaUI::Color color(0.36f, 0.85f, 0.44f, 1.f); // classic green collider outline
    const RenderSystem::Frustum frustum = RenderSystem::Frustum::of(viewProj);
    // Pixels that a world unit takes on screen at the clip depth 1: the vertical scale of the
    // projection (row Y of the matrix) by half of the viewport height.
    const float pixelScale = 0.5f * viewportFrame.size.height *
                             glm::length(glm::vec3(viewProj[0][1], viewProj[1][1], viewProj[2][1]));

    std::vector<HelperLine> edges; // in the order of the registry; sorted by outlines at the end
    std::vector<Outline> outlines;

    // World → window; false — the point is behind the camera (an edge with such a point is skipped).
    auto project = [&](const glm::vec3 &point, PandaUI::Point &out) {
        const glm::vec4 clip = viewProj * glm::vec4(point, 1.f);
        if (clip.w <= 0.f) { return false; }
        out.x = viewportFrame.origin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewportFrame.size.width;
        out.y = viewportFrame.origin.y + (1.f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportFrame.size.height;
        return true;
    };
    auto addEdge = [&](const glm::vec3 &a, const glm::vec3 &b) {
        HelperLine line;
        line.color = color;
        if (project(a, line.a) && project(b, line.b)) { edges.push_back(line); }
    };
    // Opens the outline of a collider that fits into the sphere. false — the collider is outside
    // the view or too small on screen. pixelsPerUnit — the size of a world unit on screen at it.
    auto beginOutline = [&](const glm::vec3 &center, float radius, float &pixelsPerUnit) {
        const glm::vec3 extent(radius);
        if (!frustum.visible(glm::mat4(1.f), center - extent, center + extent)) { return false; }
        const float depth = (viewProj * glm::vec4(center, 1.f)).w;
        pixelsPerUnit = pixelScale / glm::max(depth, 0.01f);
        // Negated on purpose: a broken transform (NaN) fails the comparison and is skipped too.
        if (!(radius * pixelsPerUnit >= MIN_OUTLINE_PIXELS)) { return false; }
        outlines.push_back({depth, edges.size(), 0});
        return true;
    };
    auto endOutline = [&] { outlines.back().count = edges.size() - outlines.back().first; };

    // BoxCollider3D: an oriented box.
    world.getAllEntitiesWith<IdComponent, BoxCollider3DComponent>().each(
        [&](IdComponent &idc, BoxCollider3DComponent &collider) {
            Entity entity = world.getById(idc.id);
            if (!entity.isValid()) { return; }
            auto transform = entity.calculateWorldSpaceTransform();
            const glm::vec3 position = transform.getPosition();
            const glm::quat rotation = transform.getRotation();
            const glm::vec3 half = glm::abs(collider.size * transform.getScale());
            float pixelsPerUnit = 0.f;
            if (!beginOutline(position + rotation * collider.offset, glm::length(half), pixelsPerUnit)) { return; }
            glm::vec3 corners[8];
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 sign((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f);
                corners[i] = position + rotation * (collider.offset + sign * half);
            }
            static const int boxEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                                {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto &edge : boxEdges) {
                addEdge(corners[edge[0]], corners[edge[1]]);
            }
            endOutline();
        }
    );
    // Circle arc in the (axisA, axisB) plane around center, in segments (full circle: from=0, to=2π).
    auto addArc = [&](const glm::vec3 &center, const glm::vec3 &axisA, const glm::vec3 &axisB, float radius,
                      float from, float to, int segments) {
        glm::vec3 previous = center + radius * (cosf(from) * axisA + sinf(from) * axisB);
        for (int i = 1; i <= segments; ++i) {
            const float angle = from + (to - from) * (float)i / segments;
            const glm::vec3 point = center + radius * (cosf(angle) * axisA + sinf(angle) * axisB);
            addEdge(previous, point);
            previous = point;
        }
    };
    const float pi = 3.14159265358979f;
    const float twoPi = 2.f * pi;

    // SphereCollider3D: three great circles in the local planes of the entity.
    world.getAllEntitiesWith<IdComponent, SphereCollider3DComponent>().each(
        [&](IdComponent &idc, SphereCollider3DComponent &collider) {
            Entity entity = world.getById(idc.id);
            if (!entity.isValid()) { return; }
            auto transform = entity.calculateWorldSpaceTransform();
            const glm::quat rotation = transform.getRotation();
            const glm::vec3 scale = glm::abs(transform.getScale());
            const glm::vec3 center = transform.getPosition() + rotation * collider.offset;
            const float radius = glm::abs(collider.radius) * glm::max(scale.x, glm::max(scale.y, scale.z));
            float pixelsPerUnit = 0.f;
            if (!beginOutline(center, radius, pixelsPerUnit)) { return; }
            const int segments = arcSegments(radius * pixelsPerUnit);
            const glm::vec3 x = rotation * glm::vec3(1.f, 0.f, 0.f);
            const glm::vec3 y = rotation * glm::vec3(0.f, 1.f, 0.f);
            const glm::vec3 z = rotation * glm::vec3(0.f, 0.f, 1.f);
            addArc(center, x, y, radius, 0.f, twoPi, segments);
            addArc(center, x, z, radius, 0.f, twoPi, segments);
            addArc(center, y, z, radius, 0.f, twoPi, segments);
            endOutline();
        }
    );
    // CapsuleCollider3D: Y axis — two end circles, four side edges and hemisphere arcs.
    world.getAllEntitiesWith<IdComponent, CapsuleCollider3DComponent>().each(
        [&](IdComponent &idc, CapsuleCollider3DComponent &collider) {
            Entity entity = world.getById(idc.id);
            if (!entity.isValid()) { return; }
            auto transform = entity.calculateWorldSpaceTransform();
            const glm::quat rotation = transform.getRotation();
            const glm::vec3 scale = glm::abs(transform.getScale());
            const float radius = glm::abs(collider.radius) * glm::max(scale.x, scale.z);
            const float halfHeight = glm::abs(collider.halfHeight) * scale.y;
            const glm::vec3 center = transform.getPosition() + rotation * collider.offset;
            float pixelsPerUnit = 0.f;
            if (!beginOutline(center, halfHeight + radius, pixelsPerUnit)) { return; }
            const int segments = arcSegments(radius * pixelsPerUnit);
            const int halfSegments = glm::max(segments / 2, 3);
            const glm::vec3 x = rotation * glm::vec3(1.f, 0.f, 0.f);
            const glm::vec3 y = rotation * glm::vec3(0.f, 1.f, 0.f);
            const glm::vec3 z = rotation * glm::vec3(0.f, 0.f, 1.f);
            const glm::vec3 top = center + y * halfHeight;
            const glm::vec3 bottom = center - y * halfHeight;
            addArc(top, x, z, radius, 0.f, twoPi, segments);
            addArc(bottom, x, z, radius, 0.f, twoPi, segments);
            addEdge(top + x * radius, bottom + x * radius);
            addEdge(top - x * radius, bottom - x * radius);
            addEdge(top + z * radius, bottom + z * radius);
            addEdge(top - z * radius, bottom - z * radius);
            addArc(top, x, y, radius, 0.f, pi, halfSegments);    // top hemisphere
            addArc(top, z, y, radius, 0.f, pi, halfSegments);
            addArc(bottom, x, y, radius, pi, twoPi, halfSegments); // bottom hemisphere
            addArc(bottom, z, y, radius, pi, twoPi, halfSegments);
            endOutline();
        }
    );
    // BoxCollider2D: a rectangle in the XY plane of the entity (full world transform).
    world.getAllEntitiesWith<IdComponent, BoxCollider2DComponent>().each(
        [&](IdComponent &idc, BoxCollider2DComponent &collider) {
            Entity entity = world.getById(idc.id);
            if (!entity.isValid()) { return; }
            const glm::mat4 matrix = world.getWorldSpaceTransformMatrix(entity);
            const glm::vec2 signs[4] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
            glm::vec3 corners[4];
            for (int i = 0; i < 4; ++i) {
                const glm::vec2 local = collider.offset + signs[i] * collider.size;
                corners[i] = glm::vec3(matrix * glm::vec4(local, 0.f, 1.f));
            }
            const glm::vec3 center = (corners[0] + corners[2]) * 0.5f;
            float pixelsPerUnit = 0.f;
            if (!beginOutline(center, glm::distance(corners[0], center), pixelsPerUnit)) { return; }
            for (int i = 0; i < 4; ++i) {
                addEdge(corners[i], corners[(i + 1) % 4]);
            }
            endOutline();
        }
    );

    // Nearest colliders first: the overlay draws as many lines as its share of the UI draw list
    // holds and drops the rest. The sort is stable — the list does not change from frame to frame
    // while the camera and the world stand still (the overlay redraws only on a change).
    std::stable_sort(outlines.begin(), outlines.end(), [](const Outline &a, const Outline &b) {
        return a.depth < b.depth;
    });
    lines.reserve(lines.size() + edges.size());
    for (const Outline &outline : outlines) {
        const auto first = edges.begin() + static_cast<std::ptrdiff_t>(outline.first);
        lines.insert(lines.end(), first, first + static_cast<std::ptrdiff_t>(outline.count));
    }
}

} // namespace Panda::PhysicsEditor

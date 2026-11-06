#include "Panda/PhysicsEditor/ColliderLines.hpp"

#include <Panda/GameLogic/SelectionContext.hpp>
#include <Panda/GameLogic/Systems/RenderSystem.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Physics/MeshColliderGeometry.hpp>
#include <Panda/Physics/PhysicsComponents.hpp>

#include <algorithm>
#include <cfloat>
#include <map>
#include <unordered_set>

namespace Panda::PhysicsEditor {

namespace {

    // Detail by the size on screen: a collider whose bounding radius is below the minimum is not
    // outlined, and a circle gets fewer segments the smaller it looks.
    constexpr float MIN_OUTLINE_PIXELS = 4.f;
    constexpr int MIN_ARC_SEGMENTS = 6;
    constexpr int MAX_ARC_SEGMENTS = 24;
    // A mesh collider of a selected entity shows its edges; a mesh has thousands, the overlay has
    // a budget shared by every collider: beyond the cap every k-th edge is drawn, a sparser
    // wireframe of the same shape.
    constexpr size_t MAX_MESH_OUTLINE_EDGES = 3000;

    // The outline of a mesh collider source, built once from its triangles and reused every frame
    // (the geometry does not change under the editor; a runtime mesh a script rewrites is caught by
    // its size). The box is for every collider; the edges — unique edges of the triangles, or the
    // edges of the convex hull — for a selected one.
    struct MeshOutline {
        size_t positionCount = 0, indexCount = 0; // the signature of the geometry it was built from
        glm::vec3 boundsMin{0.f}, boundsMax{0.f};
        std::vector<glm::vec3> positions;                   // of the triangles
        std::vector<std::pair<uint32_t, uint32_t>> edges;   // unique, into positions
        std::vector<glm::vec3> hullPoints;                  // of the convex hull
        std::vector<std::pair<uint32_t, uint32_t>> hullEdges; // into hullPoints
        bool hullBuilt = false;
        bool valid = false; // the source has triangles
    };

    // The outline of a source; nullptr — the source has no triangles. The cache lives with the
    // editor: bounded by the distinct meshes the colliders of the open worlds use.
    const MeshOutline *meshOutlineOf(const Physics::MeshColliderSource &source, bool withHull) {
        static std::map<Physics::MeshColliderSource, MeshOutline> cache;
        size_t positionCount = 0, indexCount = 0;
        const bool hasGeometry = Physics::meshColliderGeometrySize(source, positionCount, indexCount);
        MeshOutline &outline = cache[source];
        const bool sameSize = outline.positionCount == positionCount && outline.indexCount == indexCount;
        const bool stale = hasGeometry ? (!outline.valid || !sameSize) : outline.valid;
        if (stale) {
            outline = MeshOutline{};
            MeshGeometry geometry;
            if (hasGeometry && Physics::meshColliderGeometry(source, geometry)) {
                outline.valid = true;
                outline.positionCount = geometry.positions.size();
                outline.indexCount = geometry.indices.size();
                outline.boundsMin = glm::vec3(FLT_MAX);
                outline.boundsMax = glm::vec3(-FLT_MAX);
                for (const glm::vec3 &position : geometry.positions) {
                    outline.boundsMin = glm::min(outline.boundsMin, position);
                    outline.boundsMax = glm::max(outline.boundsMax, position);
                }
                // Unique edges: the smaller index first, one entry per pair.
                std::unordered_set<uint64_t> seen;
                const size_t triangles = geometry.indices.size() / 3;
                for (size_t t = 0; t < triangles; t++) {
                    for (int e = 0; e < 3; e++) {
                        uint32_t a = geometry.indices[t * 3 + e];
                        uint32_t b = geometry.indices[t * 3 + (e + 1) % 3];
                        if (a == b || a >= geometry.positions.size() || b >= geometry.positions.size()) { continue; }
                        if (a > b) { std::swap(a, b); }
                        if (seen.insert((static_cast<uint64_t>(a) << 32) | b).second) {
                            outline.edges.emplace_back(a, b);
                        }
                    }
                }
                outline.positions = std::move(geometry.positions);
            }
        }
        if (!outline.valid) { return nullptr; }
        if (withHull && !outline.hullBuilt) {
            outline.hullBuilt = true;
            MeshGeometry points;
            points.positions = outline.positions;
            points.indices = {0, 1, 2}; // the hull needs the points alone
            Physics::meshColliderHullOutline(points, outline.hullPoints, outline.hullEdges);
        }
        return &outline;
    }

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
    // MeshCollider3D: the box of its triangles; for a selected entity the triangles' edges (the
    // hull's for a convex one), through the full world transform like the renderer.
    world.getAllEntitiesWith<IdComponent, MeshCollider3DComponent>().each(
        [&](IdComponent &idc, MeshCollider3DComponent &collider) {
            Entity entity = world.getById(idc.id);
            if (!entity.isValid()) { return; }
            Physics::MeshColliderSource source;
            if (!Physics::meshColliderSource(entity, collider, source)) { return; }
            const bool selected = SelectionContext::isEntitySelected(idc.id);
            const MeshOutline *outline = meshOutlineOf(source, selected && collider.convex);
            if (outline == nullptr) { return; }
            const glm::mat4 matrix = world.getWorldSpaceTransformMatrix(entity);
            glm::vec3 corners[8];
            glm::vec3 center(0.f);
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 local(
                    (i & 1) ? outline->boundsMax.x : outline->boundsMin.x,
                    (i & 2) ? outline->boundsMax.y : outline->boundsMin.y,
                    (i & 4) ? outline->boundsMax.z : outline->boundsMin.z
                );
                corners[i] = glm::vec3(matrix * glm::vec4(local, 1.f));
                center += corners[i] * 0.125f;
            }
            float radius = 0.f;
            for (const glm::vec3 &corner : corners) {
                radius = glm::max(radius, glm::distance(corner, center));
            }
            float pixelsPerUnit = 0.f;
            if (!beginOutline(center, radius, pixelsPerUnit)) { return; }
            const std::vector<glm::vec3> &points = collider.convex ? outline->hullPoints : outline->positions;
            const auto &meshEdges = collider.convex ? outline->hullEdges : outline->edges;
            if (!selected || meshEdges.empty()) {
                static const int boxEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                                    {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto &edge : boxEdges) {
                    addEdge(corners[edge[0]], corners[edge[1]]);
                }
            } else {
                const size_t stride =
                    glm::max<size_t>(1, (meshEdges.size() + MAX_MESH_OUTLINE_EDGES - 1) / MAX_MESH_OUTLINE_EDGES);
                for (size_t i = 0; i < meshEdges.size(); i += stride) {
                    const auto &[a, b] = meshEdges[i];
                    const glm::vec3 from(matrix * glm::vec4(points[a], 1.f));
                    const glm::vec3 to(matrix * glm::vec4(points[b], 1.f));
                    addEdge(from, to);
                }
            }
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

void appendJointLines(World &world, std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame) {
    const PandaUI::Color color(1.f, 0.62f, 0.2f, 1.f); // orange: the joints, apart from the green colliders
    const PandaUI::Color linkColor(1.f, 0.62f, 0.2f, 0.55f);
    const RenderSystem::Frustum frustum = RenderSystem::Frustum::of(viewProj);
    const float pixelScale = 0.5f * viewportFrame.size.height *
                             glm::length(glm::vec3(viewProj[0][1], viewProj[1][1], viewProj[2][1]));
    std::vector<HelperLine> edges;
    std::vector<Outline> outlines;
    auto project = [&](const glm::vec3 &point, PandaUI::Point &out) {
        const glm::vec4 clip = viewProj * glm::vec4(point, 1.f);
        if (clip.w <= 0.f) { return false; }
        out.x = viewportFrame.origin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewportFrame.size.width;
        out.y = viewportFrame.origin.y + (1.f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportFrame.size.height;
        return true;
    };
    auto addEdge = [&](const glm::vec3 &a, const glm::vec3 &b, const PandaUI::Color &edgeColor) {
        HelperLine line;
        line.color = edgeColor;
        if (project(a, line.a) && project(b, line.b)) { edges.push_back(line); }
    };
    // Opens the drawing of a joint around its anchor; false — out of the view. worldPerPixel — the
    // size of a pixel in world units at the anchor, for the marks to keep their size on screen.
    auto beginJoint = [&](const glm::vec3 &anchor, float &worldPerPixel) {
        const glm::vec3 extent(0.5f);
        if (!frustum.visible(glm::mat4(1.f), anchor - extent, anchor + extent)) { return false; }
        const float depth = (viewProj * glm::vec4(anchor, 1.f)).w;
        worldPerPixel = glm::max(depth, 0.01f) / glm::max(pixelScale, 1e-3f);
        outlines.push_back({depth, edges.size(), 0});
        return true;
    };
    auto endJoint = [&] { outlines.back().count = edges.size() - outlines.back().first; };
    auto cross = [&](const glm::vec3 &point, float half) {
        addEdge(point - glm::vec3(half, 0.f, 0.f), point + glm::vec3(half, 0.f, 0.f), color);
        addEdge(point - glm::vec3(0.f, half, 0.f), point + glm::vec3(0.f, half, 0.f), color);
        addEdge(point - glm::vec3(0.f, 0.f, half), point + glm::vec3(0.f, 0.f, half), color);
    };
    auto axisLine = [&](const glm::vec3 &point, const glm::vec3 &axis, float half) {
        if (glm::dot(axis, axis) < 1e-8f) { return; }
        const glm::vec3 direction = glm::normalize(axis);
        addEdge(point - direction * half, point + direction * half, color);
    };
    // The anchor of a joint in world space: the authored local anchor, scaled, through the entity.
    auto worldAnchor = [](const TransformComponent &transform, const glm::vec3 &anchor) {
        return transform.getPosition() + transform.getRotation() * (anchor * glm::abs(transform.getScale()));
    };
    auto connectedPosition = [&](UUID connectedId, glm::vec3 &out) {
        Entity connected = world.getById(connectedId);
        if (!connected.isValid()) { return false; }
        out = connected.calculateWorldSpaceTransform().getPosition();
        return true;
    };
    constexpr float CROSS_PIXELS = 6.f;
    constexpr float AXIS_PIXELS = 28.f;

    // A joint with an anchor and an axis: a cross, the axis through it, a line to the connected body.
    auto anchorAxisJoint = [&](Entity entity, const glm::vec3 &anchor, const glm::vec3 &axis, bool withAxis, UUID connectedId) {
        const TransformComponent transform = entity.calculateWorldSpaceTransform();
        const glm::vec3 point = worldAnchor(transform, anchor);
        float worldPerPixel = 0.f;
        if (!beginJoint(point, worldPerPixel)) { return; }
        cross(point, CROSS_PIXELS * worldPerPixel);
        if (withAxis) { axisLine(point, transform.getRotation() * axis, AXIS_PIXELS * worldPerPixel); }
        glm::vec3 connected;
        if (connectedPosition(connectedId, connected)) { addEdge(point, connected, linkColor); }
        endJoint();
    };
    world.getAllEntitiesWith<IdComponent, RevoluteJoint3DComponent>().each([&](IdComponent &idc,
                                                                        RevoluteJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { anchorAxisJoint(entity, joint.anchor, joint.axis, true, joint.connectedEntity); }
    });
    world.getAllEntitiesWith<IdComponent, SphericalJoint3DComponent>().each([&](IdComponent &idc,
                                                                        SphericalJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { anchorAxisJoint(entity, joint.anchor, joint.axis, true, joint.connectedEntity); }
    });
    world.getAllEntitiesWith<IdComponent, PrismaticJoint3DComponent>().each([&](IdComponent &idc,
                                                                        PrismaticJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { anchorAxisJoint(entity, joint.anchor, joint.axis, true, joint.connectedEntity); }
    });
    world.getAllEntitiesWith<IdComponent, WeldJoint3DComponent>().each([&](IdComponent &idc,
                                                                        WeldJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { anchorAxisJoint(entity, joint.anchor, glm::vec3(0.f), false, joint.connectedEntity); }
    });
    world.getAllEntitiesWith<IdComponent, WheelJoint3DComponent>().each([&](IdComponent &idc,
                                                                        WheelJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (!entity.isValid()) { return; }
        const TransformComponent transform = entity.calculateWorldSpaceTransform();
        const glm::vec3 point = worldAnchor(transform, joint.anchor);
        float worldPerPixel = 0.f;
        if (!beginJoint(point, worldPerPixel)) { return; }
        cross(point, CROSS_PIXELS * worldPerPixel);
        axisLine(point, transform.getRotation() * joint.axis, AXIS_PIXELS * worldPerPixel);              // spin
        axisLine(point, transform.getRotation() * joint.suspensionAxis, AXIS_PIXELS * 0.5f * worldPerPixel); // travel
        glm::vec3 chassis;
        if (connectedPosition(joint.connectedEntity, chassis)) { addEdge(point, chassis, linkColor); }
        endJoint();
    });
    // A distance joint: the two anchors and the segment between them.
    world.getAllEntitiesWith<IdComponent, DistanceJoint3DComponent>().each([&](IdComponent &idc,
                                                                        DistanceJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (!entity.isValid()) { return; }
        const glm::vec3 pointA = worldAnchor(entity.calculateWorldSpaceTransform(), joint.anchor);
        float worldPerPixel = 0.f;
        if (!beginJoint(pointA, worldPerPixel)) { return; }
        cross(pointA, CROSS_PIXELS * worldPerPixel);
        Entity connected = world.getById(joint.connectedEntity);
        if (connected.isValid()) {
            const glm::vec3 pointB = worldAnchor(connected.calculateWorldSpaceTransform(), joint.connectedAnchor);
            cross(pointB, CROSS_PIXELS * worldPerPixel);
            addEdge(pointA, pointB, color);
        }
        endJoint();
    });
    // A filter and a motor have no geometry: a line between the two entities.
    auto linkJoint = [&](Entity entity, UUID connectedId) {
        const glm::vec3 point = entity.calculateWorldSpaceTransform().getPosition();
        float worldPerPixel = 0.f;
        if (!beginJoint(point, worldPerPixel)) { return; }
        cross(point, CROSS_PIXELS * worldPerPixel);
        glm::vec3 connected;
        if (connectedPosition(connectedId, connected)) { addEdge(point, connected, linkColor); }
        endJoint();
    };
    world.getAllEntitiesWith<IdComponent, FilterJoint3DComponent>().each([&](IdComponent &idc,
                                                                        FilterJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { linkJoint(entity, joint.connectedEntity); }
    });
    world.getAllEntitiesWith<IdComponent, MotorJoint3DComponent>().each([&](IdComponent &idc,
                                                                        MotorJoint3DComponent &joint) {
        Entity entity = world.getById(idc.id);
        if (entity.isValid()) { linkJoint(entity, joint.connectedEntity); }
    });

    std::stable_sort(outlines.begin(), outlines.end(), [](const Outline &a, const Outline &b) { return a.depth < b.depth; });
    lines.reserve(lines.size() + edges.size());
    for (const Outline &outline : outlines) {
        const auto first = edges.begin() + static_cast<std::ptrdiff_t>(outline.first);
        lines.insert(lines.end(), first, first + static_cast<std::ptrdiff_t>(outline.count));
    }
}

} // namespace Panda::PhysicsEditor

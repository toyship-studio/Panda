#include "Panda/Renderer/CascadedShadowMap.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Assets/Base/AssetHandler.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Panda {

namespace {

    // Splitting into slices: 0 — uniform, 1 — logarithmic (practical split scheme).
    constexpr float SPLIT_LAMBDA = 0.6f;
    // The atlas width does not exceed the maximum texture size of the backends (Metal/GL/D3D11 —
    // 16384).
    constexpr uint32_t MAX_ATLAS_WIDTH = 16384;

    // Light NDC → (u, v, depth) of the tile: v is flipped on D3D/Metal (row 0 is at the top), the
    // depth on GL is written to the buffer as z*0.5+0.5. The light projection is ZO on all backends
    // (on GL [0..1] is a valid subregion of the clip volume), so depth = z*0.5+0.5 only by the flag.
    glm::mat4 textureBiasMatrix() {
        const bool bottomLeft = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
        const bool homogeneousDepth = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth;
        glm::mat4 bias(1.f);
        bias[0][0] = 0.5f;
        bias[3][0] = 0.5f;
        bias[1][1] = bottomLeft ? 0.5f : -0.5f;
        bias[3][1] = 0.5f;
        bias[2][2] = homogeneousDepth ? 0.5f : 1.f;
        bias[3][2] = homogeneousDepth ? 0.5f : 0.f;
        return bias;
    }

    // The camera pyramid from viewProj (without knowing near/far/fov): the corner rays and the
    // axis; the cross-section at depth d is the points of the rays at this depth (the depths are
    // signed along the axis).
    struct CameraRays {
        glm::vec3 axis{0.f};
        glm::vec3 origins[4];
        glm::vec3 directions[4]; // unit length
        float originDepths[4]{}; // depth of the ray origin along the axis
        float farDepth = 0.f;

        glm::vec3 cornerAt(int c, float depth) const {
            const float along = (depth - originDepths[c]) / std::max(glm::dot(directions[c], axis), 1e-4f);
            return origins[c] + directions[c] * along;
        }
    };

    // The far plane of a perspective (far 20000 at near 0.1) cannot be recovered from the inverse
    // matrix in float32: the inversion is ill-conditioned, the far corners drift by percents from
    // frame to frame, the radius of the cascade sphere follows them, and the shadow edges jitter
    // as the camera moves. So the perspective is built from the SIDE planes (rows 0, 1, 3 of the
    // matrix are well-conditioned): the directions of the corner rays = the intersections of the
    // planes, the apex is the camera, the axis is the w row; the far plane is used only as the
    // upper limit of the distance. Ortho (w does not depend on the point): the inverse matrix is
    // conditioned normally, the corners come from it, the rays are parallel to the axis.
    bool
    extractCameraRays(const glm::mat4 &viewProj, const glm::vec3 &cameraPosition, float fallbackFar, CameraRays &rays) {
        const auto row = [&viewProj](int i) {
            return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]);
        };
        const glm::vec4 rowX = row(0);
        const glm::vec4 rowY = row(1);
        const glm::vec4 rowW = row(3);
        const bool perspective = glm::length(glm::vec3(rowW)) > 1e-6f;
        if (perspective) {
            rays.axis = glm::normalize(glm::vec3(rowW));
            const glm::vec3 planeLeft = glm::vec3(rowW + rowX);
            const glm::vec3 planeRight = glm::vec3(rowW - rowX);
            const glm::vec3 planeBottom = glm::vec3(rowW + rowY);
            const glm::vec3 planeTop = glm::vec3(rowW - rowY);
            const glm::vec3 sidePlanes[4][2] = {
                {planeLeft, planeBottom}, {planeRight, planeBottom}, {planeRight, planeTop}, {planeLeft, planeTop}
            };
            for (int c = 0; c < 4; c++) {
                glm::vec3 direction = glm::normalize(glm::cross(sidePlanes[c][0], sidePlanes[c][1]));
                if (glm::dot(direction, rays.axis) < 0.f) { direction = -direction; }
                rays.origins[c] = cameraPosition;
                rays.directions[c] = direction;
                rays.originDepths[c] = 0.f;
            }
            const glm::vec4 farPlane = rowW - row(2);
            const float farDenominator = glm::dot(glm::vec3(farPlane), rays.axis);
            rays.farDepth = std::abs(farDenominator) > 1e-12f
                                ? -(glm::dot(glm::vec3(farPlane), cameraPosition) + farPlane.w) / farDenominator
                                : fallbackFar;
        } else {
            const glm::mat4 invViewProj = glm::inverse(viewProj);
            const auto unproject = [&invViewProj](float x, float y, float z) {
                const glm::vec4 v = invViewProj * glm::vec4(x, y, z, 1.f);
                return glm::vec3(v) / v.w;
            };
            const float cornerX[4] = {-1.f, 1.f, 1.f, -1.f};
            const float cornerY[4] = {-1.f, -1.f, 1.f, 1.f};
            glm::vec3 nearCenter(0.f);
            glm::vec3 farCenter(0.f);
            for (int c = 0; c < 4; c++) {
                rays.origins[c] = unproject(cornerX[c], cornerY[c], -1.f);
                nearCenter += rays.origins[c] * 0.25f;
                farCenter += unproject(cornerX[c], cornerY[c], 1.f) * 0.25f;
            }
            if (glm::distance(farCenter, nearCenter) < 0.01f) { return false; }
            rays.axis = glm::normalize(farCenter - nearCenter);
            for (int c = 0; c < 4; c++) {
                rays.directions[c] = rays.axis;
                rays.originDepths[c] = glm::dot(rays.origins[c] - cameraPosition, rays.axis);
            }
            rays.farDepth = glm::dot(farCenter - cameraPosition, rays.axis);
        }
        return rays.farDepth > 0.01f;
    }

    // The planes around a convex body swept toward the light, inside positive, pushed out by
    // `margin`. The body is a slice of the camera pyramid by its eight corners: the ring at the
    // near depth, then the ring at the far one (the near ring of a perspective from the camera is
    // one point). The sweep is bounded by the faces of the body turned away from the light and by
    // its outline as the light sees it — the convex hull of the corners on the plane across the
    // light, `right` and `above` being the axes of that plane.
    void sweptSlicePlanes(
        const glm::vec3 corners[8], const glm::vec3 &toLight, const glm::vec3 &right, const glm::vec3 &above,
        float margin, std::vector<glm::vec4> &planes
    ) {
        static const int FACES[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                        {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
        glm::vec3 middle(0.f);
        for (int c = 0; c < 8; c++) { middle += corners[c] * 0.125f; }
        for (const int *face : FACES) {
            const glm::vec3 &a = corners[face[0]];
            const glm::vec3 &b = corners[face[1]];
            const glm::vec3 &c = corners[face[2]];
            const glm::vec3 &d = corners[face[3]];
            glm::vec3 normal = glm::cross(c - a, d - b);
            const float length = glm::length(normal);
            if (length < 1e-6f) { continue; } // a ring that is one point has no face
            normal /= length;
            const glm::vec3 point = (a + b + c + d) * 0.25f;
            if (glm::dot(normal, middle - point) > 0.f) { normal = -normal; }
            // The sweep leaves the body through a face turned to the light; one that runs along
            // the light is left to the outline.
            if (glm::dot(normal, toLight) > -0.01f) { continue; }
            planes.push_back(glm::vec4(-normal, glm::dot(normal, point) + margin));
        }

        // The outline: the hull of the corners as the light sees them (Andrew's monotone chain,
        // counterclockwise).
        std::array<glm::vec2, 8> points;
        for (int c = 0; c < 8; c++) { points[c] = glm::vec2(glm::dot(corners[c], right), glm::dot(corners[c], above)); }
        std::sort(points.begin(), points.end(), [](const glm::vec2 &l, const glm::vec2 &r) {
            return l.x < r.x || (l.x == r.x && l.y < r.y);
        });
        const auto turn = [](const glm::vec2 &o, const glm::vec2 &p, const glm::vec2 &q) {
            return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x);
        };
        std::array<glm::vec2, 16> hull;
        int count = 0;
        for (int i = 0; i < 8; i++) {
            while (count >= 2 && turn(hull[count - 2], hull[count - 1], points[i]) <= 0.f) { count--; }
            hull[count++] = points[i];
        }
        for (int i = 6, lower = count + 1; i >= 0; i--) {
            while (count >= lower && turn(hull[count - 2], hull[count - 1], points[i]) <= 0.f) { count--; }
            hull[count++] = points[i];
        }
        count--; // the chain ends where it began
        if (count < 3) { return; }
        // Two corners the light sees as one point (it shines along an edge of the body) differ by
        // the rounding only: the edge between them has no direction to trust. Without its plane
        // the outline is wider by nothing to speak of.
        const float shortest = 1e-3f * glm::distance(points[0], points[7]);
        for (int i = 0; i < count; i++) {
            const glm::vec2 from = hull[i];
            const glm::vec2 edge = hull[(i + 1) % count] - from;
            const float length = glm::length(edge);
            if (length <= shortest) { continue; }
            const glm::vec2 outward = glm::vec2(edge.y, -edge.x) / length;
            planes.push_back(glm::vec4(-(right * outward.x + above * outward.y), glm::dot(outward, from) + margin));
        }
    }

} // namespace

CascadedShadowMap::~CascadedShadowMap() {
    releaseAtlas();
}

void CascadedShadowMap::releaseAtlas() {
    if (m_frameBuffer.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = MIREN_INVALID_HANDLE;
    }
    m_data.atlas = {};
    m_atlasCascades = 0;
    m_atlasTileSize = 0;
}

Miren::ViewId CascadedShadowMap::addCascadeView(uint32_t cascade) const {
    const int tileSize = static_cast<int>(m_atlasTileSize);
    return Miren::addView({
        .frameBuffer = m_frameBuffer,
        .viewport = Miren::Rect(static_cast<int>(cascade) * tileSize, 0, tileSize, tileSize),
        .clearsColor = false,
        .clearsDepth = cascade == 0,
    });
}

void CascadedShadowMap::ensureAtlas(uint32_t cascadeCount, uint32_t tileSize) {
    if (m_data.atlas && m_atlasCascades == cascadeCount && m_atlasTileSize == tileSize) { return; }
    // The FBO references the texture — tear it down before the recreation under the same asset.
    if (m_frameBuffer.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = MIREN_INVALID_HANDLE;
    }
    Miren::TextureCreate create;
    create.m_format = Miren::TextureFormat::DEPTH32F;
    create.m_width = static_cast<uint16_t>(cascadeCount * tileSize);
    create.m_height = static_cast<uint16_t>(tileSize);
    create.m_minFiltering = Miren::LINEAR; // hardware 2×2 PCF per sample
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP_TO_BORDER; // beyond the edge — lit
    create.m_compare = Miren::COMPARE_LESS_EQUAL;
    if (m_data.atlas) {
        m_data.atlas->create(create); // the same asset — the shader references stay alive
    } else {
        m_data.atlas = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
    }
    Miren::FrameBufferAttachment attachments[1] = {m_data.atlas->getMirenHandle()};
    m_frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    m_atlasCascades = cascadeCount;
    m_atlasTileSize = tileSize;
}

bool CascadedShadowMap::update(
    const Settings &settings,
    const glm::mat4 &cameraViewProj,
    const glm::vec3 &cameraPosition,
    const glm::vec3 &directionToLight
) {
    m_data.cascadeCount = 0;
    m_data.strength = 0.f;
    const bool active = settings.enabled && settings.cascadeCount > 0 && settings.strength > 0.f &&
                        glm::length(directionToLight) >= 0.001f && settings.distance > 0.f;
    if (!active) {
        releaseAtlas(); // disabled shadows do not hold tens of megabytes of the atlas
        return false;
    }
    const uint32_t cascadeCount = std::min(settings.cascadeCount, RENDERER_MAX_SHADOW_CASCADES);
    // The atlas width is limited by the maximum texture size of the backends (16384).
    const uint32_t tileSize = std::clamp(settings.tileSize, 256u, MAX_ATLAS_WIDTH / cascadeCount);
    ensureAtlas(cascadeCount, tileSize);

    // Slices of the camera pyramid by depth along the view axis; cascade 0 starts at the camera.
    CameraRays rays;
    if (!extractCameraRays(cameraViewProj, cameraPosition, settings.distance, rays)) { return false; }
    const glm::vec3 axis = rays.axis;
    const float shadowFar = std::min(settings.distance, rays.farDepth);

    // Slice boundaries: a blend of the uniform and the logarithmic split.
    float splits[RENDERER_MAX_SHADOW_CASCADES + 1] = {};
    const float logNear = std::max(0.5f, shadowFar * 0.01f);
    for (uint32_t i = 1; i <= cascadeCount; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(cascadeCount);
        const float uniform = shadowFar * t;
        const float logarithmic = logNear * std::pow(shadowFar / logNear, t);
        splits[i] = glm::mix(uniform, logarithmic, SPLIT_LAMBDA);
    }
    splits[cascadeCount] = shadowFar;

    const glm::vec3 light = glm::normalize(directionToLight);
    const glm::vec3 up = std::abs(light.y) > 0.99f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
    const glm::mat4 bias = textureBiasMatrix();
    // On GL the buffer depth = z*0.5+0.5 of the NDC [0..1] of the ZO projection: there are half
    // as many depth units per meter.
    const float depthPerMeterScale = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth ? 0.5f : 1.f;
    for (uint32_t i = 0; i < cascadeCount; i++) {
        const float d0 = splits[i];
        const float d1 = splits[i + 1];
        // Minimal sphere of the slice: the center is on the camera axis between the rings of the
        // slice corners.
        float r0 = 0.f;
        float r1 = 0.f;
        for (int c = 0; c < 4; c++) {
            r0 = std::max(r0, glm::distance(rays.cornerAt(c, d0), cameraPosition + axis * d0));
            r1 = std::max(r1, glm::distance(rays.cornerAt(c, d1), cameraPosition + axis * d1));
        }
        const float center1D = glm::clamp((d1 * d1 + r1 * r1 - d0 * d0 - r0 * r0) / (2.f * (d1 - d0)), d0, d1);
        const glm::vec3 center = cameraPosition + axis * center1D;
        float radius = 0.f;
        for (int c = 0; c < 4; c++) {
            radius = std::max(radius, glm::distance(rays.cornerAt(c, d0), center));
            radius = std::max(radius, glm::distance(rays.cornerAt(c, d1), center));
        }
        radius = std::ceil(radius * 64.f) / 64.f; // a stable texel size between frames

        // Light: the eye is on the front side of the sphere in the direction of the sun; casters
        // farther towards the sun are pancaked by the depth vertex shader (clip z < 0 is clamped
        // to 0).
        const glm::mat4 lightView = glm::lookAt(center + light * radius, center, up);
        glm::mat4 lightProj = glm::orthoZO(-radius, radius, -radius, radius, 0.f, 2.f * radius);
        // Snap to the texel: the projection of the world origin moves only by whole texels —
        // otherwise the shadow edges flicker as the camera moves (the sphere slides over the world
        // smoothly).
        const glm::mat4 lightViewProj = lightProj * lightView;
        const glm::vec4 origin = lightViewProj * glm::vec4(0.f, 0.f, 0.f, 1.f);
        const float texelNdc = 2.f / static_cast<float>(tileSize);
        const glm::vec2 snapped = glm::round(glm::vec2(origin) / texelNdc) * texelNdc;
        lightProj[3][0] += snapped.x - origin.x;
        lightProj[3][1] += snapped.y - origin.y;
        m_cascadeViewProj[i] = lightProj * lightView;
        m_data.textureMatrices[i] = bias * m_cascadeViewProj[i];
        m_data.splits[i] = d1;
        m_data.texelWorld[i] = 2.f * radius / static_cast<float>(tileSize);
        m_data.depthScale[i] = depthPerMeterScale / (2.f * radius);

        // Who casts into the cascade. The shader takes a cascade by the distance to the camera,
        // crossfades into it before its slice begins and falls through to it from a tile a point
        // missed: a point of any depth up to the far edge may ask this cascade, so the body is
        // the pyramid from the camera to that edge. The margin is the reach of a lookup around
        // its point: the widest filter and the offset along the normal.
        glm::vec3 corners[8];
        for (int c = 0; c < 4; c++) {
            corners[c] = rays.cornerAt(c, 0.f);
            corners[4 + c] = rays.cornerAt(c, d1);
        }
        const glm::vec3 right = glm::normalize(glm::cross(up, light));
        m_casterPlanes[i].clear();
        sweptSlicePlanes(
            corners, light, right, glm::cross(light, right), 0.5f + 3.f * m_data.texelWorld[i], m_casterPlanes[i]
        );
    }
    for (uint32_t i = cascadeCount; i < RENDERER_MAX_SHADOW_CASCADES; i++) {
        m_data.textureMatrices[i] = glm::mat4(1.f);
        m_data.splits[i] = 0.f;
        m_data.texelWorld[i] = 0.f;
        m_data.depthScale[i] = 0.f;
    }
    m_data.cascadeCount = cascadeCount;
    m_data.tileSize = tileSize;
    m_data.distance = shadowFar;
    m_data.strength = glm::clamp(settings.strength, 0.f, 1.f);
    m_data.normalOffset = settings.normalOffset;
    m_data.receiverBias = settings.receiverBias;
    m_data.slopeBias = settings.slopeBias;
    m_data.softness = settings.softness;
    return true;
}

} // namespace Panda

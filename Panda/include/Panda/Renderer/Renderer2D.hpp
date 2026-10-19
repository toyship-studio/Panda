#pragma once

#include "Panda/Base/Base.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Renderer/Vertex.hpp"
#include "Panda/Renderer/Renderer3D.hpp" // WorldLightingData (lighting for lit/pbr sprites)
#include "Panda/Renderer/RenderingMode.hpp"

#include <Miren/Miren.hpp>

namespace Panda {

/// Renderer2D is responsible for drawing primitive two-dimensional shapes.
/// For that Renderer2D creates a shader and buffers with vertices and indices.
/// Renderer2D issues the drawing after flush() is called;
/// If a non-standard framebuffer is required, it has to be created outside Renderer2D
/// and its Miren::ViewId passed in.
class Renderer2D {
public:
    struct RectData {
        RectData()
            : center()
            , size()
            , color()
            , material()
            , rotation(0)
            , id(-1)
            , textureCoords(0, 0, 1, 1)
            , transform(1.f) {}

        Vec3 center;
        Rect textureCoords;
        Size size;
        Color color;
        uint32_t id;
        float rotation;
        glm::mat4 transform;
        AssetRef<MaterialAsset> material;
        AssetRef<TextureAsset> textureOverride;
    };

    struct Statistics {
        uint32_t drawCalls = 0;
        uint32_t quadCount = 0;

        uint32_t getTotalVertexCount() const {
            return quadCount * 4;
        }
        uint32_t getTotalIndexCount() const {
            return quadCount * 6;
        }
    };

    struct DrawCallData {
        Renderer2D::Statistics stats;
        glm::mat4 projMat;
        AssetRef<MaterialAsset> material;
        AssetRef<TextureAsset> textureOverride;
        Vertex *vertices;
        uint32_t verticesCount;
        uint32_t *indices;
        uint32_t indicesCount;
        uint32_t vbSize;
        uint32_t ibSize;
    };

    Renderer2D();
    Renderer2D(Renderer2D &other) = delete;
    ~Renderer2D();
    void begin(RenderingMode mode, Miren::ViewId id);
    void drawRect(RectData rect);
    void end();
    Statistics getStats();
    void setViewProj(glm::mat4 viewProj);
    // Scene lighting for lit/pbr sprite materials (SCENE_LIGHTS must be bound — otherwise UB in GL).
    void setLighting(const WorldLightingData &lighting);
    Renderer2D &operator=(Renderer2D &other) = delete;

private:
    // Capacity of the batch buffers — asserted when a rect is written.
    static constexpr uint32_t MAX_VERTICES_COUNT = 100000;
    static constexpr uint32_t MAX_INDICES_COUNT = 160000;

    glm::mat4 m_viewProj;
    WorldLightingData m_sceneLighting;
    RenderingMode m_mode;
    Miren::ViewId m_viewId;
    DrawCallData m_drawData;

    void reset();
    void flush();
    void drawRect(glm::mat4 &transform, RectData rect);
};

} // namespace Panda

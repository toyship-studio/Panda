#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"
#include "Panda/Base/Base.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Panda {

class World;
class Renderer2D;
class Renderer3D;

// Feeding world geometry to the renderers by component families — functions over registry views;
// the order of passes is set by World::renderWorld (between sprites and meshes — the world systems
// from the Extensions::worldSystems registry). Common culling — the view frustum of the pass
// matrix (in the shadow depth pass — of the cascade).
namespace RenderSystem {

    // View frustum planes in the space of the matrix (Gribb–Hartmann), without near: behind the
    // camera the side planes converge to a point — the near plane is not needed for culling.
    // A pass that knows more than its matrix says narrows the frustum by planes of its own (a
    // shadow cascade: CascadedShadowMap::cascadeCasterPlanes).
    struct Frustum {
        static constexpr uint32_t MAX_PLANES = 24;
        glm::vec4 planes[MAX_PLANES];
        uint32_t count = 0;

        static Frustum of(const glm::mat4 &viewProj);
        // More planes to be inside of (dot(plane.xyz, point) + plane.w >= 0); those that do not
        // fit are left out — the frustum stays a little wider.
        void narrow(const std::vector<glm::vec4> &more);
        // AABB (mesh-local) against the planes after the transform: center/extents via abs(M).
        bool visible(const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax) const;
    };

    // Sky: the first entity with SkyComponent (the component's material or the default one).
    void submitSky(World &world, Renderer3D &renderer, const glm::mat4 &skyViewProj);
    // Sprites (they cast no shadow — not called in the depth pass).
    void submitSprites(World &world, Renderer2D &renderer, const Frustum &frustum);
    // The meshes a world draws in a frame — runtime meshes, greyboxing primitives, the primitives
    // of imported meshes — read from its registry ONCE (collect): the world transform of each
    // (the hierarchy is walked here and nowhere else), its bounds and its material. Every pass of
    // the frame — the scene, each shadow cascade and lamp tile, picking — culls and submits from
    // the list. A frame has many passes and a world many entities: walking the registry and the
    // hierarchy for each pass was the cost of a shadow.
    class MeshList final {
    public:
        void collect(World &world);
        // Opaque meshes first, then the blended ones (alphaMode 2 of the material) from the
        // farthest to the nearest to the camera, so that each is drawn over what is behind it.
        // Blended meshes cast no shadow: the depth pass skips them.
        void submit(
            Renderer3D &renderer, const Frustum &frustum, bool shadowPass, const glm::vec3 &cameraPosition
        ) const;
        // The box of the world around the collected meshes — all of them, or those of the given
        // entities. false — none of them has bounds (nothing to frame).
        bool bounds(glm::vec3 &outMin, glm::vec3 &outMax, const std::vector<UUID> *entities = nullptr) const;

    private:
        struct Item {
            UUID id;
            glm::mat4 transform;
            glm::vec3 boundsMin = glm::vec3(0.f);
            glm::vec3 boundsMax = glm::vec3(0.f);
            bool bounded = false; // no bounds — never culled
            bool blended = false;
            AssetRef<MaterialAsset> material;
            AssetRef<RuntimeMeshAsset> mesh;      // a runtime mesh or a primitive,
            AssetRef<ImportedMeshAsset> imported; // or a primitive of an imported mesh
            uint32_t submeshIndex = 0;
            uint32_t primitiveIndex = 0;
        };
        void submitItem(Renderer3D &renderer, const Item &item) const;

        std::vector<Item> m_items;
    };

} // namespace RenderSystem

} // namespace Panda

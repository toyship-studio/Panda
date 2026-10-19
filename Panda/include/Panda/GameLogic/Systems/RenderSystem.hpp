#pragma once

#include <glm/glm.hpp>

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
    struct Frustum {
        glm::vec4 planes[5];

        static Frustum of(const glm::mat4 &viewProj);
        // AABB (mesh-local) against the planes after the transform: center/extents via abs(M).
        bool visible(const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax) const;
    };

    // Sky: the first entity with SkyComponent (the component's material or the default one).
    void submitSky(World &world, Renderer3D &renderer, const glm::mat4 &skyViewProj);
    // Sprites (they cast no shadow — not called in the depth pass).
    void submitSprites(World &world, Renderer2D &renderer, const Frustum &frustum);
    // Meshes: runtime, greyboxing primitives, imported submeshes. Transparent ones (alphaMode = 2)
    // are drawn after all opaque ones — otherwise their depth hides the geometry behind; they cast
    // no shadow (not accumulated in the depth pass).
    void submitMeshes(World &world, Renderer3D &renderer, const Frustum &frustum, bool shadowPass);

} // namespace RenderSystem

} // namespace Panda

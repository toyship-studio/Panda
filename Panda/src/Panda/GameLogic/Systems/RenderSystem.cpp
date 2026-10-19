#include "Panda/GameLogic/Systems/RenderSystem.hpp"

#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Base/ColorSpace.hpp"
#include "Panda/GameLogic/World.hpp"

#include <Foundation/Profile.hpp>

namespace Panda::RenderSystem {

Frustum Frustum::of(const glm::mat4 &m) {
    const auto row = [&m](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    Frustum frustum;
    frustum.planes[0] = row(3) + row(0); // left
    frustum.planes[1] = row(3) - row(0); // right
    frustum.planes[2] = row(3) + row(1); // bottom
    frustum.planes[3] = row(3) - row(1); // top
    frustum.planes[4] = row(3) - row(2); // far
    return frustum;
}

bool Frustum::visible(const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax) const {
    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 localExtents = (localMax - localMin) * 0.5f;
    const glm::vec3 center = glm::vec3(transform * glm::vec4(localCenter, 1.f));
    const glm::vec3 extents = glm::abs(glm::vec3(transform[0])) * localExtents.x +
                              glm::abs(glm::vec3(transform[1])) * localExtents.y +
                              glm::abs(glm::vec3(transform[2])) * localExtents.z;
    for (const glm::vec4 &plane : planes) {
        const glm::vec3 normal(plane);
        const float distance = glm::dot(normal, center) + plane.w;
        const float radius = glm::dot(glm::abs(normal), extents);
        if (distance + radius < 0.f) { return false; }
    }
    return true;
}

void submitSky(World &world, Renderer3D &renderer, const glm::mat4 &skyViewProj) {
    auto view = world.getAllEntitiesWith<SkyComponent>();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        AssetRef<RuntimeMeshAsset> skyMesh = StaticResources::defaultSkyMesh;
        auto &skyComponent = view.get<SkyComponent>(entityHandle);
        AssetRef<MaterialAsset> skyMaterial = skyComponent.material.as<MaterialAsset>();
        if (!skyMaterial) { skyMaterial = StaticResources::defaultSkyMaterial; }
        glm::mat4 viewProj = skyViewProj;
        renderer.submitSky(viewProj, skyMesh, skyMaterial);
        break;
    }
}

void submitSprites(World &world, Renderer2D &renderer, const Frustum &frustum) {
    auto view = world.getAllEntitiesWith<IdComponent, SpriteRendererComponent, TransformComponent>();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        auto &id = view.get<IdComponent>(entityHandle);
        auto &spriteComponent = view.get<SpriteRendererComponent>(entityHandle);
        Renderer2D::RectData rect;
        rect.transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
        if (!frustum.visible(rect.transform, glm::vec3(-0.5f, -0.5f, -0.01f), glm::vec3(0.5f, 0.5f, 0.01f))) {
            continue;
        }
        // The color is authored (sRGB from the color picker): the vertex color goes into the linear
        // pipeline of the shader (the tonemap encodes on output) — it is converted here, like the
        // light and the COLOR fields of materials. It must not be converted in the shader: for
        // layered materials and flora this channel is data.
        rect.color = srgbToLinear(spriteComponent.color);
        if (spriteComponent.material) { rect.material = spriteComponent.material.as<MaterialAsset>(); }
        if (spriteComponent.texture) {
            AssetRef<TextureAsset> texture = spriteComponent.texture.as<TextureAsset>();
            if (texture.get()) { rect.textureOverride = texture; }
        }
        rect.size = {1.f, 1.f};
        rect.id = id.id;
        const int xTileIndex = spriteComponent.index % spriteComponent.cols;
        const int yTileIndex = spriteComponent.index % spriteComponent.rows;
        const float tileWidth = 1.f / spriteComponent.cols;
        const float tileHeight = 1.f / spriteComponent.rows;
        rect.textureCoords = {tileWidth * xTileIndex, tileHeight * yTileIndex, tileWidth, tileHeight};
        renderer.drawRect(rect);
    }
}

void submitMeshes(World &world, Renderer3D &renderer, const Frustum &frustum, bool shadowPass) {
    struct BlendedRuntimeMesh {
        UUID id;
        glm::mat4 transform;
        AssetRef<RuntimeMeshAsset> mesh;
        AssetRef<MaterialAsset> material;
    };
    std::vector<BlendedRuntimeMesh> blendedMeshes;
    {
        PANDA_PROFILE_SCOPE_N("World: runtime meshes");
        auto view = world.getAllEntitiesWith<IdComponent, RuntimeMeshComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &meshComponent = view.get<RuntimeMeshComponent>(entityHandle);
            if (!meshComponent.runtimeMesh) { continue; }
            auto &id = view.get<IdComponent>(entityHandle);
            glm::mat4 transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            AssetRef<RuntimeMeshAsset> mesh = meshComponent.runtimeMesh.as<RuntimeMeshAsset>();
            glm::vec3 boundsMin, boundsMax;
            if (mesh.get() && mesh->getLocalBounds(boundsMin, boundsMax) &&
                !frustum.visible(transform, boundsMin, boundsMax)) {
                continue;
            }
            AssetRef<MaterialAsset> material = meshComponent.material.as<MaterialAsset>();
            if (material && material->getUIntFieldValue("alphaMode", 0u) == 2u) {
                // Transparent geometry casts no shadow (Renderer3D would skip it itself — it is
                // not collected).
                if (!shadowPass) { blendedMeshes.push_back({id.id, transform, mesh, material}); }
                continue;
            }
            renderer.submit(id.id, transform, mesh, material);
        }
        // Greyboxing primitives: a builtin mesh by kind, an empty material = checker.
        auto primitiveView = world.getAllEntitiesWith<IdComponent, PrimitiveMeshComponent, TransformComponent>();
        for (auto entityHandle : primitiveView) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &primitiveComponent = primitiveView.get<PrimitiveMeshComponent>(entityHandle);
            AssetRef<RuntimeMeshAsset> mesh = StaticResources::primitiveMeshOf(primitiveComponent.kind);
            if (!mesh) { continue; }
            auto &id = primitiveView.get<IdComponent>(entityHandle);
            glm::mat4 transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            glm::vec3 boundsMin, boundsMax;
            if (mesh->getLocalBounds(boundsMin, boundsMax) && !frustum.visible(transform, boundsMin, boundsMax)) {
                continue;
            }
            AssetRef<MaterialAsset> material = primitiveComponent.material.as<MaterialAsset>();
            if (!material.get()) { material = StaticResources::primitiveMaterial; }
            renderer.submit(id.id, transform, mesh, material);
        }
    }
    {
        PANDA_PROFILE_SCOPE_N("World: imported submeshes");
        auto view = world.getAllEntitiesWith<IdComponent, ImportedSubmeshComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &meshComponent = view.get<ImportedSubmeshComponent>(entityHandle);
            if (!meshComponent.importedMesh) { continue; }
            auto &id = view.get<IdComponent>(entityHandle);
            glm::mat4 transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            AssetRef<ImportedMeshAsset> mesh = meshComponent.importedMesh.as<ImportedMeshAsset>();
            const uint32_t submeshIndex = meshComponent.submeshIndex;
            const uint32_t primitiveIndex = meshComponent.primitiveIndex;
            glm::vec3 boundsMin, boundsMax;
            if (mesh.get() && mesh->getPrimitiveBounds(submeshIndex, primitiveIndex, boundsMin, boundsMax) &&
                !frustum.visible(transform, boundsMin, boundsMax)) {
                continue;
            }
            // Material: the component override, otherwise — the slot layout of the mesh itself.
            AssetRef<Asset> material = meshComponent.material;
            if (!material && mesh.get()) { material = mesh->materialForPrimitive(submeshIndex, primitiveIndex); }
            renderer.submit(id.id, transform, mesh, submeshIndex, primitiveIndex, material.as<MaterialAsset>());
        }
    }
    for (BlendedRuntimeMesh &blended : blendedMeshes) {
        renderer.submit(blended.id, blended.transform, blended.mesh, blended.material);
    }
}

} // namespace Panda::RenderSystem

#include "Panda/GameLogic/Systems/RenderSystem.hpp"

#include "Panda/Renderer/Renderer2D.hpp"
#include "Panda/Renderer/Renderer3D.hpp"

#include <algorithm>

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
    frustum.count = 5;
    return frustum;
}

void Frustum::narrow(const std::vector<glm::vec4> &more) {
    for (const glm::vec4 &plane : more) {
        if (count == MAX_PLANES) { return; }
        planes[count++] = plane;
    }
}

bool Frustum::visible(const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax) const {
    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 localExtents = (localMax - localMin) * 0.5f;
    const glm::vec3 center = glm::vec3(transform * glm::vec4(localCenter, 1.f));
    const glm::vec3 extents = glm::abs(glm::vec3(transform[0])) * localExtents.x +
                              glm::abs(glm::vec3(transform[1])) * localExtents.y +
                              glm::abs(glm::vec3(transform[2])) * localExtents.z;
    for (uint32_t i = 0; i < count; i++) {
        const glm::vec4 &plane = planes[i];
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

void MeshList::collect(World &world) {
    PANDA_PROFILE_SCOPE_N("World: mesh list");
    m_items.clear();
    const auto isBlended = [](const AssetRef<MaterialAsset> &material) {
        return material && material->getUIntFieldValue("alphaMode", 0u) == 2u;
    };
    {
        auto view = world.getAllEntitiesWith<IdComponent, RuntimeMeshComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &meshComponent = view.get<RuntimeMeshComponent>(entityHandle);
            if (!meshComponent.runtimeMesh) { continue; }
            Item item;
            item.id = view.get<IdComponent>(entityHandle).id;
            item.transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            item.mesh = meshComponent.runtimeMesh.as<RuntimeMeshAsset>();
            item.bounded = item.mesh.get() && item.mesh->getLocalBounds(item.boundsMin, item.boundsMax);
            item.material = meshComponent.material.as<MaterialAsset>();
            item.blended = isBlended(item.material);
            m_items.push_back(std::move(item));
        }
        // Greyboxing primitives: a builtin mesh by kind, an empty material = checker.
        auto primitiveView = world.getAllEntitiesWith<IdComponent, PrimitiveMeshComponent, TransformComponent>();
        for (auto entityHandle : primitiveView) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &primitiveComponent = primitiveView.get<PrimitiveMeshComponent>(entityHandle);
            Item item;
            item.mesh = StaticResources::primitiveMeshOf(primitiveComponent.kind);
            if (!item.mesh) { continue; }
            item.id = primitiveView.get<IdComponent>(entityHandle).id;
            item.transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            item.bounded = item.mesh->getLocalBounds(item.boundsMin, item.boundsMax);
            item.material = primitiveComponent.material.as<MaterialAsset>();
            if (!item.material.get()) { item.material = StaticResources::primitiveMaterial; }
            item.blended = isBlended(item.material);
            m_items.push_back(std::move(item));
        }
    }
    {
        auto view = world.getAllEntitiesWith<IdComponent, ImportedSubmeshComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &meshComponent = view.get<ImportedSubmeshComponent>(entityHandle);
            if (!meshComponent.importedMesh) { continue; }
            Item item;
            item.id = view.get<IdComponent>(entityHandle).id;
            item.transform = world.getWorldSpaceTransformMatrix(world.entity(entityHandle));
            item.imported = meshComponent.importedMesh.as<ImportedMeshAsset>();
            item.submeshIndex = meshComponent.submeshIndex;
            item.primitiveIndex = meshComponent.primitiveIndex;
            item.bounded = item.imported.get() &&
                           item.imported->getPrimitiveBounds(
                               item.submeshIndex, item.primitiveIndex, item.boundsMin, item.boundsMax
                           );
            // Material: the component override, otherwise — the slot layout of the mesh itself.
            AssetRef<Asset> slot = meshComponent.material;
            if (!slot && item.imported.get()) {
                slot = item.imported->materialForPrimitive(item.submeshIndex, item.primitiveIndex);
            }
            item.material = slot.as<MaterialAsset>();
            item.blended = isBlended(item.material);
            m_items.push_back(std::move(item));
        }
    }
}

bool MeshList::bounds(glm::vec3 &outMin, glm::vec3 &outMax, const std::vector<UUID> *entities) const {
    bool found = false;
    for (const Item &item : m_items) {
        if (!item.bounded) { continue; }
        if (entities != nullptr && std::find(entities->begin(), entities->end(), item.id) == entities->end()) { continue; }
        // The box of the transformed local box: its center and its extents through abs(M).
        const glm::vec3 center = glm::vec3(item.transform * glm::vec4((item.boundsMin + item.boundsMax) * 0.5f, 1.f));
        const glm::vec3 half = (item.boundsMax - item.boundsMin) * 0.5f;
        const glm::mat3 axes(item.transform);
        const glm::vec3 extents = glm::abs(axes[0]) * half.x + glm::abs(axes[1]) * half.y + glm::abs(axes[2]) * half.z;
        if (!found) {
            outMin = center - extents;
            outMax = center + extents;
            found = true;
        } else {
            outMin = glm::min(outMin, center - extents);
            outMax = glm::max(outMax, center + extents);
        }
    }
    return found;
}

void MeshList::submitItem(Renderer3D &renderer, const Item &item) const {
    glm::mat4 transform = item.transform;
    if (item.imported) {
        renderer.submit(item.id, transform, item.imported, item.submeshIndex, item.primitiveIndex, item.material);
    } else {
        renderer.submit(item.id, transform, item.mesh, item.material);
    }
}

void MeshList::submit(
    Renderer3D &renderer, const Frustum &frustum, bool shadowPass, const glm::vec3 &cameraPosition
) const {
    PANDA_PROFILE_SCOPE_N("World: meshes");
    struct Blended {
        float distance; // from the camera to the middle of the bounds
        const Item *item;
    };
    std::vector<Blended> blended;
    for (const Item &item : m_items) {
        if (item.bounded && !frustum.visible(item.transform, item.boundsMin, item.boundsMax)) { continue; }
        if (!item.blended) {
            submitItem(renderer, item);
            continue;
        }
        // A blended surface waits until the opaque scene is drawn; it casts no shadow.
        if (shadowPass) { continue; }
        const glm::vec3 middle = item.bounded ? (item.boundsMin + item.boundsMax) * 0.5f : glm::vec3(0.f);
        blended.push_back({glm::distance(glm::vec3(item.transform * glm::vec4(middle, 1.f)), cameraPosition), &item});
    }
    std::stable_sort(blended.begin(), blended.end(), [](const Blended &a, const Blended &b) {
        return a.distance > b.distance;
    });
    for (const Blended &entry : blended) { submitItem(renderer, *entry.item); }
}

} // namespace Panda::RenderSystem

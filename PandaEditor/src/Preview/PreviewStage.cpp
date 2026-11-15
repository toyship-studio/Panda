#include "Preview/PreviewStage.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/ImportedMeshAsset.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Renderer/PrimitiveMeshes.hpp>
#include <Panda/Renderer/Vertex.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>

#include <glm/ext.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Panda {

namespace {

    constexpr float FIELD_OF_VIEW = glm::radians(38.f); // vertical
    constexpr float MAX_PITCH = glm::radians(89.f);

    MeshData primitiveData(PrimitiveMeshKind kind) {
        switch (kind) {
            case PrimitiveMeshKind::Cube: return PrimitiveMeshes::buildCube();
            case PrimitiveMeshKind::Capsule: return PrimitiveMeshes::buildCapsule();
            case PrimitiveMeshKind::Plane: return PrimitiveMeshes::buildPlane();
            case PrimitiveMeshKind::Cylinder: return PrimitiveMeshes::buildCylinder();
            case PrimitiveMeshKind::Sphere: break;
        }
        return PrimitiveMeshes::buildSphere();
    }

    // The primitive in the vertices a module rule asks for: its vertex color (a rule reads it as
    // data) and, when it has one, its own vertex format.
    AssetRef<RuntimeMeshAsset> ruleMesh(const Editor::MaterialPreview &rule, PrimitiveMeshKind kind) {
        MeshData data = primitiveData(kind);
        const size_t count = data.vertexBufferSize / sizeof(Vertex);
        const Vertex *source = static_cast<const Vertex *>(data.vertexBuffer.data);
        std::vector<Vertex> vertices(source, source + count);
        data.vertexBuffer.release();
        const Color &tint = rule.vertexColor;
        for (Vertex &vertex : vertices) { vertex.color = glm::vec4(tint.r, tint.g, tint.b, tint.a); }
        if (rule.packVertices != nullptr && rule.vertexLayout != nullptr) {
            uint32_t size = 0;
            data.vertexBuffer = rule.packVertices(vertices, size);
            data.vertexBufferSize = size;
            data.vertexStride = static_cast<uint32_t>(size / count); // the position leads: the bounds are read from it
            data.layoutHandle = rule.vertexLayout();
        } else {
            data.vertexBufferSize = static_cast<uint32_t>(sizeof(Vertex) * vertices.size());
            data.vertexBuffer = Foundation::Memory::copying(vertices.data(), data.vertexBufferSize);
        }
        AssetRef<RuntimeMeshAsset> mesh = GameContext::getAssetHandler()->createRuntimeAsset<RuntimeMeshAsset>();
        mesh->update(data);
        return mesh;
    }

} // namespace

PreviewStage::PreviewStage() {
    setUpStudio();
}

// The studio: the built-in sky lights the subject and is reflected by it, a little flat fill,
// a key light from the upper right of the first view. The light stands still while the camera
// orbits — the far side of the subject is its shaded side. No shadows, no occlusion: their
// passes belong to the frame of the viewport (World::draw).
void PreviewStage::setUpStudio() {
    WorldLightingSettings &lighting = m_world.getLightingSettings();
    lighting.ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    lighting.ambientIntensity = 0.1f;
    lighting.environmentIntensity = 1.f;
    WorldGraphicsSettings &graphics = m_world.getGraphicsSettings();
    graphics.shadows.enabled = false;
    graphics.occlusion.enabled = false;
    graphics.ssao.enabled = false;
    graphics.fog.enabled = false;

    Entity key = m_world.instantiateEntity();
    key.setName("Key Light");
    // A light shines along the -Z of its entity: turned to come from the upper right front.
    const glm::vec3 toLight = glm::normalize(glm::vec3(0.45f, 0.55f, 1.f));
    key.getTransform().setRotation(glm::quatLookAt(-toLight, glm::vec3(0.f, 1.f, 0.f)));
    DirectionalLightComponent &light = key.addComponent<DirectionalLightComponent>();
    light.intensity = 1.6f;
    light.castShadows = false;
}

void PreviewStage::clear() {
    if (m_asset == AssetId(0)) { return; }
    m_world.clear(); // the settings go back to their defaults with the entities
    m_ruleMesh.reset();
    m_rule = -1;
    m_asset = AssetId(0);
    setUpStudio();
}

bool PreviewStage::show(AssetId asset, PrimitiveMeshKind shape) {
    clear();
    auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (handler == nullptr) { return false; }
    // A type guard, not just an existence check: AssetRef<T> on a foreign id resolves an object
    // of another class.
    const auto it = handler->getRegistry().find(asset);
    if (it == handler->getRegistry().end()) { return false; }
    const bool shown = it->second.type == AssetType::MATERIAL        ? showMaterial(asset, shape)
                       : it->second.type == AssetType::IMPORTED_MESH ? showModel(asset)
                                                                     : false;
    if (!shown) {
        clear();
        return false;
    }
    m_asset = asset;
    m_shape = shape;
    return true;
}

bool PreviewStage::showMaterial(AssetId asset, PrimitiveMeshKind shape) {
    AssetRef<MaterialAsset> material(asset);
    if (!material || !material->getShaderAsset() || !material->getShaderAsset()->getMirenHandle().isValid()) { return false; }
    Entity subject = m_world.instantiateEntity();
    subject.setName("Material");
    // The first rule a material falls under decides its vertices.
    const std::vector<Editor::MaterialPreview> &rules = Editor::editorExtensions().materialPreviews;
    for (size_t i = 0; i < rules.size(); i++) {
        if (rules[i].matches == nullptr || !rules[i].matches(*material.get())) { continue; }
        m_rule = static_cast<int>(i);
        m_ruleMesh = ruleMesh(rules[i], shape);
        break;
    }
    if (m_ruleMesh) {
        RuntimeMeshComponent &mesh = subject.addComponent<RuntimeMeshComponent>();
        mesh.runtimeMesh = AssetRef<Asset>(m_ruleMesh.getId());
        mesh.material = AssetRef<Asset>(asset);
    } else {
        PrimitiveMeshComponent &mesh = subject.addComponent<PrimitiveMeshComponent>();
        mesh.kind = shape;
        mesh.material = AssetRef<Asset>(asset);
    }
    // A ball, a cylinder and a capsule are round about the vertical axis: they reach as far as
    // their box does along an axis, not as far as its corners.
    measure(shape == PrimitiveMeshKind::Sphere || shape == PrimitiveMeshKind::Cylinder || shape == PrimitiveMeshKind::Capsule);
    return true;
}

bool PreviewStage::showModel(AssetId asset) {
    auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    AssetRef<ImportedMeshAsset> mesh(asset);
    if (!mesh) { return false; }
    const UUID root = AssetImporterBase::addGltfSceneToWorld(handler->getProjectPath() / handler->assetPath(asset), mesh, m_world);
    if (!m_world.getById(root).isValid()) { return false; }
    measure(/*round*/ false);
    return true;
}

// What the camera fits, from the box of the meshes on the stage: the subject turns about its
// vertical axis under an orbiting camera, so across the frame it takes its reach from that axis
// (the corners of the box; the side of the box for a round thing), and up the frame its height.
void PreviewStage::measure(bool round) {
    RenderSystem::MeshList meshes;
    meshes.collect(m_world);
    glm::vec3 boundsMin(-0.5f);
    glm::vec3 boundsMax(0.5f);
    meshes.bounds(boundsMin, boundsMax);
    m_center = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 half = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(1.e-4f));
    m_reach = round ? std::max(half.x, half.z) : std::sqrt(half.x * half.x + half.z * half.z);
    m_halfHeight = half.y;
}

PreviewOrbit PreviewStage::defaultOrbit(AssetType type) {
    PreviewOrbit orbit;
    if (type == AssetType::IMPORTED_MESH) {
        orbit.yaw = glm::radians(28.f);
        orbit.pitch = glm::radians(-16.f);
    }
    return orbit;
}

void PreviewStage::draw(Miren::ViewId view, const PreviewOrbit &orbit, float aspect) {
    aspect = std::max(aspect, 0.01f);
    const float pitch = std::clamp(orbit.pitch, -MAX_PITCH, MAX_PITCH);
    // The distance the subject fits the frame from: across — at any turn about the vertical
    // axis; up — at this pitch (seen from above, a thing shows its depth as height). The part of
    // the subject nearest to the camera is larger in the frame: the reach is added to the distance.
    const float tanY = std::tan(FIELD_OF_VIEW * 0.5f);
    const float tanX = tanY * aspect;
    const float height = m_halfHeight * std::cos(pitch) + m_reach * std::sin(std::abs(pitch));
    const float fit = std::max(m_reach / tanX, height / tanY) * 1.06f + m_reach * 0.5f;
    const float distance = fit / std::clamp(orbit.zoom, 0.05f, 50.f);
    // The angles of the viewport camera (CameraController): the same gesture turns both.
    const glm::quat rotation(glm::vec3(pitch, orbit.yaw, 0.f));
    const glm::vec3 forward = rotation * glm::vec3(0.f, 0.f, -1.f);
    const glm::vec3 position = m_center - forward * distance;
    const float radius = std::sqrt(m_reach * m_reach + m_halfHeight * m_halfHeight); // the sphere around the subject
    const float nearPlane = std::max(distance - radius * 1.2f, std::max(distance, radius) * 0.01f);
    const float farPlane = distance + radius * 1.2f;

    WorldFrame frame;
    frame.projection = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth
                           ? glm::perspective(FIELD_OF_VIEW, aspect, nearPlane, farPlane)
                           : glm::perspectiveZO(FIELD_OF_VIEW, aspect, nearPlane, farPlane);
    const glm::mat4 orientation = glm::mat4_cast(rotation);
    frame.viewProj = frame.projection * glm::inverse(glm::translate(glm::mat4(1.f), position) * orientation);
    frame.skyViewProj = frame.projection * glm::inverse(orientation);
    frame.cameraPosition = position;

    // The view clears its target also when the stage is empty (a view without draws is not executed).
    Miren::touch(view);
    const std::vector<Editor::MaterialPreview> &rules = Editor::editorExtensions().materialPreviews;
    if (m_rule >= 0 && m_rule < static_cast<int>(rules.size()) && rules[static_cast<size_t>(m_rule)].bindInputs != nullptr) {
        rules[static_cast<size_t>(m_rule)].bindInputs(view);
    }
    m_world.draw(view, frame);
}

} // namespace Panda

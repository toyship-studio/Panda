#include "Viewport/EditorHelpersRenderer.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/GameLogic/SelectionContext.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>

namespace Panda {

EditorHelpersRenderer::EditorHelpersRenderer()
    : m_sceneProgram()
    , m_idsProgram()
    , m_indexBuffer()
    , m_vertexBuffer()
    , m_directionalLightIcon("ui/editor-helpers/directional_light.png")
    , m_pointLightIcon("ui/editor-helpers/point_light.png")
    , m_cameraIcon("ui/editor-helpers/camera.png") {
    initResources();
}

EditorHelpersRenderer::~EditorHelpersRenderer() {
    freeResources();
}

void EditorHelpersRenderer::initResources() {
    float vertices[8] = {
        -1.f,
        1.f,
        1.f,
        1.f,
        1.f,
        -1.f,
        -1.f,
        -1.f,
    };
    uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec2();
    Miren::VertexLayoutHandle layoutHandle = Miren::createVertexLayout(layoutData);
    Foundation::Memory verticesMemory = Foundation::Memory::copying(vertices, sizeof(vertices));
    Foundation::Memory indicesMemory = Foundation::Memory::copying(indices, sizeof(indices));
    m_vertexBuffer = Miren::createVertexBuffer(verticesMemory, sizeof(vertices), layoutHandle);
    m_indexBuffer = Miren::createIndexBuffer(indicesMemory, Miren::BufferElementType::UnsignedInt, 6);

    m_sceneProgram = Miren::createProgram(Panda::AssetImporterBase::loadShaderProgram(
        "editor-shaders/helper_icon.vert", "editor-shaders/helper_icon.frag"
    ));
    m_idsProgram = Miren::createProgram(Panda::AssetImporterBase::loadShaderProgram(
        "editor-shaders/helper_icon.vert", "editor-shaders/helper_icon_ids.frag"
    ));
}

void EditorHelpersRenderer::freeResources() {
    Miren::deleteVertexBuffer(m_vertexBuffer);
    Miren::deleteIndexBuffer(m_indexBuffer);
    Miren::deleteProgram(m_sceneProgram);
    Miren::deleteProgram(m_idsProgram);
}

Miren::TextureHandle EditorHelpersRenderer::getIconTextureHandle(IconType type) {
    switch (type) {
        case IconType::DirectionalLight:
            return m_directionalLightIcon.getMirenHandle();
        case IconType::PointLight:
            return m_pointLightIcon.getMirenHandle();
        case IconType::Camera:
        default:
            return m_cameraIcon.getMirenHandle();
    }
}

void EditorHelpersRenderer::submitIcon(
    const IconInstance &icon, const glm::mat4 &viewProj, Size viewportSize, bool idsPass
) {
    Miren::ProgramHandle program = idsPass ? m_idsProgram : m_sceneProgram;

    Std140Buffer vertUbo;
    vertUbo.addMat4(&viewProj[0][0]);
    vertUbo.addVec4(icon.position.x, icon.position.y, icon.position.z, icon.sizePixels);
    vertUbo.addVec4(static_cast<float>(viewportSize.width), static_cast<float>(viewportSize.height), 0.f, 0.f);
    Miren::addInputUniformBuffer("HELPER_VERT", vertUbo.getData(), vertUbo.getSize());

    Std140Buffer fragUbo;
    fragUbo.addVec4(icon.tint.x, icon.tint.y, icon.tint.z, icon.tint.w);
    fragUbo.addUInt(static_cast<uint32_t>(icon.ownerId));
    fragUbo.addUInt(0u);
    fragUbo.addUInt(0u);
    fragUbo.addUInt(0u);
    Miren::addInputUniformBuffer("HELPER_META", fragUbo.getData(), fragUbo.getSize());
    Miren::addInputTexture("iconTexture", getIconTextureHandle(icon.type));

    Miren::setVertexBuffer(m_vertexBuffer);
    Miren::setIndexBuffer(m_indexBuffer, 0, 6);
    Miren::setState(idsPass ? 0u : MIREN_STATE_BLEND);
    Miren::submit(idsPass ? Views::IDS_VIEW : Views::SCENE_VIEW, program);
}

void EditorHelpersRenderer::render(World *world, const glm::mat4 &viewProj, Size viewportSize) {
    if (!world) { return; }

    const std::unordered_set<UUID> selectedIds = SelectionContext::getSelectedEntities();

    auto directionalLights = world->getAllEntitiesWith<IdComponent, DirectionalLightComponent, TransformComponent>();
    for (auto entityHandle : directionalLights) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        UUID id = directionalLights.get<IdComponent>(entityHandle).id;
        Entity entity = world->getById(id);
        auto &light = entity.getComponent<DirectionalLightComponent>();
        glm::vec3 position = glm::vec3(world->getWorldSpaceTransformMatrix(entity) * glm::vec4(0.f, 0.f, 0.f, 1.f));

        const bool selected = selectedIds.contains(id);
        IconInstance icon;
        icon.ownerId = id;
        icon.position = position;
        icon.type = IconType::DirectionalLight;
        icon.sizePixels = selected ? 18.f : 14.f;
        icon.tint = light.isEnabled ? Color(1.0f, 1.0f, 1.0f, 0.95f) : Color(0.45f, 0.45f, 0.45f, 0.85f);
        submitIcon(icon, viewProj, viewportSize, false);
        submitIcon(icon, viewProj, viewportSize, true);
    }

    auto pointLights = world->getAllEntitiesWith<IdComponent, PointLightComponent, TransformComponent>();
    for (auto entityHandle : pointLights) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        UUID id = pointLights.get<IdComponent>(entityHandle).id;
        Entity entity = world->getById(id);
        auto &light = entity.getComponent<PointLightComponent>();
        glm::vec3 position = glm::vec3(world->getWorldSpaceTransformMatrix(entity) * glm::vec4(0.f, 0.f, 0.f, 1.f));

        const bool selected = selectedIds.contains(id);
        IconInstance icon;
        icon.ownerId = id;
        icon.position = position;
        icon.type = IconType::PointLight;
        icon.sizePixels = selected ? 17.f : 13.f;
        icon.tint = light.isEnabled ? light.color : Color(0.45f, 0.45f, 0.45f, 0.85f);
        submitIcon(icon, viewProj, viewportSize, false);
        submitIcon(icon, viewProj, viewportSize, true);
    }

    auto cameras = world->getAllEntitiesWith<IdComponent, CameraComponent, TransformComponent>();
    for (auto entityHandle : cameras) {
        if (!world->isValidEntt(entityHandle)) { continue; }
        UUID id = cameras.get<IdComponent>(entityHandle).id;
        Entity entity = world->getById(id);
        auto &camera = entity.getComponent<CameraComponent>();
        glm::vec3 position = glm::vec3(world->getWorldSpaceTransformMatrix(entity) * glm::vec4(0.f, 0.f, 0.f, 1.f));

        const bool selected = selectedIds.contains(id);
        IconInstance icon;
        icon.ownerId = id;
        icon.position = position;
        icon.type = IconType::Camera;
        icon.sizePixels = selected ? 18.f : 14.f;
        icon.tint = camera.isPrimary ? Color(0.70f, 1.0f, 0.86f, 0.95f) : Color(1.0f, 1.0f, 1.0f, 0.95f);
        submitIcon(icon, viewProj, viewportSize, false);
        submitIcon(icon, viewProj, viewportSize, true);
    }
}

} // namespace Panda

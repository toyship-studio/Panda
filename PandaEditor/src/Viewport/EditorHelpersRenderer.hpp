#pragma once

#include <Panda/Assets/TextureAsset.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/Std140Buffer.hpp>

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

class EditorHelpersRenderer final {
public:
    EditorHelpersRenderer();
    ~EditorHelpersRenderer();

    EditorHelpersRenderer(const EditorHelpersRenderer &) = delete;
    EditorHelpersRenderer &operator=(const EditorHelpersRenderer &) = delete;

    // The icons of the lights and the cameras of the world, each twice: into the scene pass of
    // the frame (what the user sees) and into the picking pass (a click on an icon selects its
    // entity).
    void render(
        World *world, const glm::mat4 &viewProj, Size viewportSize, Miren::ViewId sceneView, Miren::ViewId idsView
    );

private:
    enum class IconType : uint32_t { DirectionalLight = 0, PointLight = 1, Camera = 2 };

    struct IconInstance final {
        UUID ownerId = 0;
        glm::vec3 position = glm::vec3(0.f);
        Color tint = Color(1.f, 1.f, 1.f, 1.f);
        IconType type = IconType::PointLight;
        float sizePixels = 14.f;
    };

    void initResources();
    void freeResources();
    void submitIcon(const IconInstance &icon, const glm::mat4 &viewProj, Size viewportSize, bool idsPass);
    Miren::TextureHandle getIconTextureHandle(IconType type);

    Miren::ViewId m_sceneView = 0; // of the render in progress

    Miren::ViewId m_idsView = 0;

    Miren::ProgramHandle m_sceneProgram;
    Miren::ProgramHandle m_idsProgram;
    Miren::IndexBufferHandle m_indexBuffer;
    Miren::VertexBufferHandle m_vertexBuffer;
    TextureAsset m_directionalLightIcon;
    TextureAsset m_pointLightIcon;
    TextureAsset m_cameraIcon;
};

} // namespace Panda

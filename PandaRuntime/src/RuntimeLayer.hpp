#pragma once

#include "Assets/AssetHandlerRuntime.hpp"
#include "ProjectLoader/RuntimeProjectLoader.hpp"

#include <Panda/Application/Layer.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/PostprocessingPipeline.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>

#include <cstdint>
#include <memory>

namespace Panda {

class PandaUIImpl;

class RuntimeLayer final : public Layer {
public:
    explicit RuntimeLayer(const path_t &runtimeDataPath);
    ~RuntimeLayer() override;

    void onAttach() override;
    void onUpdate(double deltaTime) override;
    void onEvent(Fern::Event *event) override;
    void onDetach() override;

private:
    void setupPersistentDataPath();
    bool loadStaticScripts();
    void syncViewportWithWindow();
    // Postprocessing chain of the main camera: with an active chain the scene is rendered into an FBO,
    // the last pass — straight to the screen (POST_PROCESSING_OUTPUT_VIEW).
    void updatePostprocessing();
    void ensureSceneFramebuffer(uint32_t width, uint32_t height);
    void requestWorldLoad(AssetId worldAssetId);
    void loadPendingWorldRequest();

    path_t m_runtimeDataPath;
    AssetHandlerRuntime *m_assetHandler;
    ScriptEngine *m_scriptEngine;
    World *m_world;
    RuntimeProjectLoader *m_projectLoader;
    PandaUIImpl *m_pandaUI;
    AssetId m_pendingWorldLoad;
    Size m_viewportSize;
    Size m_viewportDpi;

    std::unique_ptr<PostprocessingPipeline> m_postprocessing;
    Miren::FrameBufferHandle m_sceneFB;
    Miren::FrameBufferSpecification m_sceneFbSpec;
    Miren::TextureHandle m_sceneColor;
    Miren::TextureHandle m_sceneEmissive;
    Miren::TextureHandle m_sceneDepth;
    bool m_sceneFramebufferActive = false; // SCENE_VIEW is currently bound to the FBO (the chain is active)
    uint32_t m_scenePixelWidth = 0;
    uint32_t m_scenePixelHeight = 0;
};

} // namespace Panda

#pragma once

#include "Assets/AssetHandlerRuntime.hpp"
#include "ProjectLoader/RuntimeProjectLoader.hpp"

#include <Panda/Application/Layer.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/WorldView.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>

#include <memory>

namespace Panda {

class GameUI;

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
    // The frame of the view with the look of the main camera of the world, the last pass straight
    // to the screen.
    void developFrame();
    void requestWorldLoad(AssetId worldAssetId);
    void loadPendingWorldRequest();

    path_t m_runtimeDataPath;
    AssetHandlerRuntime *m_assetHandler;
    ScriptEngine *m_scriptEngine;
    World *m_world;
    // The one view of the game: its world through its main camera, at the size of the window. It
    // lives from onAttach to onDetach, as the world does: what it holds is gone before the assets.
    std::unique_ptr<WorldView> m_view;
    RuntimeProjectLoader *m_projectLoader;
    GameUI *m_gameUI;
    AssetId m_pendingWorldLoad;
    Size m_viewportSize;
    Size m_viewportDpi;
};

} // namespace Panda

#pragma once

#include "Assets/AssetHandlerRuntime.hpp"
#include "ProjectLoader/RuntimeProjectLoader.hpp"

#include <Panda/Application/Layer.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Renderer/WorldView.hpp>
#include <Panda/Runtime/CheckRun.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>

#include <chrono>
#include <memory>
#include <vector>

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

    // What the process answers with when the game has left: 0, or the verdict of a check run.
    static int exitCode();

private:
    void setupPersistentDataPath();
    bool loadStaticScripts();
    void syncViewportWithWindow();
    // The frame of the view with the look of the main camera of the world, the last pass straight
    // into `output` — the screen.
    void developFrame(const Miren::ViewDescription &output);
    // A check run (Panda/Runtime/CheckRun.hpp): the frames are counted, the last one is also drawn
    // into a texture and written to the file that was asked for, then the game leaves.
    void beginCheckRun();
    bool isCheckShotFrame() const;
    void ensureCheckShotTarget();
    void updateCheckRun();
    void finishCheckRun();
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

    struct Check {
        uint32_t frames = 0; // how many frames to play; 0 — an ordinary run
        path_t shotFile;     // where the picture of the last frame goes; empty — nowhere
        uint32_t frame = 0;  // frames played
        std::chrono::steady_clock::time_point started;
        Miren::TextureHandle shot; // the last frame once more, as the window shows it
        Miren::FrameBufferHandle shotBuffer;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
        uint32_t readyFrame = 0; // the Miren frame the pixels are read by
        bool reading = false;
    };
    Check m_check;
    bool m_loaded = false; // the data of the game was read
};

} // namespace Panda

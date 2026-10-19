//
// Created by Admin on 22.01.2022.
//

#pragma once

#include "Panda/Application/LayerStack.hpp"

#include <PandaAsync/Scheduler.hpp>

namespace Fern {
class Window;
}

namespace Panda {

class Application {
public:
    ~Application();
    Application();
    void loop();
    bool tick(double deltaTime);
    void setMainWindow(Fern::Window *window);
    void pushLayer(Layer *layer);
    void pushOverlay(Layer *layer);
    void processFernEvents();
    Fern::Window *getMainWindow();
    void close();
    bool shouldClose() const;
    float getTime();
    static Application *get();

    // Engine-wide background job scheduler: asset decode runs on workers; GPU/registry work runs on
    // the main thread via completions drained each frame in tick().
    PandaAsync::Scheduler &getScheduler() {
        return m_scheduler;
    }

    int fps;

private:
    bool m_isApplicationShouldClose;
    uint64_t m_timeMillis;
    uint64_t m_startTimeMillis;
    // Auxiliary value for the game
    double m_gameTime;
    // Timer up to 1 second for FPS counting (in milliseconds)
    double m_oneSecondTimeCount;
    // Time since the previous frame was rendered
    uint64_t m_deltaTimeMillis;
    int m_thisSecondFramesCount;
    // FPS limit
    int m_maximumFps;
    LayerStack *m_layerStack;
    Fern::Window *m_mainWindow;
    PandaAsync::Scheduler m_scheduler;

    static Application *s_instance;
};

} // namespace Panda

#pragma once

#include "Panda/Application/LayerStack.hpp"

#include <PandaAsync/Scheduler.hpp>

#include <cstdint>

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

    // The pace of the loop. By default a frame every 1/maximumFps at most, as the display holds it:
    // a game, and an application someone looks at. One with nothing going on that nobody looks
    // at (the editor behind another application) asks for rest: the next frame comes after
    // `seconds`, or at once when an event of a window comes first (Fern::waitEvents). 0 — no rest.
    void setRestInterval(double seconds);
    // The background jobs handed results to the main thread in the last frame (decoded assets,
    // downloads): work is going on.
    bool deliveredCompletions() const {
        return m_deliveredCompletions;
    }

    // Engine-wide background job scheduler: asset decode runs on workers; GPU/registry work runs on
    // the main thread via completions drained each frame in tick().
    PandaAsync::Scheduler &getScheduler() {
        return m_scheduler;
    }

    int fps;

private:
    bool m_isApplicationShouldClose;
    // Seconds of frames since the start: the time of the shaders (getTime)
    double m_gameTime;
    // Timer up to 1 second for FPS counting (in seconds)
    double m_oneSecondTimeCount;
    int m_thisSecondFramesCount;
    // FPS limit
    int m_maximumFps;
    double m_restInterval = 0.0; // setRestInterval
    bool m_deliveredCompletions = false;
    LayerStack *m_layerStack;
    Fern::Window *m_mainWindow;
    PandaAsync::Scheduler m_scheduler;

    static Application *s_instance;
};

} // namespace Panda

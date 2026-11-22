#include "Panda/Application/Application.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/Input.hpp"
#include "Panda/Random/Random.hpp"
#include "Panda/UI/GameUI.hpp"

#include <Foundation/Profile.hpp>
#include <Miren/Miren.hpp>
#include <Fern/Fern.hpp>
#include <chrono>
#include <thread>

namespace Panda {

Application *Application::s_instance = nullptr;

namespace {

    bool isCloseRequest(Fern::Event *event) {
        return event->type == Fern::EventType::WindowCloseRequest || event->type == Fern::EventType::QuitRequest;
    }

    bool belongsToWindow(Fern::Event *event, Fern::Window *window) {
        return event->getWindow() == nullptr || event->getWindow() == window;
    }

    // Worker pool sized to the machine, leaving headroom for the main thread and Miren's render thread.
    PandaAsync::SchedulerDesc makeSchedulerDesc() {
        const unsigned hardware = std::thread::hardware_concurrency();
        const uint32_t workers = hardware > 4 ? hardware - 2 : 2;
        return PandaAsync::SchedulerDesc{workers, "panda"};
    }

} // namespace

Application *Application::get() {
    return s_instance;
}

Application::~Application() {
    LOG_INFO("Application: shutdown begins");
    F_DELETE(Foundation::getAllocator(), m_layerStack);
    // The application systems of the contributions stop after the layers and their worlds are
    // gone: a world that stops on the way out may still talk to them.
    GameContext::getExtensions().appSystems.setLive(false);
    s_instance = nullptr;
    LOG_INFO("Application: shutdown is complete");
}

void Application::pushLayer(Layer *layer) {
    m_layerStack->pushLayer(layer);
    layer->onAttach();
}

void Application::pushOverlay(Layer *layer) {
    m_layerStack->pushOverlay(layer);
    layer->onAttach();
}

Application::Application()
    : fps(0)
    , m_isApplicationShouldClose(false)
    , m_gameTime(0)
    , m_oneSecondTimeCount(0)
    , m_thisSecondFramesCount(0)
    , m_maximumFps(120)
    , m_layerStack(nullptr)
    , m_mainWindow(nullptr)
    , m_scheduler(makeSchedulerDesc()) {
    s_instance = this;
    PANDA_PROFILE_THREAD("Main Thread");
    m_scheduler.registerMainThread(); // ctor runs on the main thread; completions drain here
    m_layerStack = F_NEW(Foundation::getAllocator(), LayerStack);
    Random::init();
    // The application exists: the application systems registered so far start, the ones that
    // modules register later start at registration.
    GameContext::getExtensions().appSystems.setLive(true);
}

void Application::setMainWindow(Fern::Window *window) {
    m_mainWindow = window;
}

Fern::Window *Application::getMainWindow() {
    return m_mainWindow;
}

void Application::loop() {
    // The clock is the steady one: the wall clock jumps when the time of the system is set.
    using Clock = std::chrono::steady_clock;
    Clock::time_point last = Clock::now();
    while (!m_isApplicationShouldClose) {
        if (m_restInterval > 0.0) {
            // At rest: the next frame after the interval, or at once when an event of a window
            // comes first — a click that brings the application back is answered in that frame.
            const Clock::time_point due =
                last + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(m_restInterval));
            while (!m_isApplicationShouldClose && !Fern::getEventQueue()->hasEvents()) {
                const double left = std::chrono::duration<double>(due - Clock::now()).count();
                if (left <= 0.0) { break; }
                Fern::waitEvents(left);
            }
        } else {
            // A frame every 1/maximumFps at most. The thread sleeps till a moment before it is due
            // (it used to spin on the clock — a core busy for nothing where the display does not
            // hold the frame), and the display's synchronization in Miren::frame keeps the pace: a
            // sleep to the exact moment loses frames to its jitter.
            const Clock::time_point wake =
                last + std::chrono::microseconds(1000000 / m_maximumFps) - std::chrono::microseconds(1500);
            if (Clock::now() < wake) { std::this_thread::sleep_until(wake); }
        }
        const Clock::time_point now = Clock::now();
        const double deltaTime = std::chrono::duration<double>(now - last).count();
        last = now;
        tick(deltaTime);
    }
}

void Application::setRestInterval(double seconds) {
    m_restInterval = seconds > 0.0 ? seconds : 0.0;
}

bool Application::tick(double deltaTime) {
    PANDA_PROFILE_SCOPE_N("Panda::Application::tick");
    if (m_isApplicationShouldClose) { return false; }
    if (deltaTime <= 0.0) { deltaTime = 0.00000001; }

    m_gameTime += deltaTime;
    m_oneSecondTimeCount += deltaTime;
    m_thisSecondFramesCount++;
    if (m_oneSecondTimeCount >= 1.0) {
        fps = m_thisSecondFramesCount;
        m_thisSecondFramesCount = 0;
        m_oneSecondTimeCount = 0.0;
    }

    Input::nextFrame();
    {
        PANDA_PROFILE_SCOPE_N("Fern::pollEvents");
        Fern::pollEvents();
        processFernEvents();
    }
    LayerStack &layerStack = *m_layerStack;
    {
        PANDA_PROFILE_SCOPE_N("Layers onUpdate");
        for (Layer *layer : layerStack) {
            layer->onUpdate(deltaTime);
        }
    }
    {
        PANDA_PROFILE_SCOPE_N("App systems tick");
        GameContext::getExtensions().appSystems.tick(deltaTime); // the process-wide services of the contributions
    }
    {
        PANDA_PROFILE_SCOPE_N("Scheduler completions");
        // Main-thread completions of background jobs; any — work is going on (deliveredCompletions).
        m_deliveredCompletions = m_scheduler.processCompletions() > 0;
    }
    {
        PANDA_PROFILE_SCOPE_N("Miren::frame (main)");
        Miren::frame();
    }
    GameUI::flushPendingWindowDisposals();
    PANDA_PROFILE_FRAME();
    return !m_isApplicationShouldClose;
}

void Application::processFernEvents() {
    PANDA_PROFILE_SCOPE_N("Panda::Application::processFernEvents");
    Fern::EventQueue *eventQueue = Fern::getEventQueue();
    Fern::Event *event;
    while ((event = eventQueue->poll()) != nullptr) {
        for (auto it = m_layerStack->rbegin(); it != m_layerStack->rend(); ++it) {
            if (event->isHandled) break;
            (*it)->onEvent(event);
        }
        if (!event->isHandled && isCloseRequest(event) && belongsToWindow(event, m_mainWindow)) {
            event->isHandled = true;
            close();
            continue;
        }
        if (!event->isHandled && belongsToWindow(event, m_mainWindow)) {
            Input::onEvent(event);
        }
    }
    eventQueue->reset();
}

void Application::close() {
    m_isApplicationShouldClose = true;
}

bool Application::shouldClose() const {
    return m_isApplicationShouldClose;
}

float Application::getTime() {
    return static_cast<float>(m_gameTime);
}

} // namespace Panda

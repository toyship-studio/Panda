//
// Created by Admin on 22.01.2022.
//

#include "Panda/Application/Application.hpp"
#include "Panda/GameLogic/Input.hpp"
#include "Panda/Random/Random.hpp"
#include "Panda/UI/PandaUIImpl.hpp"

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

uint64_t getMillis() {
    auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    return now.count();
}

Application::~Application() {
    LOG_INFO("APP SHUTDOWN BEGIN");
    F_DELETE(Foundation::getAllocator(), m_layerStack);
    s_instance = nullptr;
    LOG_INFO("APP SHUTDOWN END");
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
    , m_timeMillis(0)
    , m_startTimeMillis(0)
    , m_gameTime(0)
    , m_oneSecondTimeCount(0)
    , m_deltaTimeMillis(0)
    , m_thisSecondFramesCount(0)
    , m_maximumFps(120)
    , m_layerStack(nullptr)
    , m_mainWindow(nullptr)
    , m_scheduler(makeSchedulerDesc()) {
    s_instance = this;
    PANDA_PROFILE_THREAD("Main Thread");
    m_scheduler.registerMainThread(); // ctor runs on the main thread; completions drain here
    m_timeMillis = getMillis();
    m_startTimeMillis = m_timeMillis;
    m_layerStack = F_NEW(Foundation::getAllocator(), LayerStack);
    Random::init();
}

void Application::setMainWindow(Fern::Window *window) {
    m_mainWindow = window;
}

Fern::Window *Application::getMainWindow() {
    return m_mainWindow;
}

void Application::loop() {
    while (!m_isApplicationShouldClose) {
        uint64_t lastTime = m_timeMillis;
        m_timeMillis = getMillis();
        m_deltaTimeMillis += m_timeMillis - lastTime;
        if (m_deltaTimeMillis < (1000 / m_maximumFps)) { continue; }
        double deltaTime = m_deltaTimeMillis / 1000.0;
        m_deltaTimeMillis = 0;
        tick(deltaTime);
    }
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
        PANDA_PROFILE_SCOPE_N("Scheduler completions");
        m_scheduler.processCompletions(); // run main-thread completions of background jobs
    }
    {
        PANDA_PROFILE_SCOPE_N("Miren::frame (main)");
        Miren::frame();
    }
    PandaUIImpl::flushPendingWindowDisposals();
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

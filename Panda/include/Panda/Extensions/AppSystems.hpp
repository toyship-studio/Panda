#pragma once

#include <string>
#include <vector>

namespace Panda {

// A system of the APPLICATION as a contribution to the engine registry (context/plugins.md): a
// service that lives for the whole process and not for one world — an audio device and its mixer,
// the state of a network layer. World systems (WorldSystems.hpp) see only the frames of a running
// world; an application system sees every frame of the application, whether a world runs or not,
// and the moments the application appears and goes away. Per-world state still lives in
// World::context<T>; what an application system owns is what has to survive a world switch and
// the editor's Play copy.
struct AppSystem final {
    std::string name;
    // The application exists (Application::get() is valid, the scheduler runs): open devices,
    // allocate pools. Called at registration when the application is already live — the editor
    // registers the modules of a project after it started.
    void (*start)() = nullptr;
    // Every frame, after the layers have updated (the worlds of the frame are final), before the
    // completions of the scheduler run; deltaTime is the real time of the frame in seconds.
    void (*tick)(double deltaTime) = nullptr;
    // The application goes away, after its layers (and their worlds) are destroyed: close devices.
    // Not called where the process is killed without a teardown (iOS).
    void (*stop)() = nullptr;
};

class AppSystemRegistry final {
public:
    // Registration order is the tick order; a live application starts the system at once.
    void add(AppSystem system);
    const std::vector<AppSystem> &systems() const {
        return m_systems;
    }
    // Driven by Application: live on construction (starts the registered systems, in order), not
    // live on destruction (stops them, in reverse order).
    void setLive(bool live);
    bool isLive() const {
        return m_live;
    }
    void tick(double deltaTime);

private:
    std::vector<AppSystem> m_systems;
    bool m_live = false;
};

} // namespace Panda

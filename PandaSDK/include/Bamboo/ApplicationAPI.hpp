#pragma once

#include <cstdint>
#include <string>

namespace Bamboo {

namespace ApplicationAPI {
    uint32_t getWidth();
    uint32_t getHeight();
    // Seconds since the application started, in real time: it does not restart with a world
    // and moves once a frame. Game logic counts its own time from the deltaTime of update or
    // the step of fixedUpdate.
    float getTime();
    bool isCursorLocked();
    void setCursorLocked(bool locked);
    void quit();
    /// Writable per-game directory for save files (cross-platform). Empty if the host
    /// did not provide one.
    std::string getPersistentDataPath();
}; // namespace ApplicationAPI

} // namespace Bamboo

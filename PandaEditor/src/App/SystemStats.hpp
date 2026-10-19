#pragma once

#include <cstdint>

namespace Editor {

// Process metrics for the status indicators of the visibility strip. Cheap (2 syscalls),
// but poll them once per ~half second, not every frame.
struct SystemStats {
    uint64_t residentBytes = 0; // process RAM (resident set)
    double cpuPercent = 0.0;    // CPU load by the process, % of one core (like top)
};

SystemStats sampleSystemStats();

} // namespace Editor

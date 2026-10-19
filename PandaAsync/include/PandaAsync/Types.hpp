#pragma once

#include <cstdint>

namespace PandaAsync {

using JobId = uint64_t;
using GroupId = uint64_t;

enum class Priority : uint8_t {
    Critical,
    High,
    Normal,
    Low,
    Background,
};

struct SchedulerDesc {
    // A scheduler runs a small worker pool sized to its purpose (thumbnails, chunks),
    // not to the machine. Several schedulers coexist (editor, script module), so each
    // takes only as many workers as its own queue needs. 0 is clamped to 1.
    uint32_t workerCount = 2;
    const char *name = nullptr;
};

struct JobDesc {
    const char *name = nullptr;
    Priority priority = Priority::Normal;
    GroupId group = 0; // 0 means no group.
};

struct CompletionBudget {
    uint32_t maxCount = 32;
    double maxMilliseconds = 1.0;
};

// Internal per-job state, shared between the queued job, its handle and its
// queued completion. Defined in the implementation.
struct JobState;

} // namespace PandaAsync

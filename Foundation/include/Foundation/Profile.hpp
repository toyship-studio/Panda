#pragma once

#include <cstddef>
#include <cstdint>

// Built-in profiler (the Profiler panel in PandaEditor): RAII scopes write into
// per-thread aggregates (the scope name is a string literal, the key is the pointer),
// PANDA_PROFILE_FRAME on the main thread collects a frame snapshot for the panel.
// The scope overhead is two clock::now() calls and a map update under an uncontended mutex.

namespace Foundation {
namespace Profiler {

    struct Entry {
        const char *name;
        const char *thread;
        uint32_t calls; // 0 — this is a plot value, totalMs holds the value
        float totalMs;
        float maxMs;
    };

    uint64_t nowNs();
    void registerThread(const char *name);
    void recordScope(const char *name, uint64_t startNs, uint64_t endNs);
    void plot(const char *name, int64_t value);
    void frameMark();
    // Snapshot of the last frame (sorted by totalMs descending); returns the number of entries.
    size_t snapshot(Entry *out, size_t maxCount);
    float lastFrameMs();

} // namespace Profiler

struct ProfileScopeGuard {
    const char *name;
    uint64_t startNs;

    explicit ProfileScopeGuard(const char *scopeName)
        : name(scopeName)
        , startNs(Profiler::nowNs()) {}

    ~ProfileScopeGuard() {
        Profiler::recordScope(name, startNs, Profiler::nowNs());
    }
};

} // namespace Foundation

#define PANDA_PROFILE_CONCAT2(a, b) a##b
#define PANDA_PROFILE_CONCAT(a, b) PANDA_PROFILE_CONCAT2(a, b)
#define PANDA_PROFILE_SCOPE() \
    Foundation::ProfileScopeGuard PANDA_PROFILE_CONCAT(_profileScope, __LINE__)(__func__)
#define PANDA_PROFILE_SCOPE_N(name) \
    Foundation::ProfileScopeGuard PANDA_PROFILE_CONCAT(_profileScope, __LINE__)(name)
#define PANDA_PROFILE_FRAME() Foundation::Profiler::frameMark()
#define PANDA_PROFILE_THREAD(name) Foundation::Profiler::registerThread(name)
#define PANDA_PROFILE_PLOT(name, value) Foundation::Profiler::plot(name, value)

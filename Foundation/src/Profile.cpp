#include "Foundation/Profile.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Foundation {
namespace Profiler {

    namespace {

        struct Accumulator {
            uint32_t calls = 0;
            uint64_t totalNs = 0;
            uint64_t maxNs = 0;
        };

        struct ThreadState {
            std::mutex mutex;
            const char *name = "Thread";
            std::unordered_map<const char *, Accumulator> scopes;
            std::unordered_map<const char *, int64_t> plots;
        };

        std::mutex s_registryMutex;
        std::vector<ThreadState *> s_threads;

        std::mutex s_snapshotMutex;
        std::vector<Entry> s_snapshot;
        uint64_t s_frameStartNs = 0;
        float s_lastFrameMs = 0.f;

        ThreadState &threadState() {
            thread_local ThreadState *state = [] {
                ThreadState *created = new ThreadState();
                std::lock_guard<std::mutex> lock(s_registryMutex);
                s_threads.push_back(created);
                return created;
            }();
            return *state;
        }

    } // namespace

    uint64_t nowNs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }

    void registerThread(const char *name) {
        threadState().name = name;
    }

    void recordScope(const char *name, uint64_t startNs, uint64_t endNs) {
        ThreadState &state = threadState();
        const uint64_t duration = endNs > startNs ? endNs - startNs : 0;
        std::lock_guard<std::mutex> lock(state.mutex);
        Accumulator &acc = state.scopes[name];
        acc.calls++;
        acc.totalNs += duration;
        acc.maxNs = std::max(acc.maxNs, duration);
    }

    void plot(const char *name, int64_t value) {
        ThreadState &state = threadState();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.plots[name] = value;
    }

    void frameMark() {
        std::vector<Entry> entries;
        {
            std::lock_guard<std::mutex> registryLock(s_registryMutex);
            for (ThreadState *state : s_threads) {
                std::lock_guard<std::mutex> lock(state->mutex);
                for (const auto &[name, acc] : state->scopes) {
                    entries.push_back(
                        {name, state->name, acc.calls, static_cast<float>(acc.totalNs) / 1e6f,
                         static_cast<float>(acc.maxNs) / 1e6f}
                    );
                }
                state->scopes.clear();
                // Plots do not accumulate across frames — show the last value.
                for (const auto &[name, value] : state->plots) {
                    entries.push_back({name, state->name, 0, static_cast<float>(value), 0.f});
                }
            }
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return a.totalMs > b.totalMs;
        });
        const uint64_t now = nowNs();
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        s_snapshot = std::move(entries);
        if (s_frameStartNs != 0) {
            s_lastFrameMs = static_cast<float>(now - s_frameStartNs) / 1e6f;
        }
        s_frameStartNs = now;
    }

    size_t snapshot(Entry *out, size_t maxCount) {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        const size_t count = std::min(maxCount, s_snapshot.size());
        for (size_t i = 0; i < count; i++) {
            out[i] = s_snapshot[i];
        }
        return count;
    }

    float lastFrameMs() {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        return s_lastFrameMs;
    }

} // namespace Profiler
} // namespace Foundation

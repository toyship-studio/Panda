#include "Bamboo/Internal/MainThread.hpp"

#include <PandaAsync/PandaAsync.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace Bamboo::Internal {

namespace {
// Written once on the main thread at plugin load, before any worker exists. The
// atomic flag publishes that write to later reads on other threads.
std::thread::id s_mainThreadId;
std::atomic<bool> s_hasMainThread{false};
} // namespace

void registerMainThread() {
    s_mainThreadId = std::this_thread::get_id();
    s_hasMainThread.store(true, std::memory_order_release);
}

bool isMainThread() {
    return s_hasMainThread.load(std::memory_order_acquire) &&
           std::this_thread::get_id() == s_mainThreadId;
}

bool requireMainThreadApi(const char *apiName) {
    if (isMainThread()) { return true; }

    if (PandaAsync::isWorkerThread()) {
        const char *job = PandaAsync::currentJobName();
        std::fprintf(stderr,
            "[Bamboo] main-thread-only API '%s' was called from async job '%s'. "
            "Apply the result in the job's complete() callback instead.\n",
            apiName, job != nullptr ? job : "?");
    } else {
        std::fprintf(stderr,
            "[Bamboo] main-thread-only API '%s' was called from a non-main thread. "
            "Only the main thread may call it.\n",
            apiName);
    }

#ifndef NDEBUG
    std::fflush(stderr);
    std::abort();
#endif
    return false;
}

} // namespace Bamboo::Internal

#include "Bamboo/Async.hpp"
#include "Bamboo/Internal/MainThread.hpp"

namespace Bamboo::Async {

namespace {
    // The pool is sized for the module's queue, not for the number of cores: the engine and every
    // script module hold their own isolated pools (a shared one through the ABI is impossible
    // without losing safe unloading), so the total number of threads must remain deliberate.
    constexpr uint32_t WORKER_COUNT = 2;

    PandaAsync::Scheduler *s_scheduler = nullptr;
} // namespace

namespace Internal {

    bool hasScheduler() {
        return s_scheduler != nullptr;
    }

    PandaAsync::Scheduler &scheduler() {
        if (s_scheduler == nullptr) {
            s_scheduler = new PandaAsync::Scheduler(PandaAsync::SchedulerDesc{WORKER_COUNT, "BambooScript"});
            // Workers are created lazily, only if a script actually uses Async.
            // processCompletions is allowed only on the main thread, so that thread is registered
            // if the scheduler is created from it (the usual case: run() from start/update).
            if (::Bamboo::Internal::isMainThread()) { s_scheduler->registerMainThread(); }
        }
        return *s_scheduler;
    }

    void update(uint32_t maxCompletions, double maxMilliseconds) {
        if (s_scheduler == nullptr) { return; }
        PandaAsync::CompletionBudget budget;
        budget.maxCount = maxCompletions;
        budget.maxMilliseconds = maxMilliseconds;
        s_scheduler->processCompletions(budget);
    }

    void cancelAllJobs() {
        if (s_scheduler == nullptr) { return; }
        s_scheduler->cancelAll();
    }

    void waitAllJobs() {
        if (s_scheduler == nullptr) { return; }
        // Destroying the scheduler stops and joins the workers and discards the pending
        // completions: after that the module's code is guaranteed not to be executed by
        // anyone, and the engine may destroy script instances and unload the library.
        // A work that does not check Context::isCanceled() will delay this moment until
        // it returns — so checking for cancellation is mandatory.
        delete s_scheduler;
        s_scheduler = nullptr;
    }

} // namespace Internal

void cancelAll() {
    Internal::cancelAllJobs();
}

bool isWorkerThread() {
    return PandaAsync::isWorkerThread();
}

} // namespace Bamboo::Async

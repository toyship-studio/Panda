#pragma once

#include <PandaAsync/Context.hpp>
#include <PandaAsync/JobHandle.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace PandaAsync {

class Scheduler {
public:
    explicit Scheduler(SchedulerDesc desc = {});
    ~Scheduler();

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

    // Records the calling thread as the main thread. processCompletions() must run
    // on it. May be called once at startup.
    void registerMainThread();
    bool isMainThread() const;

    GroupId createGroup();

    // Typed job: work produces a TResult on a worker, complete consumes it on the
    // main thread. Requires an explicit template argument, e.g. submit<Mesh>(...).
    // TResult only needs to be movable; it is held in a shared_ptr so the completion
    // closure stays copyable for the type-erased queue (no std::move_only_function
    // until C++23).
    template<typename TResult>
    JobHandle submit(JobDesc desc,
                     std::function<TResult(Context &)> work,
                     std::function<void(TResult &&)> complete) {
        ErasedWork run = [work = std::move(work), complete = std::move(complete)](
                             Context &ctx) mutable -> CompletionFn {
            if (ctx.isCanceled()) { return {}; }
            auto result = std::make_shared<TResult>(work(ctx));
            if (ctx.isCanceled()) { return {}; }
            return [result, complete = std::move(complete)]() mutable {
                complete(std::move(*result));
            };
        };
        return submitErased(desc, std::move(run));
    }

    // Void job: work runs on a worker, optional complete runs on the main thread.
    JobHandle submit(JobDesc desc,
                     std::function<void(Context &)> work,
                     std::function<void()> complete = {});

    void cancel(JobId id);
    void cancelGroup(GroupId group);
    void cancelAll();
    bool isCanceled(JobId id) const;

    // Runs queued completion entries on the main thread, bounded by budget. Canceled
    // entries are dropped and still consume budget.
    uint32_t processCompletions(CompletionBudget budget = {});

    // Waits until all submitted work has finished on workers. Does not run
    // completions. Cancel first if pending work should be skipped rather than run.
    void waitAll();

    // Cancels all jobs, drops pending work/completions, stops and joins workers.
    // Idempotent; also called by the destructor.
    void shutdown();

private:
    using CompletionFn = std::function<void()>;
    using ErasedWork = std::function<CompletionFn(Context &)>;

    JobHandle submitErased(JobDesc desc, ErasedWork run);
    void runWorker();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Convenience global scheduler for engine/editor code. Not suitable as the base
// for scripting reload/unload, which must own its own Scheduler.
Scheduler &defaultScheduler();

// Execution-context helpers for the calling thread.
bool isWorkerThread();
const char *currentJobName();

} // namespace PandaAsync

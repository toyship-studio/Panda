#pragma once

// Background jobs of a script.
//
// A job has two phases:
//   work     — runs on a worker: pure computation, data parsing, geometry
//              generation. The engine API (World, Entity, components, assets, meshes,
//              textures, UI) must NOT be called from here — it is main-thread-only and the
//              call will be caught by the Bamboo::Internal::requireMainThreadApi check.
//   complete — runs on the main thread between frames: here the result is applied
//              to the world.
//
// Only copies/plain data may be passed into work. Pointers to objects that the main thread
// may delete while the job is running must not be captured.
//
// Cancellation is cooperative: work must periodically check Context::isCanceled() and
// return. A canceled job never runs complete.
//
// The scheduler belongs to the script module; the engine itself applies completions once per
// frame and stops the workers when play mode stops and before the module is unloaded. Therefore
// the user does not need to (and must not) call processCompletions/waitAll.
//
// Example:
//
//     Bamboo::Async::run(
//         [](Bamboo::Async::Context &ctx) { return generateMesh(); }, // worker
//         [this](MeshData &&data) { MeshAPI::update(m_mesh, data); }  // main thread
//     );

#include <PandaAsync/PandaAsync.hpp>

#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace Bamboo::Async {

using Priority = PandaAsync::Priority;
using GroupId = PandaAsync::GroupId;
// Passed into work: job identification and the cancellation check.
using Context = PandaAsync::Context;
// A reference to a submitted job: cancel() / isCanceled().
using JobHandle = PandaAsync::JobHandle;

struct JobDesc {
    const char *name = nullptr;
    Priority priority = Priority::Normal;
    GroupId group = 0;
};

namespace Internal {
    // The module's scheduler. The public API intentionally allows neither creating one's own nor
    // applying completions manually: this is controlled by the engine through the scripting ABI.
    PandaAsync::Scheduler &scheduler();
    bool hasScheduler();

    inline PandaAsync::JobDesc toSchedulerDesc(const JobDesc &desc) {
        PandaAsync::JobDesc result;
        result.name = desc.name;
        result.priority = desc.priority;
        result.group = desc.group;
        return result;
    }

    // Called by the engine through the ABI, not from user code.
    void update(uint32_t maxCompletions, double maxMilliseconds);
    void cancelAllJobs();
    void waitAllJobs();
} // namespace Internal

// A group of jobs for bulk cancellation. Cancels its jobs on destruction, so it is
// convenient to hold a group as a script field: the jobs will not outlive the owner.
class Group final {
public:
    Group()
        : m_id(Internal::scheduler().createGroup()) {}

    ~Group() {
        cancel();
    }

    Group(const Group &) = delete;
    Group &operator=(const Group &) = delete;

    GroupId id() const {
        return m_id;
    }

    void cancel() {
        if (Internal::hasScheduler()) { Internal::scheduler().cancelGroup(m_id); }
    }

private:
    GroupId m_id;
};

// work + complete. The result type is deduced from work: if work returns a value,
// it is passed into complete on the main thread; if void — complete is called without
// arguments.
template<typename TWork, typename TComplete>
    requires std::invocable<TWork, Context &>
JobHandle run(JobDesc desc, TWork work, TComplete complete) {
    using TResult = std::invoke_result_t<TWork, Context &>;
    if constexpr (std::is_void_v<TResult>) {
        return Internal::scheduler().submit(
            Internal::toSchedulerDesc(desc),
            std::function<void(Context &)>(std::move(work)),
            std::function<void()>(std::move(complete))
        );
    } else {
        return Internal::scheduler().submit<TResult>(
            Internal::toSchedulerDesc(desc),
            std::function<TResult(Context &)>(std::move(work)),
            std::function<void(TResult &&)>(std::move(complete))
        );
    }
}

template<typename TWork, typename TComplete>
    requires std::invocable<TWork, Context &>
JobHandle run(TWork work, TComplete complete) {
    return run(JobDesc{}, std::move(work), std::move(complete));
}

// Only work, without applying the result on the main thread.
template<typename TWork>
    requires std::invocable<TWork, Context &>
JobHandle run(JobDesc desc, TWork work) {
    static_assert(
        std::is_void_v<std::invoke_result_t<TWork, Context &>>,
        "run() without complete must return void: there is nowhere to apply the result"
    );
    return Internal::scheduler().submit(
        Internal::toSchedulerDesc(desc), std::function<void(Context &)>(std::move(work))
    );
}

template<typename TWork>
    requires std::invocable<TWork, Context &>
JobHandle run(TWork work) {
    return run(JobDesc{}, std::move(work));
}

// Cancels all jobs of the module. A work that is already running will finish on its own, but
// its complete will not be called.
void cancelAll();

// true if the code is running on a worker (i.e. inside work).
bool isWorkerThread();

} // namespace Bamboo::Async

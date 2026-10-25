// Standalone smoke test for PandaAsync. Exercises work/complete phases,
// main-thread completion, cooperative cancellation and group cancellation.
// Returns non-zero on failure.

#include <PandaAsync/PandaAsync.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// Blocks one worker until released, so we can deterministically queue and cancel
// jobs behind it on a single-worker scheduler.
struct Blocker {
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};

    PandaAsync::JobHandle submit(PandaAsync::Scheduler &scheduler) {
        return scheduler.submit(PandaAsync::JobDesc{"blocker"}, [this](PandaAsync::Context &) {
            started.store(true);
            while (!release.load()) { std::this_thread::sleep_for(1ms); }
        });
    }

    void waitUntilRunning() {
        while (!started.load()) { std::this_thread::sleep_for(1ms); }
    }
};

void testWorkAndComplete() {
    std::printf("testWorkAndComplete\n");
    PandaAsync::Scheduler scheduler;
    scheduler.registerMainThread();

    constexpr int kJobs = 64;
    std::atomic<int> workerThreadViolations{0};
    std::vector<int> results;

    for (int i = 0; i < kJobs; ++i) {
        scheduler.submit<int>(
            PandaAsync::JobDesc{"square", PandaAsync::Priority::Normal},
            [i, &workerThreadViolations](PandaAsync::Context &) -> int {
                if (!PandaAsync::isWorkerThread()) { ++workerThreadViolations; }
                return i * i;
            },
            [&results, &scheduler](int &&value) {
                check(scheduler.isMainThread(), "complete must run on main thread");
                check(!PandaAsync::isWorkerThread(), "complete must not run on a worker");
                results.push_back(value);
            });
    }

    scheduler.waitAll();
    uint32_t ran = scheduler.processCompletions(PandaAsync::CompletionBudget{kJobs, 0.0});

    check(ran == kJobs, "all completions ran");
    check(results.size() == kJobs, "all results collected");
    check(workerThreadViolations.load() == 0, "work ran on worker threads");

    long sum = 0;
    for (int v : results) { sum += v; }
    long expected = 0;
    for (int i = 0; i < kJobs; ++i) { expected += i * i; }
    check(sum == expected, "results are correct");
}

struct MoveOnly {
    int value = 0;
    MoveOnly() = default;
    explicit MoveOnly(int v)
        : value(v) {}
    MoveOnly(const MoveOnly &) = delete;
    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&) = default;
    MoveOnly &operator=(MoveOnly &&) = default;
};

void testMoveOnlyResult() {
    std::printf("testMoveOnlyResult\n");
    PandaAsync::Scheduler scheduler;
    scheduler.registerMainThread();

    constexpr int kJobs = 16;
    int sum = 0;
    for (int i = 0; i < kJobs; ++i) {
        scheduler.submit<MoveOnly>(
            PandaAsync::JobDesc{"moveonly"},
            [i](PandaAsync::Context &) -> MoveOnly { return MoveOnly(i + 1); },
            [&sum](MoveOnly &&result) { sum += result.value; });
    }
    scheduler.waitAll();
    scheduler.processCompletions(PandaAsync::CompletionBudget{kJobs, 0.0});

    check(sum == kJobs * (kJobs + 1) / 2, "move-only results delivered to complete");
}

void testCompletionBudget() {
    std::printf("testCompletionBudget\n");
    PandaAsync::Scheduler scheduler;
    scheduler.registerMainThread();

    constexpr int kJobs = 50;
    for (int i = 0; i < kJobs; ++i) {
        scheduler.submit(
            PandaAsync::JobDesc{"noop"}, [](PandaAsync::Context &) {}, [] {});
    }
    scheduler.waitAll();

    uint32_t first = scheduler.processCompletions(PandaAsync::CompletionBudget{10, 0.0});
    check(first == 10, "budget caps completions per call");

    uint32_t rest = 0;
    while (scheduler.processCompletions(PandaAsync::CompletionBudget{10, 0.0}) > 0) {
        rest += 10;
    }
    check(first + rest >= kJobs, "remaining completions drain across calls");
}

void testCancelPending() {
    std::printf("testCancelPending\n");
    PandaAsync::Scheduler scheduler(PandaAsync::SchedulerDesc{1});
    scheduler.registerMainThread();

    Blocker blocker;
    blocker.submit(scheduler);
    blocker.waitUntilRunning();

    std::atomic<bool> workRan{false};
    std::atomic<bool> completeRan{false};
    PandaAsync::JobHandle handle = scheduler.submit(
        PandaAsync::JobDesc{"canceled"},
        [&workRan](PandaAsync::Context &) { workRan.store(true); },
        [&completeRan] { completeRan.store(true); });

    handle.cancel();
    check(handle.isCanceled(), "handle reports canceled");

    blocker.release.store(true);
    scheduler.waitAll();
    scheduler.processCompletions();

    check(!workRan.load(), "canceled pending job does not run work");
    check(!completeRan.load(), "canceled pending job does not run complete");
}

void testCancelCooperative() {
    std::printf("testCancelCooperative\n");
    PandaAsync::Scheduler scheduler(PandaAsync::SchedulerDesc{1});
    scheduler.registerMainThread();

    std::atomic<bool> started{false};
    std::atomic<bool> completeRan{false};
    PandaAsync::JobHandle handle = scheduler.submit(
        PandaAsync::JobDesc{"long"},
        [&started](PandaAsync::Context &ctx) {
            started.store(true);
            while (!ctx.isCanceled()) { std::this_thread::sleep_for(1ms); }
        },
        [&completeRan] { completeRan.store(true); });

    while (!started.load()) { std::this_thread::sleep_for(1ms); }
    handle.cancel();

    scheduler.waitAll();
    scheduler.processCompletions();

    check(!completeRan.load(), "cooperatively canceled job does not run complete");
}

void testCancelGroup() {
    std::printf("testCancelGroup\n");
    PandaAsync::Scheduler scheduler(PandaAsync::SchedulerDesc{1});
    scheduler.registerMainThread();

    Blocker blocker;
    blocker.submit(scheduler);
    blocker.waitUntilRunning();

    PandaAsync::GroupId group = scheduler.createGroup();
    std::atomic<int> workRan{0};
    std::atomic<int> completeRan{0};

    constexpr int kJobs = 8;
    for (int i = 0; i < kJobs; ++i) {
        scheduler.submit(
            PandaAsync::JobDesc{"grouped", PandaAsync::Priority::Normal, group},
            [&workRan](PandaAsync::Context &) { ++workRan; },
            [&completeRan] { ++completeRan; });
    }

    scheduler.cancelGroup(group);
    blocker.release.store(true);
    scheduler.waitAll();
    scheduler.processCompletions();

    check(workRan.load() == 0, "grouped jobs canceled before running work");
    check(completeRan.load() == 0, "grouped jobs do not run complete");
}

void testCanceledCompletionsRespectBudget() {
    std::printf("testCanceledCompletionsRespectBudget\n");
    PandaAsync::Scheduler scheduler(PandaAsync::SchedulerDesc{2});
    scheduler.registerMainThread();

    constexpr int kJobs = 8;
    std::vector<PandaAsync::JobHandle> handles;
    handles.reserve(kJobs);
    std::atomic<int> completeRan{0};

    for (int i = 0; i < kJobs; ++i) {
        handles.emplace_back(scheduler.submit(
            PandaAsync::JobDesc{"cancel-queued-complete"},
            [](PandaAsync::Context &) {},
            [&completeRan] { ++completeRan; }
        ));
    }

    scheduler.waitAll();
    for (auto &handle : handles) {
        handle.cancel();
    }

    uint32_t first = scheduler.processCompletions(PandaAsync::CompletionBudget{2, 0.0});
    uint32_t second = scheduler.processCompletions(PandaAsync::CompletionBudget{2, 0.0});
    uint32_t processed = first + second;
    while (uint32_t count = scheduler.processCompletions(PandaAsync::CompletionBudget{2, 0.0})) {
        processed += count;
    }

    check(first == 2, "canceled completions consume first budget");
    check(second == 2, "canceled completions consume second budget");
    check(processed == kJobs, "canceled completions drain across budgeted calls");
    check(completeRan.load() == 0, "canceled completions do not run callbacks");
}

void testShutdownCancelsRunningJob() {
    std::printf("testShutdownCancelsRunningJob\n");
    PandaAsync::Scheduler scheduler(PandaAsync::SchedulerDesc{1});
    scheduler.registerMainThread();

    std::atomic<bool> started{false};
    std::atomic<bool> sawCanceled{false};
    std::atomic<bool> completeRan{false};
    std::atomic<bool> forceRelease{false};
    std::atomic<bool> shutdownReturned{false};

    scheduler.submit(
        PandaAsync::JobDesc{"shutdown-cancel"},
        [&started, &sawCanceled, &forceRelease](PandaAsync::Context &ctx) {
            started.store(true);
            while (!ctx.isCanceled() && !forceRelease.load()) {
                std::this_thread::sleep_for(1ms);
            }
            if (ctx.isCanceled()) { sawCanceled.store(true); }
        },
        [&completeRan] { completeRan.store(true); }
    );

    while (!started.load()) { std::this_thread::sleep_for(1ms); }

    std::thread shutdownThread([&scheduler, &shutdownReturned] {
        scheduler.shutdown();
        shutdownReturned.store(true);
    });

    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!shutdownReturned.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }

    check(shutdownReturned.load(), "shutdown cancels running job and returns");
    if (!shutdownReturned.load()) { forceRelease.store(true); }
    shutdownThread.join();

    check(sawCanceled.load(), "running job observes cancellation during shutdown");
    check(scheduler.processCompletions(PandaAsync::CompletionBudget{10, 0.0}) == 0,
          "shutdown drops queued completions");
    check(!completeRan.load(), "shutdown does not run complete for canceled job");
}

} // namespace

void testPost() {
    std::printf("testPost\n");
    PandaAsync::Scheduler scheduler;
    scheduler.registerMainThread();
    int ran = 0;
    // Posted from another thread, run on the main thread; a canceled group drops its completion.
    std::thread other([&] { scheduler.post(PandaAsync::JobDesc{"posted"}, [&] { ++ran; }); });
    other.join();
    PandaAsync::GroupId group = scheduler.createGroup();
    scheduler.post(PandaAsync::JobDesc{"dropped", PandaAsync::Priority::Normal, group}, [&] { ran += 100; });
    scheduler.cancelGroup(group);
    scheduler.processCompletions(PandaAsync::CompletionBudget{16, 0.0});
    check(ran == 1, "a posted completion runs, a canceled one is dropped");
}

int main() {
    testWorkAndComplete();
    testMoveOnlyResult();
    testCompletionBudget();
    testCancelPending();
    testCancelCooperative();
    testCancelGroup();
    testCanceledCompletionsRespectBudget();
    testShutdownCancelsRunningJob();
    testPost();

    if (g_failures == 0) {
        std::printf("PandaAsync smoke: PASS\n");
        return 0;
    }
    std::printf("PandaAsync smoke: FAIL (%d)\n", g_failures);
    return 1;
}

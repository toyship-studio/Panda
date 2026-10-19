// Smoke test of the Bamboo::Async user API.
//
// Checks what script code relies on: result type deduction in run(), the work/complete
// phases on their own threads, move-only results, cooperative cancellation, groups and
// scheduler shutdown. Internal::update/cancelAllJobs/waitAllJobs are called directly here —
// in the engine they are invoked by the scripting ABI (async_Update/async_CancelAll/async_WaitAll).
#include <Bamboo/Async.hpp>
#include <Bamboo/Internal/MainThread.hpp>

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace Async = Bamboo::Async;
static int g_fail = 0;
static void check(bool ok, const char *what) {
    if (!ok) { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

struct MoveOnly {
    std::unique_ptr<int> v;
    explicit MoveOnly(int x) : v(std::make_unique<int>(x)) {}
};

// Pumping of engine frames: applies completions the way World::updateScripts does.
static void pumpFrames(int frames = 400) {
    for (int i = 0; i < frames; ++i) {
        Async::Internal::update(32, 2.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main() {
    Bamboo::Internal::registerMainThread();

    // 1. run(work, complete) with result type deduction — without an explicit <T>.
    std::vector<int> results;
    for (int i = 0; i < 20; ++i) {
        Async::run(
            [i](Async::Context &) { return i * 2; },
            [&results](int &&value) { results.push_back(value); }
        );
    }

    // 2. void-work + complete.
    std::atomic<int> voidWork{0};
    bool voidComplete = false;
    Async::run(
        [&voidWork](Async::Context &) { voidWork.fetch_add(1); },
        [&voidComplete] { voidComplete = true; }
    );

    // 3. run(work) — worker only, without applying on main.
    std::atomic<bool> workOnly{false};
    Async::run([&workOnly](Async::Context &) { workOnly.store(true); });

    // 4. JobDesc: name/priority + a move-only result.
    int moved = 0;
    Async::run(
        Async::JobDesc{"named", Async::Priority::High},
        [](Async::Context &) { return MoveOnly(41); },
        [&moved](MoveOnly &&m) { moved = *m.v + 1; }
    );

    // 5. work runs on a worker, and complete — on main.
    std::atomic<bool> workOnWorker{false};
    bool completeOnMain = false;
    Async::run(
        [&workOnWorker](Async::Context &) { workOnWorker.store(Async::isWorkerThread()); },
        [&completeOnMain] { completeOnMain = !Async::isWorkerThread(); }
    );

    pumpFrames();

    check(results.size() == 20, "all typed jobs are applied");
    long sum = 0; for (int v : results) sum += v;
    check(sum == 380, "the results are correct");
    check(voidWork.load() == 1 && voidComplete, "void work + complete");
    check(workOnly.load(), "run(work) without complete");
    check(moved == 42, "the move-only result reached complete");
    check(workOnWorker.load(), "work runs on a worker");
    check(completeOnMain, "complete runs on the main thread");

    // 6. Cooperative cancellation: complete must not be called.
    std::atomic<bool> started{false};
    bool canceledComplete = false;
    Async::JobHandle handle = Async::run(
        [&started](Async::Context &ctx) {
            started.store(true);
            while (!ctx.isCanceled()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        },
        [&canceledComplete] { canceledComplete = true; }
    );
    while (!started.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    handle.cancel();
    check(handle.isCanceled(), "JobHandle::isCanceled");
    pumpFrames(200);
    check(!canceledComplete, "a canceled job does not run complete");

    // 7. Group: bulk cancellation.
    int groupCompletes = 0;
    {
        Async::Group group;
        for (int i = 0; i < 8; ++i) {
            Async::run(
                Async::JobDesc{"grouped", Async::Priority::Low, group.id()},
                [](Async::Context &) { return 1; },
                [&groupCompletes](int &&) { groupCompletes++; }
            );
        }
        group.cancel();
    } // the group destructor cancels as well
    pumpFrames(200);
    check(groupCompletes == 0, "Group::cancel cancels its jobs");

    // 8. Shutdown: the workers are joined, pending completions are discarded.
    bool afterWait = false;
    Async::run([](Async::Context &) { return 7; }, [&afterWait](int &&) { afterWait = true; });
    Async::Internal::cancelAllJobs();
    Async::Internal::waitAllJobs();
    Async::Internal::update(32, 2.0);
    check(!afterWait, "waitAll discards pending completions");

    if (g_fail == 0) { std::printf("Bamboo::Async: PASS\n"); return 0; }
    std::printf("Bamboo::Async: FAIL (%d)\n", g_fail);
    return 1;
}

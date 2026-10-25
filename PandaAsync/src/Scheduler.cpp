#include <PandaAsync/Scheduler.hpp>

#include <atomic>
#if defined(__APPLE__) || defined(__linux__)
#    include <pthread.h>
#elif defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <cstdlib>
#    include <cstring>
#    include <malloc.h>
#endif
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace PandaAsync {

static constexpr int kPriorityCount = 5;

struct JobState {
    std::atomic<bool> canceled{false};
    GroupId group = 0;
};

namespace {

struct Job {
    JobId id = 0;
    GroupId group = 0;
    Priority priority = Priority::Normal;
    std::string name;
    std::shared_ptr<JobState> state;
    std::function<std::function<void()>(Context &)> run;
};

struct CompletionEntry {
    JobId id = 0;
    std::function<void()> fn;
    std::shared_ptr<JobState> state;
};

struct ThreadContext {
    bool isWorker = false;
    const Job *currentJob = nullptr;
};

ThreadContext &threadContext() {
    thread_local ThreadContext context;
    return context;
}

int priorityIndex(Priority priority) {
    return static_cast<int>(priority);
}

} // namespace

// Thread name for the debugger/profiler; a local duplicate of Foundation::setCurrentThreadName —
// PandaAsync is standalone and does not pull in Foundation for the sake of a single call.
static void setCurrentThreadName(const char *name) {
#if defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(_WIN32)
    typedef HRESULT(WINAPI * SetThreadDescriptionProc)(HANDLE, PCWSTR);
    SetThreadDescriptionProc setDescription =
        (SetThreadDescriptionProc)::GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetThreadDescription");
    if (setDescription != NULL) {
        const size_t length = strlen(name) + 1;
        wchar_t *wide = (wchar_t *)_alloca(length * sizeof(wchar_t));
        mbstowcs(wide, name, length);
        setDescription(GetCurrentThread(), wide);
    }
#else
    (void)name;
#endif
}

struct Scheduler::Impl {
    SchedulerDesc desc;

    std::vector<std::thread> workers;
    std::deque<Job> queues[kPriorityCount];
    std::deque<CompletionEntry> completionQueues[kPriorityCount];

    std::mutex mutex;
    std::condition_variable workCv; // wakes workers on new work or stop.
    std::condition_variable waitCv; // wakes waitAll when outstanding hits zero.

    std::unordered_map<JobId, std::weak_ptr<JobState>> index;

    uint64_t nextJobId = 0;
    uint64_t nextGroupId = 0;
    uint64_t outstanding = 0; // submitted work not yet handled by a worker.
    bool stopping = false;

    std::thread::id mainThreadId;
    bool hasMainThread = false;
};

Scheduler::Scheduler(SchedulerDesc desc)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->desc = desc;

    uint32_t count = desc.workerCount > 0 ? desc.workerCount : 1;

    m_impl->workers.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        m_impl->workers.emplace_back([this, i] {
            setCurrentThreadName(("PandaAsync Worker " + std::to_string(i)).c_str());
            runWorker();
        });
    }
}

Scheduler::~Scheduler() {
    shutdown();
}

void Scheduler::registerMainThread() {
    m_impl->mainThreadId = std::this_thread::get_id();
    m_impl->hasMainThread = true;
}

bool Scheduler::isMainThread() const {
    return m_impl->hasMainThread && std::this_thread::get_id() == m_impl->mainThreadId;
}

GroupId Scheduler::createGroup() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return ++m_impl->nextGroupId;
}

JobHandle Scheduler::submit(JobDesc desc,
                            std::function<void(Context &)> work,
                            std::function<void()> complete) {
    ErasedWork run = [work = std::move(work), complete = std::move(complete)](
                         Context &ctx) mutable -> CompletionFn {
        if (ctx.isCanceled()) { return {}; }
        work(ctx);
        if (ctx.isCanceled() || !complete) { return {}; }
        return [complete = std::move(complete)]() mutable { complete(); };
    };
    return submitErased(desc, std::move(run));
}

JobHandle Scheduler::submitErased(JobDesc desc, ErasedWork run) {
    auto state = std::make_shared<JobState>();
    state->group = desc.group;

    std::unique_lock<std::mutex> lock(m_impl->mutex);
    if (m_impl->stopping) { return JobHandle(); }

    JobId id = ++m_impl->nextJobId;

    Job job;
    job.id = id;
    job.group = desc.group;
    job.priority = desc.priority;
    job.name = desc.name ? desc.name : "";
    job.state = state;
    job.run = std::move(run);

    m_impl->index[id] = state;
    m_impl->queues[priorityIndex(desc.priority)].push_back(std::move(job));
    ++m_impl->outstanding;

    lock.unlock();
    m_impl->workCv.notify_one();
    return JobHandle(id, std::move(state));
}

JobHandle Scheduler::post(JobDesc desc, std::function<void()> complete) {
    if (!complete) { return JobHandle(); }
    auto state = std::make_shared<JobState>();
    state->group = desc.group;

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->stopping) { return JobHandle(); }

    JobId id = ++m_impl->nextJobId;
    m_impl->index[id] = state;
    m_impl->completionQueues[priorityIndex(desc.priority)].push_back(
        CompletionEntry{id, std::move(complete), state});
    return JobHandle(id, std::move(state));
}

void Scheduler::cancel(JobId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto it = m_impl->index.find(id);
    if (it == m_impl->index.end()) { return; }
    if (auto state = it->second.lock()) { state->canceled.store(true); }
}

void Scheduler::cancelGroup(GroupId group) {
    if (group == 0) { return; }
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto it = m_impl->index.begin(); it != m_impl->index.end();) {
        if (auto state = it->second.lock()) {
            if (state->group == group) { state->canceled.store(true); }
            ++it;
        } else {
            it = m_impl->index.erase(it);
        }
    }

    for (auto &queue : m_impl->completionQueues) {
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->state && it->state->group == group) {
                m_impl->index.erase(it->id);
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Scheduler::cancelAll() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto it = m_impl->index.begin(); it != m_impl->index.end();) {
        if (auto state = it->second.lock()) {
            state->canceled.store(true);
            ++it;
        } else {
            it = m_impl->index.erase(it);
        }
    }

    for (auto &queue : m_impl->completionQueues) {
        for (const CompletionEntry &entry : queue) {
            m_impl->index.erase(entry.id);
        }
        queue.clear();
    }
}

bool Scheduler::isCanceled(JobId id) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto it = m_impl->index.find(id);
    if (it == m_impl->index.end()) { return false; }
    if (auto state = it->second.lock()) { return state->canceled.load(); }
    return false;
}

uint32_t Scheduler::processCompletions(CompletionBudget budget) {
    assert((!m_impl->hasMainThread || isMainThread()) &&
           "processCompletions must run on the registered main thread");

    uint32_t processed = 0;
    auto start = std::chrono::steady_clock::now();

    while (processed < budget.maxCount) {
        CompletionEntry entry;
        bool got = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            for (int p = 0; p < kPriorityCount; ++p) {
                auto &queue = m_impl->completionQueues[p];
                if (!queue.empty()) {
                    entry = std::move(queue.front());
                    queue.pop_front();
                    m_impl->index.erase(entry.id);
                    got = true;
                    break;
                }
            }
        }
        if (!got) { break; }
        ++processed;

        if (!entry.state->canceled.load()) {
            try {
                entry.fn();
            } catch (...) {
                // Failed completion is contained by the scheduler. Callers can add
                // diagnostics later without letting exceptions cross frame code.
            }
        }

        if (budget.maxMilliseconds > 0.0) {
            double elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
            if (elapsed >= budget.maxMilliseconds) { break; }
        }
    }
    return processed;
}

void Scheduler::waitAll() {
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    m_impl->waitCv.wait(lock, [this] { return m_impl->outstanding == 0; });
}

void Scheduler::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->stopping) { return; }

        for (auto it = m_impl->index.begin(); it != m_impl->index.end();) {
            if (auto state = it->second.lock()) {
                state->canceled.store(true);
                ++it;
            } else {
                it = m_impl->index.erase(it);
            }
        }

        for (auto &queue : m_impl->completionQueues) {
            for (const CompletionEntry &entry : queue) {
                m_impl->index.erase(entry.id);
            }
            queue.clear();
        }

        for (auto &queue : m_impl->queues) {
            for (const Job &job : queue) {
                m_impl->index.erase(job.id);
                if (m_impl->outstanding > 0) { --m_impl->outstanding; }
            }
            queue.clear();
        }

        m_impl->stopping = true;
        if (m_impl->outstanding == 0) { m_impl->waitCv.notify_all(); }
    }
    m_impl->workCv.notify_all();
    m_impl->waitCv.notify_all();

    for (auto &worker : m_impl->workers) {
        if (worker.joinable()) { worker.join(); }
    }
    m_impl->workers.clear();
}

void Scheduler::runWorker() {
    ThreadContext &tc = threadContext();
    tc.isWorker = true;

    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_impl->mutex);
            m_impl->workCv.wait(lock, [this] {
                if (m_impl->stopping) { return true; }
                for (int p = 0; p < kPriorityCount; ++p) {
                    if (!m_impl->queues[p].empty()) { return true; }
                }
                return false;
            });
            if (m_impl->stopping) { break; } // pending queued work is dropped on shutdown.

            for (int p = 0; p < kPriorityCount; ++p) {
                auto &queue = m_impl->queues[p];
                if (!queue.empty()) {
                    job = std::move(queue.front());
                    queue.pop_front();
                    break;
                }
            }
        }

        CompletionFn completion;
        if (!job.state->canceled.load()) {
            tc.currentJob = &job;
            Context ctx(job.id, job.group, job.name.c_str(), job.state.get());
            try {
                completion = job.run(ctx);
            } catch (...) {
                completion = nullptr; // failed work: no completion is queued.
            }
            tc.currentJob = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (completion && !job.state->canceled.load()) {
                m_impl->completionQueues[priorityIndex(job.priority)].push_back(
                    CompletionEntry{job.id, std::move(completion), job.state});
            } else {
                m_impl->index.erase(job.id);
            }
            --m_impl->outstanding;
            if (m_impl->outstanding == 0) { m_impl->waitCv.notify_all(); }
        }
    }
}

Scheduler &defaultScheduler() {
    static Scheduler scheduler;
    return scheduler;
}

bool isWorkerThread() {
    return threadContext().isWorker;
}

const char *currentJobName() {
    const Job *job = threadContext().currentJob;
    return job ? job->name.c_str() : nullptr;
}

bool Context::isCanceled() const {
    return m_state && m_state->canceled.load();
}

void JobHandle::cancel() {
    if (m_state) { m_state->canceled.store(true); }
}

bool JobHandle::isCanceled() const {
    return m_state && m_state->canceled.load();
}

} // namespace PandaAsync

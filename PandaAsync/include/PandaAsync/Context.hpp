#pragma once

#include <PandaAsync/Types.hpp>

namespace PandaAsync {

class Scheduler;

// Passed to work() on the worker thread. Lets work read its identity and poll
// cancellation. Constructed only by the scheduler.
class Context {
public:
    JobId jobId() const {
        return m_id;
    }

    GroupId group() const {
        return m_group;
    }

    const char *name() const {
        return m_name;
    }

    bool isCanceled() const;

private:
    friend class Scheduler;

    Context(JobId id, GroupId group, const char *name, const JobState *state)
        : m_id(id)
        , m_group(group)
        , m_name(name)
        , m_state(state) {}

    JobId m_id = 0;
    GroupId m_group = 0;
    const char *m_name = nullptr;
    const JobState *m_state = nullptr;
};

} // namespace PandaAsync

#pragma once

#include <PandaAsync/Types.hpp>

#include <memory>
#include <utility>

namespace PandaAsync {

class Scheduler;

// Lightweight reference to a submitted job. Does not own the scheduler; after the
// scheduler is destroyed or shut down it only reports the last known state.
class JobHandle {
public:
    JobHandle() = default;

    JobId id() const {
        return m_id;
    }

    bool isValid() const {
        return static_cast<bool>(m_state);
    }

    void cancel();
    bool isCanceled() const;

private:
    friend class Scheduler;

    JobHandle(JobId id, std::shared_ptr<JobState> state)
        : m_id(id)
        , m_state(std::move(state)) {}

    JobId m_id = 0;
    std::shared_ptr<JobState> m_state;
};

} // namespace PandaAsync

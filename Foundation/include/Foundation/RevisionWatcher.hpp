#pragma once

#include <cstdint>

namespace Foundation {

// Follows a revision counter of a model that grows with every change (a store, an update state):
// changed(revision) is true once per new value, so a view that polls every frame rebuilds exactly
// when something happened, and the first poll always reports a change. reset() makes the next
// poll report one too (the view wants a rebuild for a reason of its own).
class RevisionWatcher {
public:
    bool changed(uint64_t revision) {
        const bool result = m_forced || revision != m_seen;
        m_forced = false;
        m_seen = revision;
        return result;
    }
    void reset() {
        m_forced = true;
    }

private:
    uint64_t m_seen = 0;
    bool m_forced = true;
};

} // namespace Foundation

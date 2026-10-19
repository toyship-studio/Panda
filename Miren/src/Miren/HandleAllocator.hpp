//
// Created by Admin on 17/04/2022.
//

#pragma once

#include "Miren/Base.hpp"

namespace Miren {

template<typename T, uint32_t Max>
class FreeHandleQueue {
public:
    FreeHandleQueue()
        : m_num(0) {}

    void queue(T handle) {
        PND_ASSERT_F(m_num < Max, "Miren free handle queue overflow. Max handles: %d", Max);
        if (m_num >= Max) { return; }
        m_queue[m_num] = handle;
        ++m_num;
    }

    void reset() {
        m_num = 0;
    }

    T get(uint16_t id) const {
        return m_queue[id];
    }

    uint16_t getNumQueued() const {
        return m_num;
    }

private:
    T m_queue[Max];
    uint16_t m_num;
};

template<typename T>
class HandleAllocator {
public:
    explicit HandleAllocator(uint16_t maxHandles)
        : m_numHandles(0)
        , m_maxHandles(maxHandles) {
        m_dense = new T[m_maxHandles];
        m_sparse = new uint16_t[m_maxHandles];
        reset();
    }

    ~HandleAllocator() {
        delete[] m_dense;
        delete[] m_sparse;
    }

    T getHandleAt(uint16_t at) const {
        return m_dense[at];
    }

    uint16_t getNumHandles() const {
        return m_numHandles;
    }

    uint16_t getMaxHandles() const {
        return m_maxHandles;
    }

    T alloc() {
        if (m_numHandles >= m_maxHandles) {
            PND_ASSERT_F(false, "Miren handle allocator exhausted. Max handles: %d", m_maxHandles);
            return T(MIREN_INVALID_HANDLE);
        }

        uint16_t index = m_numHandles;
        ++m_numHandles;

        T handle = m_dense[index];
        m_sparse[handle.id] = index;
        return handle;
    }

    bool isValid(T handle) const {
        if (handle.id == MIREN_INVALID_HANDLE) { return false; }
        uint16_t index = m_sparse[handle.id];
        return index < m_numHandles && m_dense[index].id == handle.id;
    }

    void free(T handle) {
        PND_ASSERT(isValid(handle), "INVALID HANDLE FREE");
        if (!isValid(handle)) { return; }
        uint16_t index = m_sparse[handle.id];
        --m_numHandles;
        T temp = m_dense[m_numHandles];
        m_dense[m_numHandles] = handle;
        m_sparse[temp.id] = index;
        m_dense[index] = temp;
    }

    void reset() {
        m_numHandles = 0;
        for (uint16_t i = 0; i < m_maxHandles; ++i) {
            m_dense[i] = i;
        }
    }

private:
    T *m_dense;
    uint16_t *m_sparse;
    uint16_t m_numHandles;
    uint16_t m_maxHandles;
};

} // namespace Miren

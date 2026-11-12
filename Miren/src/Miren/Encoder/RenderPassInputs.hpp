#pragma once

#include "Miren/Base.hpp"
#include "Miren/Encoder/PassInput.hpp"
#include "PassInput.hpp"

#include <cstddef>
#include <cstdint>

namespace Miren {

// A stream of inputs (name + value): written by the app thread during the frame, read by the
// render thread — rewind(), then readInput() until nullptr. The stream can be read many times
// (the inputs of a view are applied to each of its draws).
class RenderPassInputs {
public:
    // size — the initial capacity; 0 allocates nothing until the first write.
    explicit RenderPassInputs(uint32_t size = 0)
        : m_size(size)
        , m_pos(0)
        , m_end(0)
        , m_data(size > 0 ? (uint8_t *)F_ALLOC(Foundation::getAllocator(), size) : nullptr) {}

    RenderPassInputs(const RenderPassInputs &) = delete;
    RenderPassInputs &operator=(const RenderPassInputs &) = delete;

    RenderPassInputs(RenderPassInputs &&other) noexcept
        : m_size(other.m_size)
        , m_pos(other.m_pos)
        , m_end(other.m_end)
        , m_data(other.m_data) {
        other.m_data = nullptr;
        other.m_pos = 0;
        other.m_end = 0;
        other.m_size = 0;
    }

    RenderPassInputs &operator=(RenderPassInputs &&other) noexcept {
        if (this == &other) { return *this; }
        if (m_data) { F_FREE(Foundation::getAllocator(), m_data); }
        m_size = other.m_size;
        m_pos = other.m_pos;
        m_end = other.m_end;
        m_data = other.m_data;
        other.m_data = nullptr;
        other.m_pos = 0;
        other.m_end = 0;
        other.m_size = 0;
        return *this;
    }

    ~RenderPassInputs() {
        if (m_data) { F_FREE(Foundation::getAllocator(), m_data); }
    }

    void writeInput(PassInput &input) {
        align(__alignof(PassInput));
        const uint32_t inputPos = m_pos;
        write(&input, sizeof(PassInput));
        if (input.data) {
            align(alignof(std::max_align_t));
            const uint32_t payloadPos = m_pos;
            write(input.data, input.size);
            // The saved input is patched only AFTER all the writes: ensureCapacity
            // may have moved m_data, and the old pointers into the buffer are dead.
            PassInput *savedInput = (PassInput *)&m_data[inputPos];
            savedInput->alignment = alignof(std::max_align_t);
            savedInput->data = &m_data[payloadPos];
        }
        m_end = m_pos;
    }

    PassInput *readInput() {
        if (m_pos >= m_end) { return nullptr; }
        align(__alignof(PassInput));
        PassInput &input = *(PassInput *)read(sizeof(PassInput));
        if (input.data) {
            align(input.alignment);
            // The data address is restored from the position: buffer growth within the same
            // frame invalidates the pointers saved by earlier writeInput calls.
            input.data = &m_data[m_pos];
            skip(input.size);
        }
        return &input;
    }

    // Start reading from the first input.
    void rewind() {
        m_pos = 0;
    }

    // Drop the inputs: the stream is ready for the writes of the next frame.
    void reset() {
        m_pos = 0;
        m_end = 0;
    }

private:
    void ensureCapacity(uint32_t requiredSize) {
        if (requiredSize <= m_size) { return; }
        uint32_t newSize = m_size > 0 ? m_size : 1;
        while (newSize < requiredSize) {
            newSize *= 2;
        }
        uint8_t *newData = (uint8_t *)F_ALLOC(Foundation::getAllocator(), newSize);
        if (m_data && m_pos > 0) { memcpy(newData, m_data, m_pos < m_size ? m_pos : m_size); }
        if (m_data) { F_FREE(Foundation::getAllocator(), m_data); }
        m_data = newData;
        m_size = newSize;
    }

    void write(const void *_data, uint32_t _size) {
        ensureCapacity(m_pos + _size);
        memcpy(&m_data[m_pos], _data, _size);
        m_pos += _size;
        PND_ASSERT(m_pos <= m_size, "NEED MORE SPACE FOR BUFFER");
    }

    void *read(uint32_t _size) {
        uint32_t pos = m_pos;
        m_pos += _size;
        return &m_data[pos];
    }

    void skip(uint32_t size) {
        m_pos += size;
    }

    void align(uint32_t _alignment) {
        const uint32_t mask = _alignment - 1;
        const uint32_t pos = (m_pos + mask) & (~mask);
        m_pos = pos;
    }

    uint32_t m_size;
    uint32_t m_pos;
    uint32_t m_end; // the end of the written inputs
    uint8_t *m_data;
};

} // namespace Miren

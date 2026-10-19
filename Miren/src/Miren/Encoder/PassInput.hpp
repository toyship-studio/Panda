#pragma once

#include "Miren/Miren.hpp"

#include <Foundation/Logger.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace Miren {

// Draw call input: name + value, without a program — the slot is resolved at render time
// by the layout of the submit's program (submit(viewId, program)).
struct PassInput {
    static constexpr size_t MAX_NAME_SIZE = 128;

    std::array<char, MAX_NAME_SIZE> name;
    RenderPassInputType type;
    void *data;
    uint16_t alignment;
    uint16_t size;

    PassInput(std::string_view name, RenderPassInputType type, void *data, uint16_t size)
        : name()
        , type(type)
        , data(data)
        , alignment(0)
        , size(size) {
        PND_ASSERT(name.size() < MAX_NAME_SIZE, "PASS INPUT NAME IS TOO LONG");
        const size_t copySize = std::min(name.size(), MAX_NAME_SIZE - 1);
        if (copySize > 0) { std::memcpy(this->name.data(), name.data(), copySize); }
        this->name[copySize] = '\0';
    }
};

} // namespace Miren

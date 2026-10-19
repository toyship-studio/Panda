#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cstdint>

namespace PandaUI {

struct TextureHandle {
    uint64_t id = 0;
    Size size;

    bool isValid() const {
        return id != 0;
    }

    explicit operator bool() const {
        return isValid();
    }

    bool operator==(const TextureHandle &other) const {
        return id == other.id;
    }

    bool operator!=(const TextureHandle &other) const {
        return !(*this == other);
    }
};

} // namespace PandaUI

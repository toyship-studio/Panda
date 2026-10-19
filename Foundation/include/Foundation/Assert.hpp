#pragma once

#include "Foundation/Logger.hpp"

#include <cassert>

namespace Foundation {

#define PND_STATIC_ASSERT(_condition, ...) static_assert(_condition, "" __VA_ARGS__)

// The condition is parenthesized as a whole: `a || b == false` would otherwise be parsed as
// `a || (b == false)`.
#define PND_ASSERT(_condition, _message)                                                                               \
    if (!(_condition)) {                                                                                               \
        LOG_CRITICAL("CHECK " _message);                                                                               \
        assert(false);                                                                                                 \
    }

#define PND_ASSERT_F(_condition, _format, ...)                                                                         \
    if (!(_condition)) {                                                                                               \
        LOG_CRITICAL("CHECK " _format, ##__VA_ARGS__);                                                                 \
        assert(false);                                                                                                 \
    }

} // namespace Foundation
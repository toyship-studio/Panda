#pragma once

// The bottom layer of the engine, shared by Fern, Miren, Panda and the tools that do not depend
// on each other: logging (Logger.hpp), asserts (Assert.hpp), the allocator and the memory blob
// (Allocator.hpp, Memory.hpp), the profiler (Profile.hpp), the platform and the graphics API
// (PlatformDetection.hpp, GraphicsAPI.hpp), the command buffer, a semaphore, thread names and a
// file reader. This header is the umbrella of the first four plus a handful of helpers.

#include "Allocator.hpp"
#include "Assert.hpp"
#include "Logger.hpp"
#include "Memory.hpp"

#include <cmath>
#include <memory>

namespace Foundation {

// A shared pointer whose object lives in the Foundation allocator (F_NEW / F_DELETE).
template<typename T>
using Shared = std::shared_ptr<T>;

template<typename T>
constexpr Shared<T> createShared(T *ptr) {
    AllocatorI *alloc = getAllocator();
    auto deleter = [alloc](T *ptr) { F_DELETE(alloc, ptr); };
    return std::shared_ptr<T>(ptr, deleter);
}

template<typename T1, typename T2>
inline Shared<T1> SharedCast(const Shared<T2> &r) {
    return std::dynamic_pointer_cast<T1>(r);
}

template<typename T, typename... Args>
constexpr Shared<T> makeShared(Args &&...args) {
    AllocatorI *alloc = getAllocator();
    T *ptr = F_NEW(alloc, T)(std::forward<Args>(args)...);
    auto deleter = [alloc](T *ptr) { F_DELETE(alloc, ptr); };
    return std::shared_ptr<T>(ptr, deleter);
}

template<typename Ty>
inline void swap(Ty &_a, Ty &_b) {
    Ty tmp = _a;
    _a = _b;
    _b = tmp;
}

template<typename Ty>
inline constexpr Ty min(const Ty &_a, const Ty &_b) {
    return _a < _b ? _a : _b;
}

template<typename Ty>
inline constexpr Ty max(const Ty &_a, const Ty &_b) {
    return _a > _b ? _a : _b;
}

template<typename Ty>
inline constexpr Ty clamp(const Ty &_a, const Ty &_min, const Ty &_max) {
    return max(min(_a, _max), _min);
}

inline int32_t strCmp(const char *lhs, const char *rhs, int32_t max) {
    for (; max > 0 && *lhs == *rhs; ++lhs, ++rhs, --max) {
        if (*lhs == '\0' || *rhs == '\0') { break; }
    }

    if (0 == max) { return 0; }

    return *lhs - *rhs;
}

inline bool epsilonEqual(float const &x, float const &y, float const &epsilon) {
    return std::fabs(x - y) < epsilon;
}

} // namespace Foundation

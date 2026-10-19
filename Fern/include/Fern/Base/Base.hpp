#pragma once

#include <Foundation/Foundation.hpp>
#include <cstdint>
#include <float.h>

namespace Fern {

struct Vec2 {
    union {
        float x, width;
    };
    union {
        float y, height;
    };

    Vec2()
        : x(0)
        , y(0) {}

    Vec2(float x, float y)
        : x(x)
        , y(y) {}

    bool operator==(const Vec2 &l) {
        return l.x == x && l.y == y;
    }

    bool operator!=(const Vec2 &l) {
        return !(*this == l);
    }

    Vec2 operator*(const Vec2 other) {
        return Vec2(x * other.x, y * other.y);
    }
};

struct Vec3 {
    union {
        float x, r, width;
    };
    union {
        float y, g, height;
    };
    union {
        float z, b;
    };

    Vec3()
        : x(0)
        , y(0)
        , z(0) {}

    Vec3(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z) {}
};

struct Vec4 {
    union {
        float x, r, width;
    };
    union {
        float y, g, height;
    };
    union {
        float z, b;
    };
    union {
        float w, a;
    };

    Vec4()
        : x(0)
        , y(0)
        , z(0)
        , w(0) {}

    Vec4(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w) {}

    bool operator==(const Vec4 &other) const {
        return Foundation::epsilonEqual(x, other.x, FLT_EPSILON) && Foundation::epsilonEqual(y, other.y, FLT_EPSILON) &&
               Foundation::epsilonEqual(z, other.z, FLT_EPSILON) && Foundation::epsilonEqual(w, other.w, FLT_EPSILON);
    }
};

using Size = Vec2;
using Color = Vec4;
using Quat = Vec4;

struct Rect {
    Vec2 origin;
    Vec2 size;

    Rect() {}

    Rect(float x, float y, float width, float height)
        : origin(x, y)
        , size(width, height) {}
};

struct EdgeInsets {
    float top, left, right, bottom;

    EdgeInsets()
        : top(0.f)
        , left(0.f)
        , right(0.f)
        , bottom(0.f) {}

    EdgeInsets(float top, float left, float right, float bottom)
        : top(top)
        , left(left)
        , right(right)
        , bottom(bottom) {}
};

enum class WindowState {
    WindowStateNone = 0,
    WindowStateNormal,
    WindowStateMaximized,
    WindowStateMinimized,
    WindowStateFullScreen
};

enum class WindowType { Normal, Modal, Popup };

using WindowFlags = uint32_t;

#define FERN_BIT(x) (1u << (x))

#define FERN_WINDOW_FLAG_RESIZABLE FERN_BIT(1)
#define FERN_WINDOW_FLAG_CLOSABLE FERN_BIT(2)
#define FERN_WINDOW_FLAG_MINIMIZABLE FERN_BIT(3)
#define FERN_WINDOW_FLAG_MAXIMIZABLE FERN_BIT(4)
#define FERN_WINDOW_FLAG_ALWAYS_ON_TOP FERN_BIT(5)

#define FERN_WINDOW_FLAGS_DEFAULT                                                                                      \
    (FERN_WINDOW_FLAG_RESIZABLE | FERN_WINDOW_FLAG_CLOSABLE | FERN_WINDOW_FLAG_MINIMIZABLE |                           \
     FERN_WINDOW_FLAG_MAXIMIZABLE)

inline bool hasWindowFlag(WindowFlags flags, WindowFlags flag) {
    return (flags & flag) != 0;
}

} // namespace Fern

#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace PandaUI {

struct Size {
    float width, height;

    Size()
        : width(0)
        , height(0) {}

    Size(float width, float height)
        : width(width)
        , height(height) {}

    inline bool isZero() {
        return width == 0 && height == 0;
    }
};

struct Point {
    float x, y;

    Point()
        : x(0)
        , y(0) {}

    Point(glm::vec2 p)
        : x(p.x)
        , y(p.y) {}

    Point(float x, float y)
        : x(x)
        , y(y) {}

    inline bool isZero() {
        return x == 0 && y == 0;
    }
};

struct Rect {
    Point origin;
    Size size;

    Rect()
        : origin()
        , size() {}

    Rect(Point origin, Size size)
        : origin(origin)
        , size(size) {}

    Rect(float x, float y, float width, float height)
        : origin(x, y)
        , size(width, height) {}

    Rect(int x, int y, int width, int height)
        : origin(x, y)
        , size(width, height) {}

    inline static Rect zero() {
        return Rect();
    }

    inline bool isZero() {
        return origin.isZero() && size.isZero();
    }
};

struct Color {
    float r, g, b, a;

    Color()
        : r(1)
        , g(0)
        , b(0)
        , a(1) {}

    Color(glm::vec4 c)
        : r(c.x)
        , g(c.y)
        , b(c.z)
        , a(c.w) {}

    Color(float r, float g, float b, float a)
        : r(r)
        , g(g)
        , b(b)
        , a(a) {}

    Color(uint32_t rgba) {
        uint8_t _r = rgba >> 24;
        uint8_t _g = rgba >> 16;
        uint8_t _b = rgba >> 8;
        uint8_t _a = rgba >> 0;
        r = (_r) / 255.f;
        g = (_g) / 255.f;
        b = (_b) / 255.f;
        a = (_a) / 255.f;
    }

    operator glm::vec4() const {
        return {r, g, b, a};
    }
};

struct EdgeInsets {
    float top, left, right, bottom;

    EdgeInsets(float top, float left, float right, float bottom)
        : top(top)
        , left(left)
        , right(right)
        , bottom(bottom) {}

    EdgeInsets()
        : top(0.f)
        , left(0.f)
        , right(0.f)
        , bottom(0.f) {}

    bool operator==(const EdgeInsets &other) const {
        return top == other.top && left == other.left && right == other.right && bottom == other.bottom;
    }

    bool operator!=(const EdgeInsets &other) const {
        return !(*this == other);
    }
};

struct SizeRange {
    Size min;
    Size max;

    SizeRange(Size min, Size max)
        : min(min)
        , max(max) {}
};

} // namespace PandaUI

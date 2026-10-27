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

    constexpr Color()
        : r(1)
        , g(0)
        , b(0)
        , a(1) {}

    Color(glm::vec4 c)
        : r(c.x)
        , g(c.y)
        , b(c.z)
        , a(c.w) {}

    constexpr Color(float r, float g, float b, float a)
        : r(r)
        , g(g)
        , b(b)
        , a(a) {}

    constexpr Color(uint32_t rgba)
        : r(static_cast<uint8_t>(rgba >> 24) / 255.f)
        , g(static_cast<uint8_t>(rgba >> 16) / 255.f)
        , b(static_cast<uint8_t>(rgba >> 8) / 255.f)
        , a(static_cast<uint8_t>(rgba >> 0) / 255.f) {}

    operator glm::vec4() const {
        return {r, g, b, a};
    }

    constexpr bool operator==(const Color &other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    constexpr bool operator!=(const Color &other) const {
        return !(*this == other);
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

// The radius of each corner, clockwise from the top left. A uniform value is the common case; a
// tab (round on top, flat below) or a bar segment (round on one side) differ per corner.
struct CornerRadii {
    float topLeft = 0.f, topRight = 0.f, bottomRight = 0.f, bottomLeft = 0.f;

    constexpr CornerRadii() = default;
    constexpr CornerRadii(float radius)
        : topLeft(radius)
        , topRight(radius)
        , bottomRight(radius)
        , bottomLeft(radius) {}
    constexpr CornerRadii(float topLeft, float topRight, float bottomRight, float bottomLeft)
        : topLeft(topLeft)
        , topRight(topRight)
        , bottomRight(bottomRight)
        , bottomLeft(bottomLeft) {}

    static constexpr CornerRadii top(float radius) {
        return CornerRadii(radius, radius, 0.f, 0.f);
    }
    static constexpr CornerRadii bottom(float radius) {
        return CornerRadii(0.f, 0.f, radius, radius);
    }
    static constexpr CornerRadii left(float radius) {
        return CornerRadii(radius, 0.f, 0.f, radius);
    }
    static constexpr CornerRadii right(float radius) {
        return CornerRadii(0.f, radius, radius, 0.f);
    }

    constexpr bool isUniform() const {
        return topLeft == topRight && topRight == bottomRight && bottomRight == bottomLeft;
    }
    constexpr bool isZero() const {
        return topLeft <= 0.f && topRight <= 0.f && bottomRight <= 0.f && bottomLeft <= 0.f;
    }
    constexpr float max() const {
        float result = topLeft;
        if (topRight > result) { result = topRight; }
        if (bottomRight > result) { result = bottomRight; }
        if (bottomLeft > result) { result = bottomLeft; }
        return result;
    }
    constexpr bool operator==(const CornerRadii &other) const {
        return topLeft == other.topLeft && topRight == other.topRight && bottomRight == other.bottomRight &&
               bottomLeft == other.bottomLeft;
    }
    constexpr bool operator!=(const CornerRadii &other) const {
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

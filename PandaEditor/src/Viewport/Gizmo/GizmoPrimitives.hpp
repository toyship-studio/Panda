#pragma once

#include "Gizmo.hpp"

#include <cstdint>

namespace Panda::Gizmo {

using DrawColor = uint32_t;

struct ScreenPoint {
    float x = 0.f;
    float y = 0.f;

    ScreenPoint() = default;
    ScreenPoint(float x, float y)
        : x(x)
        , y(y) {}
    ScreenPoint(Vec2 value)
        : x(value.x)
        , y(value.y) {}

    float operator[](int index) const {
        return index == 0 ? x : y;
    }

    float &operator[](int index) {
        return index == 0 ? x : y;
    }
};

struct StyleColor {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;

    StyleColor() = default;
    StyleColor(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w) {}
};

inline ScreenPoint operator+(ScreenPoint lhs, ScreenPoint rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

inline ScreenPoint operator-(ScreenPoint lhs, ScreenPoint rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

inline ScreenPoint operator*(ScreenPoint lhs, float rhs) {
    return {lhs.x * rhs, lhs.y * rhs};
}

inline ScreenPoint operator*(float lhs, ScreenPoint rhs) {
    return {rhs.x * lhs, rhs.y * lhs};
}

inline ScreenPoint operator*(ScreenPoint lhs, ScreenPoint rhs) {
    return {lhs.x * rhs.x, lhs.y * rhs.y};
}

inline ScreenPoint operator/(ScreenPoint lhs, float rhs) {
    return {lhs.x / rhs, lhs.y / rhs};
}

inline ScreenPoint &operator+=(ScreenPoint &lhs, ScreenPoint rhs) {
    lhs.x += rhs.x;
    lhs.y += rhs.y;
    return lhs;
}

inline ScreenPoint &operator-=(ScreenPoint &lhs, ScreenPoint rhs) {
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;
    return lhs;
}

inline ScreenPoint &operator/=(ScreenPoint &lhs, float rhs) {
    lhs.x /= rhs;
    lhs.y /= rhs;
    return lhs;
}

inline ScreenPoint &operator*=(ScreenPoint &lhs, float rhs) {
    lhs.x *= rhs;
    lhs.y *= rhs;
    return lhs;
}

inline float lengthSquared(ScreenPoint value) {
    return value.x * value.x + value.y * value.y;
}

inline ScreenPoint lerp(ScreenPoint lhs, ScreenPoint rhs, ScreenPoint t) {
    return {lhs.x + (rhs.x - lhs.x) * t.x, lhs.y + (rhs.y - lhs.y) * t.y};
}

template<typename T>
inline T absolute(T value) {
    return value < static_cast<T>(0) ? -value : value;
}

class DrawList {
public:
    void addLine(ScreenPoint a, ScreenPoint b, DrawColor color, float thickness = 1.f);
    void addTriangleFilled(ScreenPoint a, ScreenPoint b, ScreenPoint c, DrawColor color);
    void addCircle(ScreenPoint center, float radius, DrawColor color, int segments = 0, float thickness = 1.f);
    void addCircleFilled(ScreenPoint center, float radius, DrawColor color, int segments = 0);
    void addPolyline(const ScreenPoint *points, int pointsCount, DrawColor color, bool closed, float thickness);
    void addConvexPolyFilled(const ScreenPoint *points, int pointsCount, DrawColor color);
};

constexpr DrawColor makeDrawColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

inline constexpr DrawColor whiteDrawColor = makeDrawColor(255, 255, 255, 255);
inline constexpr DrawColor blackDrawColor = makeDrawColor(0, 0, 0, 255);

namespace Backend {

const InputState &input();
DrawList &drawList();
void beginFrame();
void setInput(const InputState &input);
bool isViewportHovered();
bool isBlockedByUi();
bool isMouseClicked();
DrawColor toDrawColor(StyleColor color);
void addTriangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, DrawColor color);
int circleSegments(float radius, int requestedSegments);

} // namespace Backend

} // namespace Panda::Gizmo

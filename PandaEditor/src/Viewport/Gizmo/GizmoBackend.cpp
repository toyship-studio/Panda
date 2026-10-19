#include "GizmoPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Panda::Gizmo {

namespace {

struct DrawState {
    InputState input;
    DrawList drawList;
    std::vector<DrawVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawVertex> previousVertices; // the previous frame's geometry — the reference for generation
    std::vector<uint32_t> previousIndices;
    uint64_t generation = 0;
    bool frameDiffed = false;
    DrawData drawData;
};

DrawState s_state;

float clamp01(float value) {
    return std::max(0.f, std::min(1.f, value));
}

Color toColor(DrawColor color) {
    const float r = static_cast<float>(color & 0xffu) / 255.f;
    const float g = static_cast<float>((color >> 8) & 0xffu) / 255.f;
    const float b = static_cast<float>((color >> 16) & 0xffu) / 255.f;
    const float a = static_cast<float>((color >> 24) & 0xffu) / 255.f;
    return {r, g, b, a};
}

uint32_t addVertex(ScreenPoint point, DrawColor color) {
    s_state.vertices.push_back({Vec2(point.x, point.y), toColor(color)});
    return static_cast<uint32_t>(s_state.vertices.size() - 1);
}

void appendTriangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, DrawColor color) {
    const uint32_t start = static_cast<uint32_t>(s_state.vertices.size());
    addVertex(a, color);
    addVertex(b, color);
    addVertex(c, color);
    s_state.indices.push_back(start);
    s_state.indices.push_back(start + 1);
    s_state.indices.push_back(start + 2);
}

int resolveCircleSegments(float radius, int requestedSegments) {
    if (requestedSegments > 0) { return requestedSegments; }
    return std::clamp(static_cast<int>(radius * 0.75f), 16, 64);
}

} // namespace

void setInput(const InputState &input) {
    Backend::setInput(input);
}

const DrawData &getDrawData() {
    if (!s_state.frameDiffed) { // one comparison per frame — on the first read of the assembled geometry
        s_state.frameDiffed = true;
        const bool changed =
            s_state.vertices.size() != s_state.previousVertices.size() ||
            s_state.indices.size() != s_state.previousIndices.size() ||
            (!s_state.vertices.empty() &&
             std::memcmp(
                 s_state.vertices.data(), s_state.previousVertices.data(),
                 s_state.vertices.size() * sizeof(DrawVertex)
             ) != 0) ||
            (!s_state.indices.empty() &&
             std::memcmp(
                 s_state.indices.data(), s_state.previousIndices.data(),
                 s_state.indices.size() * sizeof(uint32_t)
             ) != 0);
        if (changed) { ++s_state.generation; }
    }
    s_state.drawData = {
        .vertices = s_state.vertices.data(),
        .indices = s_state.indices.data(),
        .verticesCount = static_cast<uint32_t>(s_state.vertices.size()),
        .indicesCount = static_cast<uint32_t>(s_state.indices.size()),
        .generation = s_state.generation,
    };
    return s_state.drawData;
}

namespace Backend {

const InputState &input() {
    return s_state.input;
}

DrawList &drawList() {
    return s_state.drawList;
}

void beginFrame() {
    // The displayed frame becomes the comparison reference for the new one (swap — no copies).
    std::swap(s_state.previousVertices, s_state.vertices);
    std::swap(s_state.previousIndices, s_state.indices);
    s_state.vertices.clear();
    s_state.indices.clear();
    s_state.frameDiffed = false;
}

void setInput(const InputState &input) {
    s_state.input = input;
}

bool isViewportHovered() {
    return s_state.input.viewportHovered && !s_state.input.blockedByUi;
}

bool isBlockedByUi() {
    return s_state.input.blockedByUi;
}

bool isMouseClicked() {
    return s_state.input.mouseClicked;
}

DrawColor toDrawColor(StyleColor color) {
    const auto toByte = [](float value) {
        return static_cast<DrawColor>(clamp01(value) * 255.f + 0.5f);
    };
    return makeDrawColor(toByte(color.x), toByte(color.y), toByte(color.z), toByte(color.w));
}

void addTriangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, DrawColor color) {
    appendTriangle(a, b, c, color);
}

int circleSegments(float radius, int requestedSegments) {
    return resolveCircleSegments(radius, requestedSegments);
}

} // namespace Backend

void DrawList::addLine(ScreenPoint a, ScreenPoint b, DrawColor color, float thickness) {
    if ((color >> 24) == 0) { return; }
    ScreenPoint direction = b - a;
    const float length = std::sqrt(lengthSquared(direction));
    if (length <= 0.001f) { return; }
    ScreenPoint normal = {-direction.y / length, direction.x / length};
    const float half = std::max(1.f, thickness) * 0.5f;
    normal = normal * half;
    Backend::addTriangle(a + normal, b + normal, b - normal, color);
    Backend::addTriangle(a + normal, b - normal, a - normal, color);
}

void DrawList::addTriangleFilled(ScreenPoint a, ScreenPoint b, ScreenPoint c, DrawColor color) {
    if ((color >> 24) == 0) { return; }
    Backend::addTriangle(a, b, c, color);
}

void DrawList::addCircle(ScreenPoint center, float radius, DrawColor color, int segments, float thickness) {
    const int count = Backend::circleSegments(radius, segments);
    std::vector<ScreenPoint> points;
    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(count) * 6.28318530718f;
        points.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
    addPolyline(points.data(), static_cast<int>(points.size()), color, true, thickness);
}

void DrawList::addCircleFilled(ScreenPoint center, float radius, DrawColor color, int segments) {
    if ((color >> 24) == 0) { return; }
    const int count = Backend::circleSegments(radius, segments);
    std::vector<ScreenPoint> points;
    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(count) * 6.28318530718f;
        points.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
    addConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
}

void DrawList::addPolyline(const ScreenPoint *points, int pointsCount, DrawColor color, bool closed, float thickness) {
    if (!points || pointsCount < 2 || (color >> 24) == 0) { return; }
    for (int i = 0; i < pointsCount - 1; ++i) { addLine(points[i], points[i + 1], color, thickness); }
    if (closed) { addLine(points[pointsCount - 1], points[0], color, thickness); }
}

void DrawList::addConvexPolyFilled(const ScreenPoint *points, int pointsCount, DrawColor color) {
    if (!points || pointsCount < 3 || (color >> 24) == 0) { return; }
    for (int i = 1; i < pointsCount - 1; ++i) {
        Backend::addTriangle(points[0], points[i], points[i + 1], color);
    }
}

} // namespace Panda::Gizmo

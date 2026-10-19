#pragma once

#include <Panda/Base/Base.hpp>

#include <cstdint>

namespace Panda::Gizmo {

enum Operation {
    translateX = (1u << 0),
    translateY = (1u << 1),
    translateZ = (1u << 2),
    rotateX = (1u << 3),
    rotateY = (1u << 4),
    rotateZ = (1u << 5),
    rotateScreen = (1u << 6),
    scaleX = (1u << 7),
    scaleY = (1u << 8),
    scaleZ = (1u << 9),
    scaleXUniversal = (1u << 10),
    scaleYUniversal = (1u << 11),
    scaleZUniversal = (1u << 12),

    translate = translateX | translateY | translateZ,
    rotate = rotateX | rotateY | rotateZ | rotateScreen,
    scale = scaleX | scaleY | scaleZ,
    scaleUniversal = scaleXUniversal | scaleYUniversal | scaleZUniversal,
    universal = translate | rotate | scaleUniversal,
};

inline Operation operator|(Operation lhs, Operation rhs) {
    return static_cast<Operation>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

enum Mode {
    local,
    world,
};

struct InputState {
    Vec2 mousePosition;
    bool mouseDown = false;
    bool mouseClicked = false;
    bool viewportHovered = false;
    bool blockedByUi = false;
};

struct DrawVertex {
    Vec2 position;
    Color color;
};

struct DrawData {
    const DrawVertex *vertices = nullptr;
    const uint32_t *indices = nullptr;
    uint32_t verticesCount = 0;
    uint32_t indicesCount = 0;
    // Grows only when the geometry actually changes (the backend compares the frame with the previous
    // one): consumers with cached drawing (the PandaUI overlay) redraw by this number.
    uint64_t generation = 0;
};

void setInput(const InputState &input);
void beginFrame();
void setRect(float x, float y, float width, float height);
void setOrthographic(bool isOrthographic);
bool isOver();
bool isUsing();
bool manipulate(
    const float *view,
    const float *projection,
    Operation operation,
    Mode mode,
    float *matrix,
    float *deltaMatrix = nullptr,
    const float *snap = nullptr,
    const float *localBounds = nullptr,
    const float *boundsSnap = nullptr
);
void decomposeMatrixToComponents(const float *matrix, float *translation, float *rotation, float *scale);
void recomposeMatrixFromComponents(const float *translation, const float *rotation, const float *scale, float *matrix);
const DrawData &getDrawData();

} // namespace Panda::Gizmo

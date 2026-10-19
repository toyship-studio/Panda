#pragma once

#include <box3d/box3d.h>

#include <cstdint>

namespace Panda {

// box3d identifiers in 32-bit form: Physics3D stores and hands out the physics world as a number
// so that the engine's public headers do not pull in box3d; whoever works with box3d directly
// (world systems of contributions) unpacks it here.
inline uint32_t b3WorldIdToInt(b3WorldId id) {
    return ((uint32_t)id.index1 << 16) | (uint32_t)id.generation;
}

inline b3WorldId intToB3WorldId(uint32_t val) {
    b3WorldId id;
    id.index1 = (uint16_t)(val >> 16);
    id.generation = (uint16_t)(val & 0xFFFF);
    return id;
}

} // namespace Panda

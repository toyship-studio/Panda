#include "Panda/Terrain/TerrainData3D.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

TerrainData3D TerrainData3D::makeFlat(
    uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ, float cellSize, uint32_t groundLevel
) {
    TerrainData3D data;
    data.cellsX = cellsX;
    data.cellsY = cellsY;
    data.cellsZ = cellsZ;
    data.cellSize = cellSize;
    data.materials.assign(static_cast<size_t>(cellsX) * cellsY * cellsZ, 0);
    const uint32_t solidHeight = std::min(groundLevel, cellsY);
    for (uint32_t y = 0; y < solidHeight; y++) {
        for (uint32_t z = 0; z < cellsZ; z++) {
            for (uint32_t x = 0; x < cellsX; x++) {
                data.setMaterialAt(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), 1);
            }
        }
    }
    return data;
}

bool TerrainData3D::isValid() const {
    return cellsX > 0 && cellsY > 0 && cellsZ > 0 && cellSize > 0.f &&
           materials.size() == static_cast<size_t>(cellsX) * cellsY * cellsZ;
}

uint32_t TerrainData3D::chunksX() const {
    return (cellsX + CHUNK_CELLS - 1) / CHUNK_CELLS;
}

uint32_t TerrainData3D::chunksY() const {
    return (cellsY + CHUNK_CELLS - 1) / CHUNK_CELLS;
}

uint32_t TerrainData3D::chunksZ() const {
    return (cellsZ + CHUNK_CELLS - 1) / CHUNK_CELLS;
}

bool TerrainData3D::isInBounds(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < static_cast<int>(cellsX) &&
           y < static_cast<int>(cellsY) && z < static_cast<int>(cellsZ);
}

uint8_t TerrainData3D::materialAt(int x, int y, int z) const {
    if (!isInBounds(x, y, z)) { return 0; }
    return materials
        [(static_cast<size_t>(y) * cellsZ + static_cast<size_t>(z)) * cellsX + static_cast<size_t>(x)];
}

void TerrainData3D::setMaterialAt(int x, int y, int z, uint8_t material) {
    if (!isInBounds(x, y, z)) { return; }
    materials
        [(static_cast<size_t>(y) * cellsZ + static_cast<size_t>(z)) * cellsX + static_cast<size_t>(x)] =
            material;
}

float TerrainData3D::nodeDensity(int i, int j, int k) const {
    int solid = 0;
    for (int dy = -1; dy <= 0; dy++) {
        for (int dx = -1; dx <= 0; dx++) {
            for (int dz = -1; dz <= 0; dz++) {
                if (isSolidAt(i + dx, j + dy, k + dz)) { solid++; }
            }
        }
    }
    return solid / 8.f;
}

float TerrainData3D::sampleDensity(const glm::vec3 &point) const {
    const glm::vec3 grid = point / cellSize;
    const int i = static_cast<int>(std::floor(grid.x));
    const int j = static_cast<int>(std::floor(grid.y));
    const int k = static_cast<int>(std::floor(grid.z));
    const float tx = grid.x - i;
    const float ty = grid.y - j;
    const float tz = grid.z - k;

    float corners[2][2][2];
    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            for (int dz = 0; dz <= 1; dz++) {
                corners[dy][dx][dz] = nodeDensity(i + dx, j + dy, k + dz);
            }
        }
    }
    const float bottom = glm::mix(
        glm::mix(corners[0][0][0], corners[0][1][0], tx),
        glm::mix(corners[0][0][1], corners[0][1][1], tx),
        tz
    );
    const float top = glm::mix(
        glm::mix(corners[1][0][0], corners[1][1][0], tx),
        glm::mix(corners[1][0][1], corners[1][1][1], tx),
        tz
    );
    return glm::mix(bottom, top, ty);
}

glm::vec3 TerrainData3D::fieldNormal(const glm::vec3 &point) const {
    const float delta = cellSize * 0.5f;
    const glm::vec3 gradient(
        sampleDensity(point + glm::vec3(delta, 0.f, 0.f)) -
            sampleDensity(point - glm::vec3(delta, 0.f, 0.f)),
        sampleDensity(point + glm::vec3(0.f, delta, 0.f)) -
            sampleDensity(point - glm::vec3(0.f, delta, 0.f)),
        sampleDensity(point + glm::vec3(0.f, 0.f, delta)) -
            sampleDensity(point - glm::vec3(0.f, 0.f, delta))
    );
    const float length = glm::length(gradient);
    if (length < 1e-6f) { return glm::vec3(0.f, 1.f, 0.f); }
    return -gradient / length; // the field grows into the solid — the normal points outward
}

bool TerrainData3D::sampleSurface(float x, float z, float &outHeight, glm::vec3 &outNormal) const {
    if (!isValid()) { return false; }
    const float maxX = cellsX * cellSize;
    const float maxZ = cellsZ * cellSize;
    if (x < 0.f || z < 0.f || x > maxX || z > maxZ) { return false; }

    const float step = cellSize * 0.5f;
    const float topY = cellsY * cellSize;
    float previousY = topY;
    bool previousInside = sampleDensity(glm::vec3(x, topY, z)) > ISO;
    if (previousInside) { // the column is filled to the ceiling — the surface is at the top boundary
        outHeight = topY;
        outNormal = glm::vec3(0.f, 1.f, 0.f);
        return true;
    }
    for (float y = topY - step; y >= -step; y -= step) {
        const bool inside = sampleDensity(glm::vec3(x, std::max(y, 0.f), z)) > ISO;
        if (!previousInside && inside) {
            float above = previousY;
            float below = std::max(y, 0.f);
            for (int i = 0; i < 12; i++) {
                const float middle = (above + below) * 0.5f;
                if (sampleDensity(glm::vec3(x, middle, z)) > ISO) {
                    below = middle;
                } else {
                    above = middle;
                }
            }
            outHeight = (above + below) * 0.5f;
            outNormal = fieldNormal(glm::vec3(x, outHeight, z));
            return true;
        }
        previousInside = inside;
        previousY = std::max(y, 0.f);
    }
    return false;
}

bool TerrainData3D::raycast(
    const glm::vec3 &origin,
    const glm::vec3 &direction,
    float maxDistance,
    glm::vec3 &outPoint,
    glm::vec3 &outNormal
) const {
    if (!isValid() || maxDistance <= 0.f) { return false; }
    const glm::vec3 dir = glm::normalize(direction);

    // Clip of the ray segment against the terrain AABB (slightly expanded — the surface is near
    // the boundary).
    const glm::vec3 boxMin(-cellSize);
    const glm::vec3 boxMax(
        cellsX * cellSize + cellSize, cellsY * cellSize + cellSize, cellsZ * cellSize + cellSize
    );
    float tEnter = 0.f;
    float tExit = maxDistance;
    for (int axis = 0; axis < 3; axis++) {
        if (std::abs(dir[axis]) < 1e-8f) {
            if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) { return false; }
            continue;
        }
        float t0 = (boxMin[axis] - origin[axis]) / dir[axis];
        float t1 = (boxMax[axis] - origin[axis]) / dir[axis];
        if (t0 > t1) { std::swap(t0, t1); }
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
    }
    if (tEnter > tExit) { return false; }

    const float step = cellSize * 0.5f;
    float previousDistance = tEnter;
    bool previousInside = sampleDensity(origin + dir * tEnter) > ISO;
    if (previousInside) { return false; } // start inside the solid

    for (float distance = tEnter + step; distance <= tExit + step; distance += step) {
        const float clamped = std::min(distance, tExit);
        const bool inside = sampleDensity(origin + dir * clamped) > ISO;
        if (!previousInside && inside) {
            float nearDistance = previousDistance;
            float farDistance = clamped;
            for (int i = 0; i < 16; i++) {
                const float middle = (nearDistance + farDistance) * 0.5f;
                if (sampleDensity(origin + dir * middle) > ISO) {
                    farDistance = middle;
                } else {
                    nearDistance = middle;
                }
            }
            outPoint = origin + dir * (nearDistance + farDistance) * 0.5f;
            outNormal = fieldNormal(outPoint);
            return true;
        }
        previousInside = inside;
        previousDistance = clamped;
        if (clamped >= tExit) { break; }
    }
    return false;
}

} // namespace Panda

#include "Panda/Terrain/TerrainData2D.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

uint16_t TerrainData2D::encodeHeight(float height) {
    const float raw = (height + HEIGHT_BIAS) / HEIGHT_QUANTUM;
    return static_cast<uint16_t>(std::clamp(raw + 0.5f, 0.f, 65535.f));
}

TerrainData2D TerrainData2D::makeFlat(uint32_t cellsX, uint32_t cellsZ, float cellSize) {
    TerrainData2D data;
    data.cellsX = cellsX;
    data.cellsZ = cellsZ;
    data.cellSize = cellSize;
    data.heights.assign(static_cast<size_t>(cellsX + 1) * (cellsZ + 1), encodeHeight(0.f));
    data.layers.assign(
        static_cast<size_t>(data.layerCountX()) * data.layerCountZ(), 1
    ); // layer 1 — grass
    data.floraMask.assign(
        static_cast<size_t>(data.floraCountX()) * data.floraCountZ() * FLORA_MASK_CHANNELS, 0
    );
    return data;
}

bool TerrainData2D::isValid() const {
    const size_t vertexCount = static_cast<size_t>(vertexCountX()) * vertexCountZ();
    const size_t layerCount = static_cast<size_t>(layerCountX()) * layerCountZ();
    const size_t floraCount = static_cast<size_t>(floraCountX()) * floraCountZ();
    return cellsX > 0 && cellsZ > 0 && cellSize > 0.f && layerShift <= MAX_MAP_SHIFT &&
           floraShift <= MAX_MAP_SHIFT && heights.size() == vertexCount &&
           layers.size() == layerCount &&
           floraMask.size() == floraCount * FLORA_MASK_CHANNELS;
}

uint32_t TerrainData2D::chunksX() const {
    return (cellsX + CHUNK_CELLS - 1) / CHUNK_CELLS;
}

uint32_t TerrainData2D::chunksZ() const {
    return (cellsZ + CHUNK_CELLS - 1) / CHUNK_CELLS;
}

float TerrainData2D::heightAt(uint32_t x, uint32_t z) const {
    x = std::min(x, cellsX);
    z = std::min(z, cellsZ);
    return decodeHeight(heights[static_cast<size_t>(z) * vertexCountX() + x]);
}

void TerrainData2D::setHeightAt(uint32_t x, uint32_t z, float height) {
    if (x > cellsX || z > cellsZ) { return; }
    heights[static_cast<size_t>(z) * vertexCountX() + x] = encodeHeight(height);
}

uint8_t TerrainData2D::layerAt(uint32_t x, uint32_t z) const {
    // Vertex coordinate → node of the coarser layer grid.
    const uint32_t mapX = std::min(std::min(x, cellsX) >> layerShift, layerCountX() - 1);
    const uint32_t mapZ = std::min(std::min(z, cellsZ) >> layerShift, layerCountZ() - 1);
    return layers[static_cast<size_t>(mapZ) * layerCountX() + mapX];
}

void TerrainData2D::setLayerAt(uint32_t x, uint32_t z, uint8_t layer) {
    if (x > cellsX || z > cellsZ || layer < 1 || layer > LAYER_COUNT) { return; }
    const uint32_t mapX = std::min(x >> layerShift, layerCountX() - 1);
    const uint32_t mapZ = std::min(z >> layerShift, layerCountZ() - 1);
    layers[static_cast<size_t>(mapZ) * layerCountX() + mapX] = layer;
}

uint8_t TerrainData2D::floraMaskAt(uint32_t x, uint32_t z, uint32_t channel) const {
    const uint32_t mapX = std::min(std::min(x, cellsX) >> floraShift, floraCountX() - 1);
    const uint32_t mapZ = std::min(std::min(z, cellsZ) >> floraShift, floraCountZ() - 1);
    channel = std::min(channel, FLORA_MASK_CHANNELS - 1);
    return floraMask[(static_cast<size_t>(mapZ) * floraCountX() + mapX) * FLORA_MASK_CHANNELS + channel];
}

void TerrainData2D::setFloraMaskAt(uint32_t x, uint32_t z, uint32_t channel, uint8_t value) {
    if (x > cellsX || z > cellsZ || channel >= FLORA_MASK_CHANNELS) { return; }
    const uint32_t mapX = std::min(x >> floraShift, floraCountX() - 1);
    const uint32_t mapZ = std::min(z >> floraShift, floraCountZ() - 1);
    floraMask[(static_cast<size_t>(mapZ) * floraCountX() + mapX) * FLORA_MASK_CHANNELS + channel] =
        value;
}

glm::vec3 TerrainData2D::normalAt(uint32_t x, uint32_t z) const {
    // Central differences; one-sided at the edges (the indices are clamped in heightAt).
    const uint32_t leftX = x > 0 ? x - 1 : 0;
    const uint32_t rightX = std::min(x + 1, cellsX);
    const uint32_t backZ = z > 0 ? z - 1 : 0;
    const uint32_t frontZ = std::min(z + 1, cellsZ);
    const float dx = (heightAt(rightX, z) - heightAt(leftX, z)) / ((rightX - leftX) * cellSize);
    const float dz = (heightAt(x, frontZ) - heightAt(x, backZ)) / ((frontZ - backZ) * cellSize);
    return glm::normalize(glm::vec3(-dx, 1.f, -dz));
}

bool TerrainData2D::raycast(
    const glm::vec3 &origin,
    const glm::vec3 &direction,
    float maxDistance,
    glm::vec3 &outPoint,
    glm::vec3 &outNormal
) const {
    if (!isValid() || maxDistance <= 0.f) { return false; }
    const glm::vec3 dir = glm::normalize(direction);
    const float step = cellSize * 0.5f;

    auto heightUnder = [this](const glm::vec3 &point, float &outHeight) {
        glm::vec3 unusedNormal;
        return sampleSurface(point.x, point.z, outHeight, unusedNormal);
    };

    float height;
    float previousDistance = 0.f;
    bool previousAbove = !heightUnder(origin, height) || origin.y > height;
    if (!previousAbove) { return false; } // start below the surface — not counted as a hit

    for (float distance = step; distance <= maxDistance; distance += step) {
        const glm::vec3 point = origin + dir * distance;
        const bool inside = heightUnder(point, height);
        const bool above = !inside || point.y > height;
        if (previousAbove && !above) {
            // The intersection is between previousDistance and distance — refined by bisection.
            float nearDistance = previousDistance;
            float farDistance = distance;
            for (int i = 0; i < 16; i++) {
                const float middle = (nearDistance + farDistance) * 0.5f;
                const glm::vec3 middlePoint = origin + dir * middle;
                if (!heightUnder(middlePoint, height) || middlePoint.y > height) {
                    nearDistance = middle;
                } else {
                    farDistance = middle;
                }
            }
            const glm::vec3 hit = origin + dir * farDistance;
            float hitHeight;
            glm::vec3 hitNormal;
            if (!sampleSurface(hit.x, hit.z, hitHeight, hitNormal)) { return false; }
            outPoint = glm::vec3(hit.x, hitHeight, hit.z);
            outNormal = hitNormal;
            return true;
        }
        previousAbove = above;
        previousDistance = distance;
    }
    return false;
}

bool TerrainData2D::sampleSurface(float x, float z, float &outHeight, glm::vec3 &outNormal) const {
    if (!isValid()) { return false; }
    const float gridX = x / cellSize;
    const float gridZ = z / cellSize;
    if (gridX < 0.f || gridZ < 0.f || gridX > cellsX || gridZ > cellsZ) { return false; }

    const uint32_t cellX = std::min(static_cast<uint32_t>(gridX), cellsX - 1);
    const uint32_t cellZ = std::min(static_cast<uint32_t>(gridZ), cellsZ - 1);
    const float tx = gridX - cellX;
    const float tz = gridZ - cellZ;

    const float h00 = heightAt(cellX, cellZ);
    const float h10 = heightAt(cellX + 1, cellZ);
    const float h01 = heightAt(cellX, cellZ + 1);
    const float h11 = heightAt(cellX + 1, cellZ + 1);
    outHeight = glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);

    const glm::vec3 n00 = normalAt(cellX, cellZ);
    const glm::vec3 n10 = normalAt(cellX + 1, cellZ);
    const glm::vec3 n01 = normalAt(cellX, cellZ + 1);
    const glm::vec3 n11 = normalAt(cellX + 1, cellZ + 1);
    outNormal = glm::normalize(glm::mix(glm::mix(n00, n10, tx), glm::mix(n01, n11, tx), tz));
    return true;
}

} // namespace Panda

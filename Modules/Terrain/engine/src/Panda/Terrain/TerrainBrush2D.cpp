#include "Panda/Terrain/TerrainBrush2D.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

void TerrainBrushRegion::extendVertex(uint32_t x, uint32_t z) {
    if (!touched) {
        touched = true;
        minX = maxX = x;
        minZ = maxZ = z;
        return;
    }
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minZ = std::min(minZ, z);
    maxZ = std::max(maxZ, z);
}

void TerrainBrushRegion::extendRegion(const TerrainBrushRegion &other) {
    if (!other.touched) { return; }
    extendVertex(other.minX, other.minZ);
    extendVertex(other.maxX, other.maxZ);
}

namespace TerrainBrush2D {

    namespace {

        float falloffWeight(float distance, float radius) {
            const float t = std::clamp(1.f - distance / radius, 0.f, 1.f);
            return t * t * (3.f - 2.f * t); // smoothstep towards the edge of the circle
        }

        // Average of the 4 neighbors (the clamp at the grid edges is inside heightAt).
        float neighborAverage(const TerrainData2D &data, uint32_t x, uint32_t z) {
            const float left = data.heightAt(x > 0 ? x - 1 : 0, z);
            const float right = data.heightAt(x + 1, z);
            const float back = data.heightAt(x, z > 0 ? z - 1 : 0);
            const float front = data.heightAt(x, z + 1);
            return (left + right + back + front) * 0.25f;
        }

    } // namespace

    TerrainBrushRegion applyTick(
        TerrainData2D &data,
        TerrainBrushMode2D mode,
        bool subtract,
        float centerX,
        float centerZ,
        float radius,
        float strength,
        float targetHeight,
        float deltaTime
    ) {
        TerrainBrushRegion region;
        if (!data.isValid() || radius <= 0.f || strength <= 0.f || deltaTime <= 0.f) {
            return region;
        }
        const float gridCenterX = centerX / data.cellSize;
        const float gridCenterZ = centerZ / data.cellSize;
        const float gridRadius = radius / data.cellSize;
        const int minX = std::max(0, static_cast<int>(std::floor(gridCenterX - gridRadius)));
        const int minZ = std::max(0, static_cast<int>(std::floor(gridCenterZ - gridRadius)));
        const int maxX = std::min(
            static_cast<int>(data.cellsX), static_cast<int>(std::ceil(gridCenterX + gridRadius))
        );
        const int maxZ = std::min(
            static_cast<int>(data.cellsZ), static_cast<int>(std::ceil(gridCenterZ + gridRadius))
        );
        // Convergence rate of Flatten/Smooth: the fraction of the way to the target per second.
        const float convergence = std::min(1.f, strength * deltaTime);

        for (int z = minZ; z <= maxZ; z++) {
            for (int x = minX; x <= maxX; x++) {
                const float dx = (x - gridCenterX) * data.cellSize;
                const float dz = (z - gridCenterZ) * data.cellSize;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > radius) { continue; }
                const float weight = falloffWeight(distance, radius);
                if (weight <= 0.f) { continue; }

                const uint32_t vertexX = static_cast<uint32_t>(x);
                const uint32_t vertexZ = static_cast<uint32_t>(z);
                const float height = data.heightAt(vertexX, vertexZ);
                float newHeight = height;
                switch (mode) {
                    case TerrainBrushMode2D::Raise: {
                        const float amount = strength * deltaTime * weight;
                        newHeight = height + (subtract ? -amount : amount);
                        break;
                    }
                    case TerrainBrushMode2D::Flatten:
                        newHeight = height + (targetHeight - height) * convergence * weight;
                        break;
                    case TerrainBrushMode2D::Smooth:
                        newHeight = height +
                                    (neighborAverage(data, vertexX, vertexZ) - height) *
                                        convergence * weight;
                        break;
                    case TerrainBrushMode2D::Paint:
                        break; // layers — applyPaint (the caller branches on the mode)
                }
                if (newHeight == height) { continue; }
                data.setHeightAt(vertexX, vertexZ, newHeight);
                region.extendVertex(vertexX, vertexZ);
            }
        }
        return region;
    }

    namespace {

        // Deterministic hash of the vertex and the tick → [0, 1): the spray brush does not need a
        // global random, and repeating the stroke over the same vertices gives a different pattern
        // thanks to the seed.
        float hash01(uint32_t x, uint32_t z, uint32_t seed) {
            uint32_t h = x * 374761393u + z * 668265263u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.f;
        }

    } // namespace

    TerrainBrushRegion applyFloraPaint(
        TerrainData2D &data,
        float centerX,
        float centerZ,
        float radius,
        uint32_t channel,
        bool erase,
        float strength,
        float deltaTime
    ) {
        TerrainBrushRegion region;
        if (!data.isValid() || radius <= 0.f || channel >= TerrainData2D::FLORA_MASK_CHANNELS ||
            strength <= 0.f || deltaTime <= 0.f) {
            return region;
        }
        const float gridCenterX = centerX / data.cellSize;
        const float gridCenterZ = centerZ / data.cellSize;
        const float gridRadius = radius / data.cellSize;
        const int minX = std::max(0, static_cast<int>(std::floor(gridCenterX - gridRadius)));
        const int minZ = std::max(0, static_cast<int>(std::floor(gridCenterZ - gridRadius)));
        const int maxX = std::min(
            static_cast<int>(data.cellsX), static_cast<int>(std::ceil(gridCenterX + gridRadius))
        );
        const int maxZ = std::min(
            static_cast<int>(data.cellsZ), static_cast<int>(std::ceil(gridCenterZ + gridRadius))
        );
        // Painting rate: strength = fractions of full density per second (channel scale 0..255).
        const float flow = strength * deltaTime * 255.f;
        for (int z = minZ; z <= maxZ; z++) {
            for (int x = minX; x <= maxX; x++) {
                const float dx = (x - gridCenterX) * data.cellSize;
                const float dz = (z - gridCenterZ) * data.cellSize;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > radius) { continue; }
                const float delta = flow * falloffWeight(distance, radius);
                if (delta <= 0.f) { continue; }
                const uint32_t vertexX = static_cast<uint32_t>(x);
                const uint32_t vertexZ = static_cast<uint32_t>(z);
                const int value = data.floraMaskAt(vertexX, vertexZ, channel);
                // A slow flow does not stall on rounding: at least 1 quantum per tick.
                const int step = std::max(1, static_cast<int>(delta + 0.5f));
                const int newValue =
                    std::clamp(erase ? value - step : value + step, 0, 255);
                if (newValue == value) { continue; }
                data.setFloraMaskAt(vertexX, vertexZ, channel, static_cast<uint8_t>(newValue));
                region.extendVertex(vertexX, vertexZ);
            }
        }
        return region;
    }

    TerrainBrushRegion applyPaint(
        TerrainData2D &data,
        float centerX,
        float centerZ,
        float radius,
        uint8_t layer,
        float strength,
        float deltaTime,
        uint32_t seed
    ) {
        TerrainBrushRegion region;
        if (!data.isValid() || radius <= 0.f || layer < 1 || layer > TerrainData2D::LAYER_COUNT ||
            strength <= 0.f || deltaTime <= 0.f) {
            return region;
        }
        const float gridCenterX = centerX / data.cellSize;
        const float gridCenterZ = centerZ / data.cellSize;
        const float gridRadius = radius / data.cellSize;
        const int minX = std::max(0, static_cast<int>(std::floor(gridCenterX - gridRadius)));
        const int minZ = std::max(0, static_cast<int>(std::floor(gridCenterZ - gridRadius)));
        const int maxX = std::min(
            static_cast<int>(data.cellsX), static_cast<int>(std::ceil(gridCenterX + gridRadius))
        );
        const int maxZ = std::min(
            static_cast<int>(data.cellsZ), static_cast<int>(std::ceil(gridCenterZ + gridRadius))
        );
        const float coverage = std::min(1.f, strength * deltaTime);
        for (int z = minZ; z <= maxZ; z++) {
            for (int x = minX; x <= maxX; x++) {
                const float dx = (x - gridCenterX) * data.cellSize;
                const float dz = (z - gridCenterZ) * data.cellSize;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > radius) { continue; }
                const uint32_t vertexX = static_cast<uint32_t>(x);
                const uint32_t vertexZ = static_cast<uint32_t>(z);
                if (data.layerAt(vertexX, vertexZ) == layer) { continue; }
                const float probability = coverage * falloffWeight(distance, radius);
                if (hash01(vertexX, vertexZ, seed) >= probability) { continue; }
                data.setLayerAt(vertexX, vertexZ, layer);
                region.extendVertex(vertexX, vertexZ);
            }
        }
        return region;
    }

    const char *modeName(TerrainBrushMode2D mode) {
        switch (mode) {
            case TerrainBrushMode2D::Raise: return "Raise";
            case TerrainBrushMode2D::Flatten: return "Flatten";
            case TerrainBrushMode2D::Smooth: return "Smooth";
            case TerrainBrushMode2D::Paint: return "Paint";
        }
        return "";
    }

} // namespace TerrainBrush2D

} // namespace Panda

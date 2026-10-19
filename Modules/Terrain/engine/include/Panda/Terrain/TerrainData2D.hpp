#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Panda {

// Heightmap data of the terrain: heights at the vertices of a regular grid of cellsX × cellsZ
// cells. Pure data without the GPU — serialized into the .terrain asset, the meshes are built by
// TerrainMesher2D.
struct TerrainData2D {
    static constexpr uint32_t CHUNK_CELLS = 32; // chunk cells along one axis

    // The cap on the grid size is SHARED by resize, writing and validation on read. If they
    // diverge, the editor silently creates an asset that does not pass loading: the terrain and
    // the flora disappear as a whole after a restart (render() exits on !isValid()).
    // Memory cost (v5): uint16 heights on the vertex grid + the layer/flora mask on their own
    // coarse grids; for 4096 this is ~36 MB of RAM and ~4 MB of VRAM (the splatmap on the layer
    // grid). A larger map should be made by increasing cellSize, not the number of cells.
    static constexpr uint32_t MAX_CELLS = 4096;

    static constexpr uint32_t LAYER_COUNT = 4; // material layers (the weights are the vertex color)

    static constexpr uint32_t FLORA_MASK_CHANNELS = 4; // painted flora layers (RGBA mask)

    // Heights are uint16 with a fixed quantum (not float): the relief does not need the precision
    // of float32, and on the vertex grid this is half of the asset's whole memory. The quantum is
    // a power of two, so the round trip is exact and does not accumulate error on brush edits.
    // The bias gives relief below zero: the range is -64 .. +959.98 m, the step is 1.56 cm.
    static constexpr float HEIGHT_QUANTUM = 1.f / 64.f;
    static constexpr float HEIGHT_BIAS = 64.f;

    // The layer and flora density maps live on THEIR OWN, coarser grids (a log2 reduction
    // relative to the vertex grid) — like Control/Detail Resolution in Unity. There is no point in
    // keeping them on the vertex grid: the layer weights are blurred 3×3 and sampled bilinearly
    // anyway, and the flora density all the more so. It is this, not the bit depth, that gives the
    // main saving: at 2048 cells the layers go 16.8 → 0.26 MB, the flora mask 67 → 0.26 MB.
    // They are written into the asset, but for now are edited only here (there is no setting in
    // the inspector).
    static constexpr uint8_t DEFAULT_LAYER_SHIFT = 2; // 1/4 of the vertex grid
    static constexpr uint8_t DEFAULT_FLORA_SHIFT = 3; // 1/8 of the vertex grid
    static constexpr uint8_t MAX_MAP_SHIFT = 5;

    uint32_t cellsX = 0;
    uint32_t cellsZ = 0;
    float cellSize = 1.f;
    uint8_t layerShift = DEFAULT_LAYER_SHIFT;
    uint8_t floraShift = DEFAULT_FLORA_SHIFT;
    std::vector<uint16_t> heights; // (cellsX+1) * (cellsZ+1), row-major along Z
    std::vector<uint8_t> layers;   // material layer 1..LAYER_COUNT on the layerCount* grid
    // Painted flora placement: density 0..255, channel = painted layer, the floraCount* grid.
    std::vector<uint8_t> floraMask; // RGBA (4 bytes per node of its own grid)

    static constexpr float decodeHeight(uint16_t raw) {
        return raw * HEIGHT_QUANTUM - HEIGHT_BIAS;
    }
    static uint16_t encodeHeight(float height);

    static TerrainData2D makeFlat(uint32_t cellsX, uint32_t cellsZ, float cellSize);

    bool isValid() const;
    uint32_t vertexCountX() const {
        return cellsX + 1;
    }
    uint32_t vertexCountZ() const {
        return cellsZ + 1;
    }
    // Sizes of the layer/flora maps and conversion of a vertex coordinate into a node of their
    // own grid.
    static uint32_t mapCount(uint32_t cells, uint8_t shift) {
        return (cells >> shift) + 1;
    }
    uint32_t layerCountX() const {
        return mapCount(cellsX, layerShift);
    }
    uint32_t layerCountZ() const {
        return mapCount(cellsZ, layerShift);
    }
    uint32_t floraCountX() const {
        return mapCount(cellsX, floraShift);
    }
    uint32_t floraCountZ() const {
        return mapCount(cellsZ, floraShift);
    }
    uint32_t chunksX() const;
    uint32_t chunksZ() const;

    // UV in the splatmap for a vertex coordinate (fractional — LOD tiles step over nodes).
    // The map is on the layer grid, so the coordinate is divided by its step. A single point: if
    // the chunk mesher and the tile mesher compute it differently, the layers will shift at the
    // LOD seam.
    glm::vec2 splatUV(float vertexX, float vertexZ) const {
        const float scale = static_cast<float>(1u << layerShift);
        return glm::vec2(
            (vertexX / scale + 0.5f) / static_cast<float>(layerCountX()),
            (vertexZ / scale + 0.5f) / static_cast<float>(layerCountZ())
        );
    }

    float heightAt(uint32_t x, uint32_t z) const;
    void setHeightAt(uint32_t x, uint32_t z, float height);
    uint8_t layerAt(uint32_t x, uint32_t z) const;
    void setLayerAt(uint32_t x, uint32_t z, uint8_t layer);
    uint8_t floraMaskAt(uint32_t x, uint32_t z, uint32_t channel) const;
    void setFloraMaskAt(uint32_t x, uint32_t z, uint32_t channel, uint8_t value);
    glm::vec3 normalAt(uint32_t x, uint32_t z) const;

    // Bilinear sample of the surface in terrain local coordinates (meters).
    // false — the point is outside the grid.
    bool sampleSurface(float x, float z, float &outHeight, glm::vec3 &outNormal) const;

    // Intersection of a ray with the surface (terrain local coordinates): a march along the ray
    // with a step of ~half a cell + bisection. false — the ray did not hit the terrain within
    // maxDistance.
    bool raycast(
        const glm::vec3 &origin,
        const glm::vec3 &direction,
        float maxDistance,
        glm::vec3 &outPoint,
        glm::vec3 &outNormal
    ) const;
};

} // namespace Panda

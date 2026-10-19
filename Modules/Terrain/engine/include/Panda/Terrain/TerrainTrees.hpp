#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Panda {

// Terrain tree: a point in terrain local space (meters). The height is not stored — the root
// settles on the relief when the flora block is built, so sculpting the relief carries the
// trees along.
struct TreeInstance {
    float x = 0.f;
    float z = 0.f;
    float yaw = 0.f;    // radians around Y
    float scale = 1.f;  // size multiplier of the layer mesh
    uint32_t layer = 0; // TreeLayer::id

    bool operator==(const TreeInstance &other) const {
        return x == other.x && z == other.z && yaw == other.yaw && scale == other.scale &&
               layer == other.layer;
    }
};

// All trees of a terrain (part of .terrain). Storage — by cells of CELL_SIZE × CELL_SIZE m:
// a cell coincides with a flora block, so a block is built by reading a single cell, while the
// brush and the minimum spacing check look only at the neighboring ones. Pure data, no GPU.
class TerrainTrees {
public:
    static constexpr float CELL_SIZE = 16.f;

    size_t count() const {
        return m_count;
    }
    bool empty() const {
        return m_count == 0;
    }

    void clear();
    void add(const TreeInstance &tree);
    // Remove exactly this point (bit for bit); false — there is no such point.
    bool remove(const TreeInstance &tree);
    // Remove trees within a circle; layer = 0 — of any layer. The removed ones are appended to
    // outRemoved.
    size_t removeInRadius(
        float x, float z, float radius, uint32_t layer, std::vector<TreeInstance> *outRemoved = nullptr
    );
    // Remove all trees of a layer (layer deletion, layer re-scattering). The removed ones go to
    // outRemoved.
    size_t removeLayer(uint32_t layer, std::vector<TreeInstance> *outRemoved = nullptr);
    // Remove trees outside [0, extentX) × [0, extentZ) (terrain resize).
    void clip(float extentX, float extentZ);

    // Whether there is a tree closer than distance to the point (minimum spacing of the
    // brush/scattering).
    bool anyWithin(float x, float z, float distance) const;

    // Trees in the rectangle [x0, x1) × [z0, z1).
    template<typename Visitor>
    void forEachIn(float x0, float z0, float x1, float z1, Visitor &&visit) const {
        const int cellX0 = cellOf(x0);
        const int cellZ0 = cellOf(z0);
        const int cellX1 = cellOf(x1);
        const int cellZ1 = cellOf(z1);
        for (int cellZ = cellZ0; cellZ <= cellZ1; cellZ++) {
            for (int cellX = cellX0; cellX <= cellX1; cellX++) {
                const auto it = m_cells.find(keyOf(cellX, cellZ));
                if (it == m_cells.end()) { continue; }
                for (const TreeInstance &tree : it->second) {
                    if (tree.x >= x0 && tree.x < x1 && tree.z >= z0 && tree.z < z1) { visit(tree); }
                }
            }
        }
    }

    // All trees (the traversal order is unspecified).
    template<typename Visitor>
    void forEach(Visitor &&visit) const {
        for (const auto &cell : m_cells) {
            for (const TreeInstance &tree : cell.second) { visit(tree); }
        }
    }

    // Flat list in a stable order (cells by key, inside a cell — insertion order):
    // serialization yields identical bytes for identical content.
    std::vector<TreeInstance> flatten() const;
    void assign(const std::vector<TreeInstance> &trees);

private:
    static int cellOf(float coordinate);
    static uint64_t keyOf(int cellX, int cellZ) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cellZ)) << 32) |
               static_cast<uint32_t>(cellX);
    }

    std::unordered_map<uint64_t, std::vector<TreeInstance>> m_cells;
    size_t m_count = 0;
};

} // namespace Panda

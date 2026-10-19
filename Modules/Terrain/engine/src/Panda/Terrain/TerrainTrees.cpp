#include "Panda/Terrain/TerrainTrees.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

int TerrainTrees::cellOf(float coordinate) {
    return static_cast<int>(std::floor(coordinate / CELL_SIZE));
}

void TerrainTrees::clear() {
    m_cells.clear();
    m_count = 0;
}

void TerrainTrees::add(const TreeInstance &tree) {
    m_cells[keyOf(cellOf(tree.x), cellOf(tree.z))].push_back(tree);
    m_count++;
}

bool TerrainTrees::remove(const TreeInstance &tree) {
    const auto it = m_cells.find(keyOf(cellOf(tree.x), cellOf(tree.z)));
    if (it == m_cells.end()) { return false; }
    std::vector<TreeInstance> &cell = it->second;
    const auto found = std::find(cell.begin(), cell.end(), tree);
    if (found == cell.end()) { return false; }
    cell.erase(found);
    if (cell.empty()) { m_cells.erase(it); }
    m_count--;
    return true;
}

size_t TerrainTrees::removeInRadius(
    float x, float z, float radius, uint32_t layer, std::vector<TreeInstance> *outRemoved
) {
    size_t removed = 0;
    const float radiusSquared = radius * radius;
    for (int cellZ = cellOf(z - radius); cellZ <= cellOf(z + radius); cellZ++) {
        for (int cellX = cellOf(x - radius); cellX <= cellOf(x + radius); cellX++) {
            const auto it = m_cells.find(keyOf(cellX, cellZ));
            if (it == m_cells.end()) { continue; }
            std::vector<TreeInstance> &cell = it->second;
            const auto kept = std::stable_partition(cell.begin(), cell.end(), [&](const TreeInstance &tree) {
                if (layer != 0 && tree.layer != layer) { return true; }
                const float dx = tree.x - x;
                const float dz = tree.z - z;
                return dx * dx + dz * dz > radiusSquared;
            });
            removed += static_cast<size_t>(cell.end() - kept);
            if (outRemoved) { outRemoved->insert(outRemoved->end(), kept, cell.end()); }
            cell.erase(kept, cell.end());
            if (cell.empty()) { m_cells.erase(it); }
        }
    }
    m_count -= removed;
    return removed;
}

size_t TerrainTrees::removeLayer(uint32_t layer, std::vector<TreeInstance> *outRemoved) {
    size_t removed = 0;
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        std::vector<TreeInstance> &cell = it->second;
        const auto kept = std::stable_partition(cell.begin(), cell.end(), [layer](const TreeInstance &tree) {
            return tree.layer != layer;
        });
        removed += static_cast<size_t>(cell.end() - kept);
        if (outRemoved) { outRemoved->insert(outRemoved->end(), kept, cell.end()); }
        cell.erase(kept, cell.end());
        it = cell.empty() ? m_cells.erase(it) : std::next(it);
    }
    m_count -= removed;
    return removed;
}

void TerrainTrees::clip(float extentX, float extentZ) {
    size_t removed = 0;
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        std::vector<TreeInstance> &cell = it->second;
        const auto kept = std::stable_partition(cell.begin(), cell.end(), [&](const TreeInstance &tree) {
            return tree.x >= 0.f && tree.z >= 0.f && tree.x < extentX && tree.z < extentZ;
        });
        removed += static_cast<size_t>(cell.end() - kept);
        cell.erase(kept, cell.end());
        it = cell.empty() ? m_cells.erase(it) : std::next(it);
    }
    m_count -= removed;
}

bool TerrainTrees::anyWithin(float x, float z, float distance) const {
    const float distanceSquared = distance * distance;
    for (int cellZ = cellOf(z - distance); cellZ <= cellOf(z + distance); cellZ++) {
        for (int cellX = cellOf(x - distance); cellX <= cellOf(x + distance); cellX++) {
            const auto it = m_cells.find(keyOf(cellX, cellZ));
            if (it == m_cells.end()) { continue; }
            for (const TreeInstance &tree : it->second) {
                const float dx = tree.x - x;
                const float dz = tree.z - z;
                if (dx * dx + dz * dz < distanceSquared) { return true; }
            }
        }
    }
    return false;
}

std::vector<TreeInstance> TerrainTrees::flatten() const {
    std::vector<uint64_t> keys;
    keys.reserve(m_cells.size());
    for (const auto &entry : m_cells) { keys.push_back(entry.first); }
    std::sort(keys.begin(), keys.end());
    std::vector<TreeInstance> trees;
    trees.reserve(m_count);
    for (uint64_t key : keys) {
        const std::vector<TreeInstance> &cell = m_cells.at(key);
        trees.insert(trees.end(), cell.begin(), cell.end());
    }
    return trees;
}

void TerrainTrees::assign(const std::vector<TreeInstance> &trees) {
    clear();
    for (const TreeInstance &tree : trees) { add(tree); }
}

} // namespace Panda

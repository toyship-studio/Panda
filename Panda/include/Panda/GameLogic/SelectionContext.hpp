#pragma once

#ifdef PND_EDITOR

#    include "Entity.hpp"
#    include "World.hpp"

#    include <cstdint>
#    include <unordered_set>

namespace Panda {

class SelectionContext final {
public:
    SelectionContext() = delete;

    static std::unordered_set<UUID> getSelectedEntities() {
        return s_selectedEntities;
    }

    static std::unordered_set<path_t> getSelectedFiles() {
        return s_selectedFiles;
    }

    static std::unordered_set<AssetId> getSelectedAssets() {
        return s_selectedAssets;
    }

    static std::unordered_set<UUID> getManipulatingEntities() {
        return s_manipulatingEntities;
    }

    static bool isEntitySelected(UUID id) {
        return s_selectedEntities.contains(id);
    }

    static bool isFileSelected(const path_t &path) {
        return s_selectedFiles.contains(path);
    }

    static bool isAssetSelected(AssetId assetId) {
        return assetId && s_selectedAssets.contains(assetId);
    }

    static void addSelectedFile(const path_t &path) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        PND_ASSERT(assetHandler != nullptr, "ASSET HANDLER IS NOT INITIALIZED");
        AssetId assetId = assetHandler->getAssetId(path);
        s_selectedFiles.emplace(path);
        if (assetId) { s_selectedAssets.emplace(assetId); }
    }

    static void addSelectedFiles(const std::unordered_set<path_t> &paths) {
        for (auto &path : paths) {
            addSelectedFile(path);
        }
    }

    static void addSelectedAsset(AssetId assetId) {
        if (!assetId) { return; }
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        PND_ASSERT(assetHandler != nullptr, "ASSET HANDLER IS NOT INITIALIZED");
        s_selectedAssets.emplace(assetId);
        addSelectedFiles(assetHandler->getAssetPaths(assetId));
    }

    static void removeSelectedFile(const path_t &path) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        PND_ASSERT(assetHandler != nullptr, "ASSET HANDLER IS NOT INITIALIZED");
        AssetId assetId = assetHandler->getAssetId(path);
        s_selectedFiles.erase(path);
        if (assetId && !hasSelectedFileForAsset(assetId)) { s_selectedAssets.erase(assetId); }
    }

    static void addSelectedEntity(UUID id, bool needToCalculateMedian = true) {
        if (isEntitySelected(id)) { return; }
        World *world = GameContext::getCurrentWorld();
        if (!world) { return; }
        if (!world->isValid(id)) { return; }
        s_selectedEntities.insert(id);
        ++s_entitySelectionVersion;
        Entity entity = world->getById(id);
        //  Below, avoid the transformation being applied to the children as well when the parent
        //  is moved.
        std::unordered_set<UUID> manipulating;
        for (auto manipulatingId : s_manipulatingEntities) {
            Entity manipulatingEntity = world->getById(manipulatingId);
            if (entity.isDescendantOf(manipulatingEntity)) {
                // If the selected item is in the hierarchy of already selected ones - do nothing.
                return;
            } else if (entity.isAncestorOf(manipulatingEntity)) {
                // If the selected item is a parent - it becomes the one that manipulates.
                continue;
            }
            manipulating.insert(manipulatingId);
        }
        manipulating.insert(id);
        s_manipulatingEntities = manipulating;
        if (needToCalculateMedian) { updateValues(); }
    }

    static void addSelectedEntities(const std::unordered_set<UUID> &ids) {
        for (auto id : ids) {
            addSelectedEntity(id, false);
        }
        updateValues();
    }

    static void removeSelectedAsset(UUID assetId) {
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        PND_ASSERT(assetHandler != nullptr, "ASSET HANDLER IS NOT INITIALIZED");
        if (assetId) {
            std::unordered_set<path_t> assetPaths = assetHandler->getAssetPaths(assetId);
            for (auto &assetPath : assetPaths) {
                s_selectedFiles.erase(assetPath);
            }
            s_selectedAssets.erase(assetId);
        }
    }

    static void removeSelectedEntity(UUID id, bool needToCalculateMedian = true) {
        if (s_selectedEntities.erase(id) > 0) { ++s_entitySelectionVersion; }
        s_manipulatingEntities.erase(id);
        if (needToCalculateMedian) { updateValues(); }
    }

    static void removeSelectedEntities(const std::unordered_set<UUID> &ids) {
        for (auto id : ids) {
            removeSelectedEntity(id, false);
        }
        updateValues();
    }

    static void unselectAll() {
        s_selectedAssets.clear();
        s_selectedFiles.clear();
        if (!s_selectedEntities.empty()) { ++s_entitySelectionVersion; }
        s_selectedEntities.clear();
        s_manipulatingEntities.clear();
        resetValues();
    }

    static void unselectAllFiles() {
        s_selectedAssets.clear();
        s_selectedFiles.clear();
    }

    static int selectedFilesCount() {
        return s_selectedFiles.size();
    }

    static int selectedAssetsCount() {
        return static_cast<int>(s_selectedAssets.size());
    }

    static int selectedEntitiesCount() {
        return s_selectedEntities.size();
    }

    static bool isEmpty() {
        return s_selectedEntities.empty();
    }

    // Monotonic counter of ENTITY SELECTION changes (insert/erase/clear). The editor compares it
    // once per frame instead of copying and diffing the set — this way a selection made in the
    // viewport reaches the panels cheaply.
    static std::uint64_t getEntitySelectionVersion() {
        return s_entitySelectionVersion;
    }

    // Monotonic counter of recomputations of the selection's TRANSFORM (updateValues): bumped on a
    // gizmo drag and on an edit from the Inspector. The editor polls it to synchronize the
    // Transform fields and the gizmo position both ways.
    static std::uint64_t getTransformVersion() {
        return s_transformVersion;
    }

    static glm::mat4 getMedianMatrix() {
        return s_medianMatrix;
    }

    static glm::vec3 getMedianPosition() {
        return s_medianPosition;
    }

    static void resetValues() {
        s_medianPosition = glm::vec3(0.0f);
        s_medianScale = glm::vec3(1.0f);
        s_medianRotation = glm::quat();
        s_medianMatrix = glm::mat4(1.f);
    }

    static void updateValues() {
        ++s_transformVersion; // selection transform recomputed — the editor polls this for Inspector↔gizmo sync
        resetValues();
        if (s_selectedEntities.empty()) { return; }
        World *world = GameContext::getCurrentWorld();
        if (!world) { return; }
        auto it = s_selectedEntities.begin();
        UUID id = *it;
        Entity entity = world->getById(id);
        auto transformComponent = entity.calculateWorldSpaceTransform();
        s_medianPosition = transformComponent.getPosition();
        s_medianScale = transformComponent.getScale();
        s_medianRotation = transformComponent.getRotation();
        ++it;
        while (it != s_selectedEntities.end()) {
            id = *it;
            entity = world->getById(id);
            transformComponent = entity.calculateWorldSpaceTransform();
            // auto transformComponent = entity.getTransform();
            s_medianPosition += transformComponent.getPosition();
            s_medianScale += transformComponent.getScale();
            s_medianRotation = glm::slerp(s_medianRotation, transformComponent.getRotation(), 0.5f);
            // if (glm::dot(s_medianRotation, transformComponent.getRotation()) < 0.0f) {
            //     s_medianRotation -= transformComponent.getRotation();
            // } else {
            //     s_medianRotation += transformComponent.getRotation();
            // }
            s_medianRotation = glm::normalize(s_medianRotation);
            ++it;
        }
        s_medianPosition /= s_selectedEntities.size();
        s_medianScale /= s_selectedEntities.size();
        s_medianMatrix = glm::translate(glm::mat4(1.0f), s_medianPosition) * glm::toMat4(s_medianRotation) *
                         glm::scale(glm::mat4(1.0f), s_medianScale);
    }

private:
    static bool hasSelectedFileForAsset(AssetId assetId) {
        if (!assetId) { return false; }
        AssetHandler *assetHandler = GameContext::getAssetHandler();
        PND_ASSERT(assetHandler != nullptr, "ASSET HANDLER IS NOT INITIALIZED");
        std::unordered_set<path_t> assetPaths = assetHandler->getAssetPaths(assetId);
        for (const auto &assetPath : assetPaths) {
            if (s_selectedFiles.contains(assetPath)) { return true; }
        }
        return false;
    }

    static std::unordered_set<AssetId> s_selectedAssets;
    static std::unordered_set<path_t> s_selectedFiles;
    static std::unordered_set<UUID> s_selectedEntities;
    static std::unordered_set<UUID> s_manipulatingEntities;
    static glm::vec3 s_medianPosition;
    static glm::vec3 s_medianScale;
    static glm::quat s_medianRotation;
    static glm::mat4 s_medianMatrix;
    static std::uint64_t s_entitySelectionVersion;
    static std::uint64_t s_transformVersion;
};

} // namespace Panda

#endif

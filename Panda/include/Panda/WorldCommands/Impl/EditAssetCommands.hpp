#pragma once

#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/PostprocessingAsset.hpp"
#include "Panda/WorldCommands/WorldCommand.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace Panda {

// Undo of form edits of asset documents. Continuous gestures (typing a number character by
// character, a drag in the color picker) emit a tick on every change — the series is fused
// into ONE command by merging (canMerge/merge) within the merge window (withinMergeWindow);
// undo/redo seal the top (seal), so that a continuation of the gesture after a rollback becomes
// a new history step.

// Edit of a single material field. A series of ticks of one field = one command.
class EditMaterialFieldCommand : public WorldCommand {
public:
    EditMaterialFieldCommand(
        AssetId materialId, std::string fieldName, MaterialFieldValue before, MaterialFieldValue after
    )
        : m_materialId(materialId)
        , m_fieldName(std::move(fieldName))
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_lastEdit(std::chrono::steady_clock::now()) {}

    bool execute() override {
        return apply(m_after);
    }
    bool undo() override {
        return apply(m_before);
    }
    bool canMerge(WorldCommand &command) override {
        auto *other = dynamic_cast<EditMaterialFieldCommand *>(&command);
        return other && !m_sealed && other->m_materialId == m_materialId &&
               other->m_fieldName == m_fieldName && withinMergeWindow(m_lastEdit);
    }
    void merge(WorldCommand &command) override {
        m_after = static_cast<EditMaterialFieldCommand &>(command).m_after;
        m_lastEdit = std::chrono::steady_clock::now();
    }
    void seal() override {
        m_sealed = true;
    }

private:
    bool apply(const MaterialFieldValue &value) {
        AssetRef<MaterialAsset> material{m_materialId};
        MaterialAsset *asset = material.get();
        if (asset == nullptr || !asset->getInputs().hasField(m_fieldName)) { return false; }
        asset->setFieldValue(m_fieldName.c_str(), value);
        asset->setChanged(true);
        return true;
    }

    AssetId m_materialId;
    std::string m_fieldName;
    MaterialFieldValue m_before;
    MaterialFieldValue m_after;
    std::chrono::steady_clock::time_point m_lastEdit;
    bool m_sealed = false;
};

// Edit of the postprocessing pass list (before/after as a whole). The edits are discrete
// (toggle/combobox/asset/add/remove) — they are not merged: every click = its own history step.
class EditPostprocessingCommand : public WorldCommand {
public:
    EditPostprocessingCommand(
        AssetId assetId, std::vector<PostprocessingPass> before, std::vector<PostprocessingPass> after
    )
        : m_assetId(assetId)
        , m_before(std::move(before))
        , m_after(std::move(after)) {}

    bool execute() override {
        return apply(m_after);
    }
    bool undo() override {
        return apply(m_before);
    }

private:
    bool apply(const std::vector<PostprocessingPass> &passes) {
        AssetRef<PostprocessingAsset> asset{m_assetId};
        PostprocessingAsset *postprocessing = asset.get();
        if (postprocessing == nullptr) { return false; }
        postprocessing->getPasses() = passes;
        postprocessing->setChanged(true);
        return true;
    }

    AssetId m_assetId;
    std::vector<PostprocessingPass> m_before;
    std::vector<PostprocessingPass> m_after;
};

} // namespace Panda

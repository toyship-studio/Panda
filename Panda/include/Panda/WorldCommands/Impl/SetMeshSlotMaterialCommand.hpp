#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/WorldCommands/WorldCommand.hpp"

namespace Panda {

// Replacement of a mesh slot material (the mesh tab). The document is the mesh asset: the
// command lives in its undo stack; apply edits the working copy of the slots in the loaded
// asset (sets changed), the sidecar file is written on Save.
class SetMeshSlotMaterialCommand : public WorldCommand {
public:
    SetMeshSlotMaterialCommand(AssetId meshId, size_t slot, AssetId before, AssetId after)
        : m_meshId(meshId)
        , m_slot(slot)
        , m_before(before)
        , m_after(after) {}

    bool execute() override {
        return apply(m_after);
    }

    bool undo() override {
        return apply(m_before);
    }

private:
    bool apply(AssetId materialId) {
        AssetRef<ImportedMeshAsset> mesh{m_meshId};
        ImportedMeshAsset *asset = mesh.get();
        if (asset == nullptr) { return false; }
        asset->setSlotMaterial(m_slot, AssetRef<Asset>(materialId));
        return true;
    }

    AssetId m_meshId;
    size_t m_slot;
    AssetId m_before;
    AssetId m_after;
};

} // namespace Panda

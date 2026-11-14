#pragma once

#include "Project/Documents/FileDocument.hpp"

#include <Panda/Assets/Base/AssetInfo.hpp>

namespace Panda {

class AssetHandlerEditor;

// A loaded asset and its file. The asset database owns the asset (a texture the renderer uses
// exists without a document), so this document is a view made when it is needed
// (FileDocuments::list, FileDocuments::assetFile): the asset documents do the saving, the
// database the reloading. The disk state is taken once, when the view is made.
class AssetFile final : public FileDocument {
public:
    AssetFile(AssetHandlerEditor &handler, AssetId id, bool changedOnDisk);

    AssetId id() const {
        return m_id;
    }

    std::string title() const override;
    path_t path() const override;
    bool isDirty() const override;
    bool isChangedOnDisk() const override {
        return m_changedOnDisk;
    }
    bool save() override;
    bool reload() override;

private:
    AssetHandlerEditor &m_handler;
    AssetId m_id;
    bool m_changedOnDisk;
};

} // namespace Panda

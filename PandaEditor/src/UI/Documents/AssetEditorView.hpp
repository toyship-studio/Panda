#pragma once

#include "Preview/AssetPreview.hpp" // lease of a large asset preview

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetEditor (the editors of the asset kinds)
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Panda/Assets/Base/Asset.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace Editor {

// VIEW: the form of an asset tab — the header (name, type and size on disk), the large preview
// (a ThumbnailsManager lease: a material is a sphere, an image is the texture itself) and the
// editor of the asset kind from EditorExtensions::assetKinds (the core kinds register theirs the
// same way as the modules: CoreAssetEditors). Several assets — a summary only.
class AssetEditorView final : public EditorView {
public:
    // onEdited — the document was changed from the form: the tab pins itself, the dirty dot follows.
    explicit AssetEditorView(std::function<void()> onEdited);

    // Input: the targets of the tab (by id; the data is resolved live).
    void setAssets(const std::vector<Panda::AssetId> &assets);
    // The form rereads the document (undo/redo, a reload); the editor instance survives.
    void rebuild();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void update(double deltaTime) override; // called only while the tab is visible: the live preview

    void bind();
    void rebuildProperties();
    void requestRebuild(); // at the start of the next tick: a form is not torn down from its own control
    void applyPreview();

    std::function<void()> m_onEdited;
    std::vector<Panda::AssetId> m_assets;
    // The editor of the kind: an instance per asset, survives form rebuilds (the state of lists
    // lives in it), dies when the asset or the tab changes.
    std::unique_ptr<AssetEditor> m_editor;
    Panda::AssetId m_editorAsset = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // lifetime token for deferred tasks

    std::shared_ptr<PandaUI::ScrollView> m_body;
    std::shared_ptr<PandaUI::Label> m_name;
    std::shared_ptr<PandaUI::Label> m_subtitle;
    std::shared_ptr<PandaUI::ImageView> m_preview; // large preview: material sphere / image
    std::shared_ptr<Panda::AssetPreview> m_previewLease;
    PandaUI::TextureHandle m_previewShown; // which lease texture is currently in the ImageView
    std::shared_ptr<PandaUI::Panel> m_props;
};

} // namespace Editor

#pragma once

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetEditor (the editors of the asset kinds)
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Panda/Assets/Base/Asset.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace Editor {

class AssetPreviewView;

// VIEW: the form of an asset tab — the header (name, type and size on disk), the large preview
// (AssetPreviewView: a little viewport on a material or a model, the image of a texture) and the
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
    // The preview of the form, for what drives it without gestures (EditorMcp).
    AssetPreviewView *previewView() const {
        return m_preview.get();
    }

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void bind();
    void rebuildProperties();
    void requestRebuild(); // at the start of the next tick: a form is not torn down from its own control

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
    std::shared_ptr<AssetPreviewView> m_preview; // the large preview: a little viewport or an image
    std::shared_ptr<PandaUI::Panel> m_props;
};

} // namespace Editor

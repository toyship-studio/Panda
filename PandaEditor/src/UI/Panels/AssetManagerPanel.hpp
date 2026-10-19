#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId

#include <memory>
#include <vector>

namespace Editor {

class AssetManagerOutput {
public:
    virtual ~AssetManagerOutput() = default;
    virtual void assetManagerDidSelectAssets(const std::vector<Panda::AssetId> &ids) = 0;
    virtual void assetManagerDidRequestDelete(const std::vector<Panda::AssetId> &ids) = 0;
    virtual void assetManagerDidRequestOpenWorld(Panda::AssetId id) = 0; // open the .world as the current world
};

// A table of all project assets — reads the engine registry directly (rows are resolved live).
class AssetManagerPanelView final : public EditorView {
public:
    explicit AssetManagerPanelView(AssetManagerOutput *output);

    void refresh();                                              // re-read the registry (sorted list of ids)
    void setSelectedAssetIds(const std::vector<Panda::AssetId> &ids); // programmatic highlight (ContentBrowser sync)

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool keyDown(PandaUI::KeyEvent &event) override;
    bool pointerDown(PandaUI::PointerEvent &event) override;

    void emitSelection();
    void deleteSelection();
    bool isWorldAsset(int index) const;
    void showContextMenu(int index, PandaUI::Point position);
    void updateStatus();
    std::vector<Panda::AssetId> selectedAssetIds() const;

    AssetManagerOutput *m_output;
    std::vector<Panda::AssetId> m_ids; // sorted registry ids (row source)
    std::shared_ptr<PandaUI::CollectionView> m_table;
    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace Editor

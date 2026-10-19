#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <string>

namespace Editor {

// Composite asset reference field: [preview+name | replace | reset].
// A click on the value — SELECTION of the asset in the editor (its editor tab), NOT a reset; a click
// on an empty field and the "replace" button open the Asset Picker; reset — by a separate button,
// only when there is a value. D&D acceptance of the "asset.<kind>" payload. Preview: the host's
// thumbnail (materials, textures), otherwise — the type icon. The picker and thumbnails — EditorHost.
class AssetWell final : public PandaUI::Panel {
public:
    AssetWell(
        uint32_t initial, std::string kind, std::function<std::string(uint32_t)> nameFor,
        std::function<void(uint32_t)> onChange, std::function<void(uint32_t)> onSelect
    );

    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;

protected:
    void themeChanged() override;
    // The thumbnail is baked asynchronously (one per frame): its appearance is awaited and it is
    // picked up in place, without rebuilding the form. Cheaper than a subscription — one cache check
    // until the preview is in place.
    void draw(float offsetX, float offsetY) override;

private:
    class MainZone;

    void setValue(uint32_t id); // from drop/the picker: refresh + onChange
    void openPicker();
    void refresh();    // name, preview, visibility of the reset button
    void applyStyle();
    bool wantsThumbnail() const; // a kind for which the manager can draw a preview

    uint32_t m_assetId;
    std::string m_kind;
    std::function<std::string(uint32_t)> m_nameFor;
    std::function<void(uint32_t)> m_onChange;
    std::function<void(uint32_t)> m_onSelect;
    std::shared_ptr<MainZone> m_main;
    std::shared_ptr<PandaUI::ImageView> m_preview;
    std::shared_ptr<PandaUI::Label> m_label;
    std::shared_ptr<PandaUI::View> m_replaceButton;
    std::shared_ptr<PandaUI::View> m_clearButton;
    bool m_dropHovered = false;
    bool m_thumbnailShown = false; // the preview is already in place — polling in draw stops
};

} // namespace Editor

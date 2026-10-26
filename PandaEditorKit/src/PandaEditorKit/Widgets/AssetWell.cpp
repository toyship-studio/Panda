#include "PandaEditorKit/Widgets/AssetWell.hpp"

#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/Icons.hpp"

#include <Foundation/GraphicsAPI.hpp>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float WELL_HEIGHT = 28.f;  // = the height of text fields
    constexpr float PREVIEW_SIZE = 20.f; // square slot of the thumbnail/type icon in the field

    const char *iconForKind(const std::string &kind) {
        if (kind == "texture") { return Ic::FilePng; }
        if (kind == "material") { return Ic::Material; }
        if (kind == "mesh") { return Ic::Mesh; }
        if (kind == "postfx") { return Ic::Sprite; }
        if (kind == "font") { return Ic::Font; }
        return Ic::FileGeneric;
    }

} // namespace

// The "preview + name" zone: a click selects the asset (an empty field — opens the picker). A separate
// Control so that the replace/reset buttons are not part of the clickable area of the value.
class AssetWell::MainZone final : public Control {
public:
    explicit MainZone(AssetWell *owner)
        : m_owner(owner) {
        setCursor(Cursor::PointingHand);
        layout().setFlexGrow(1.f);
        layout().setFlexShrink(1.f);
        layout().setHeight(Length::points(WELL_HEIGHT));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, 7.f);
        layout().setGap(6.f);
        surface().setCornerRadius(Radius::sm);
        surface().setBorderWidth(1.f);
    }

protected:
    void performPrimaryAction() override {
        m_owner->m_assetId ? m_owner->m_onSelect(m_owner->m_assetId) : m_owner->openPicker();
    }

private:
    AssetWell *m_owner;
};

AssetWell::AssetWell(
    uint32_t initial, std::string kind, std::function<std::string(uint32_t)> nameFor,
    std::function<void(uint32_t)> onChange, std::function<void(uint32_t)> onSelect
)
    : m_assetId(initial)
    , m_kind(std::move(kind))
    , m_nameFor(std::move(nameFor))
    , m_onChange(std::move(onChange))
    , m_onSelect(std::move(onSelect)) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexGrow(1.f);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setGap(3.f);

    m_main = std::make_shared<MainZone>(this);
    m_preview = std::make_shared<ImageView>();
    m_preview->setContentMode(ImageContentMode::Fit);
    m_preview->setUserInteractionEnabled(false);
    m_preview->layout().setSize({PREVIEW_SIZE, PREVIEW_SIZE});
    m_preview->layout().setFlexShrink(0.f);
    m_main->addSubview(m_preview);
    m_label = std::make_shared<Label>("");
    m_label->setFont(Font(15.f));
    m_label->layout().setFlexShrink(1.f);
    m_main->addSubview(m_label);
    addSubview(m_main);

    auto replace = std::make_shared<IconButton>(Ic::More, Ic::RowSize, WELL_HEIGHT);
    replace->setCornerRadius(Radius::sm);
    replace->setTooltip("Replace Asset");
    replace->setOnClick([this] { openPicker(); });
    m_replaceButton = replace;
    addSubview(replace);

    auto clear = std::make_shared<IconButton>(Ic::Clear, Ic::RowSize, WELL_HEIGHT);
    clear->setCornerRadius(Radius::sm);
    clear->setTooltip("Clear Asset");
    clear->setOnClick([this] { setValue(0); });
    m_clearButton = clear;
    addSubview(clear);

    applyStyle();
    refresh();
}

bool AssetWell::canAcceptDrop(const DragInfo &drag) {
    return drag.payload.type == "asset." + m_kind && !drag.payload.items.empty();
}

void AssetWell::dragEntered(const DragInfo &) {
    m_dropHovered = true;
    applyStyle();
}

void AssetWell::dragExited() {
    m_dropHovered = false;
    applyStyle();
}

bool AssetWell::performDrop(const DragInfo &drag) {
    m_dropHovered = false;
    applyStyle();
    setValue(static_cast<uint32_t>(drag.payload.items.front()));
    return true;
}

void AssetWell::setValue(uint32_t id) {
    m_assetId = id;
    refresh();
    if (m_onChange) { m_onChange(m_assetId); }
}

void AssetWell::openPicker() {
    Context *context = getContext();
    if (!context) { return; }
    const Rect frame = m_main->getWorldFrame();
    editorHost().pickAsset(
        context, Point(frame.origin.x, frame.origin.y + frame.size.height), m_kind,
        [this](uint32_t id) { setValue(id); }
    );
}

void AssetWell::refresh() {
    const std::string name = (m_assetId && m_nameFor) ? m_nameFor(m_assetId) : std::string();
    m_label->setText(name.empty() ? ("None (" + m_kind + ")") : name);
    m_label->setTextRole(name.empty() ? TextRole::Muted : TextRole::Primary);

    // Preview: the host's thumbnail (FBO → flip on GL); none — the type icon. The first request puts
    // the id into the render queue, the finished thumbnail is picked up by draw (see below).
    TextureHandle thumbnail;
    if (wantsThumbnail()) { thumbnail = editorHost().assetThumbnail(Panda::AssetId(m_assetId)); }
    m_thumbnailShown = thumbnail.isValid();
    if (thumbnail.isValid()) {
        m_preview->setTexture(thumbnail);
        m_preview->setFlipVertical(Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft);
        m_preview->setTintColor(Color(0xFFFFFFFF));
    } else {
        m_preview->setTexture(icons().get(iconForKind(m_kind)));
        m_preview->setFlipVertical(false);
        m_preview->setTintColor(m_assetId ? theme().textPrimary : theme().textMuted);
    }

    const bool hasValue = m_assetId != 0;
    m_clearButton->setHidden(!hasValue);
    m_clearButton->layout().setDisplay(hasValue ? Display::Flex : Display::None);
}

bool AssetWell::wantsThumbnail() const {
    // Kinds the manager can draw: a material — as a sphere, a texture — by a blit into a square.
    return m_assetId != 0 && (m_kind == "material" || m_kind == "texture");
}

void AssetWell::draw(float offsetX, float offsetY) {
    // The thumbnail may have finished baking after the form was built — pick it up and stop polling.
    if (!m_thumbnailShown && wantsThumbnail()) {
        const TextureHandle ready = editorHost().assetThumbnail(Panda::AssetId(m_assetId));
        if (ready.isValid()) {
            m_thumbnailShown = true;
            m_preview->setTexture(ready);
            m_preview->setFlipVertical(Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft);
            m_preview->setTintColor(Color(0xFFFFFFFF));
        }
    }
    Panel::draw(offsetX, offsetY);
}

void AssetWell::applyStyle() {
    const Theme &t = theme();
    m_main->setBackgroundColor(m_dropHovered ? t.controlHover : t.control);
    m_main->surface().setBorderColor(m_dropHovered ? t.accent : t.border);
}

void AssetWell::themeChanged() {
    applyStyle();
    refresh(); // the tint of the type icon depends on the theme
}

} // namespace Editor

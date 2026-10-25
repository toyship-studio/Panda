#include "UI/Documents/AssetEditorView.hpp"

#include "Preview/ThumbnailsManager.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "UI/EditorModelHelper.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/GameLogic/GameContext.hpp>

#include <cstdio>
#include <string>

namespace Editor {

using namespace PandaUI;

namespace {

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // File size for the asset header: "512 B" / "12.4 KB" / "1.2 MB".
    std::string formatByteSize(uint64_t bytes) {
        if (bytes < 1024) { return std::to_string(bytes) + " B"; }
        const char *units[] = {"KB", "MB", "GB"};
        double value = static_cast<double>(bytes);
        int unit = -1;
        while (value >= 1024.0 && unit < 2) {
            value /= 1024.0;
            unit++;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
        return buffer;
    }

} // namespace

AssetEditorView::AssetEditorView(std::function<void()> onEdited)
    : m_onEdited(std::move(onEdited)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void AssetEditorView::addSubviews() {
    m_body = std::make_shared<ScrollView>();
    m_body->setScrollDirection(ScrollDirection::Vertical);
    m_body->layout().setFlexGrow(1.f);
    m_body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_body->getContentView()->layout().setPadding(Edge::Horizontal, 12.f);
    m_body->getContentView()->layout().setPadding(Edge::Vertical, 12.f);
    m_body->getContentView()->layout().setGap(6.f);
    addSubview(m_body);

    m_name = std::make_shared<Label>("");
    m_name->setFont(Typo::Body.withWeight(FontWeight::Medium));
    m_body->addContentSubview(m_name);

    m_subtitle = std::make_shared<Label>("");
    m_subtitle->setFont(Typo::ValueSmall);
    m_subtitle->setTextRole(TextRole::Muted);
    m_subtitle->setNumberOfLines(0);
    m_body->addContentSubview(m_subtitle);

    // Large asset preview: a material is a sphere from an FBO, an image is the texture itself on a
    // plate.
    m_preview = std::make_shared<ImageView>();
    m_preview->setContentMode(ImageContentMode::Fit);
    m_preview->setUserInteractionEnabled(false);
    m_preview->layout().setSize({176.f, 176.f});
    m_preview->layout().setAlignSelf(Align::Center);
    m_preview->layout().setMargin(Edge::Vertical, 6.f);
    m_preview->setHidden(true);
    m_preview->layout().setDisplay(Display::None);
    m_body->addContentSubview(m_preview);

    m_props = std::make_shared<Panel>();
    m_props->setBackgroundColor(Color(0x00000000));
    m_props->layout().setFlexDirection(FlexDirection::Column);
    m_props->layout().setGap(6.f);
    m_body->addContentSubview(m_props);
}

void AssetEditorView::setAssets(const std::vector<Panda::AssetId> &assets) {
    m_assets = assets;
    // The editor of the kind lives as long as the form shows THIS asset.
    if (m_editor && !(assets.size() == 1 && assets.front() == m_editorAsset)) {
        m_editor.reset();
        m_editorAsset = 0;
    }
    // The large preview is a lease of the form: every tab has its own, the previous one is
    // released by the replacement.
    auto *thumbnails = Panda::ThumbnailsManager::shared();
    m_previewLease = thumbnails && assets.size() == 1 ? thumbnails->leasePreview(assets.front()) : nullptr;
    applyPreview();
    bind();
}

void AssetEditorView::rebuild() {
    bind();
}

void AssetEditorView::bind() {
    Panda::AssetHandlerEditor *handler = assetHandler();
    if (m_assets.size() == 1 && handler) {
        const auto &registry = handler->getRegistry();
        auto it = registry.find(m_assets.front());
        if (it != registry.end()) {
            m_name->setText(handler->getAssetName(it->first));
            // Subtitle: type · size of the files in the project (in the future — also the size in
            // the runtime Asset Package: important for the game size).
            const uint64_t sizeOnDisk = handler->assetSizeOnDisk(m_assets.front());
            const std::string type = assetTypeString(it->second.type);
            m_subtitle->setText(sizeOnDisk > 0 ? type + " · " + formatByteSize(sizeOnDisk) : type);
            rebuildProperties();
            return;
        }
    }
    m_name->setText(m_assets.size() == 1 ? "Asset" : std::to_string(m_assets.size()) + " assets selected");
    m_subtitle->setText("-");
    m_props->removeAllSubviews();
    setNeedsLayout();
}

void AssetEditorView::rebuildProperties() {
    m_props->removeAllSubviews();
    Panda::AssetHandlerEditor *handler = assetHandler();
    if (!handler || m_assets.size() != 1) { return; }
    const Panda::AssetId id = m_assets.front();
    const auto &registry = handler->getRegistry();
    auto it = registry.find(id);
    if (it == registry.end()) { return; }
    const std::string kind = assetTypeString(it->second.type);
    const AssetKindUi *ui = editorExtensions().assetKinds.find(kind);
    if (ui == nullptr || ui->createEditor == nullptr) {
        // A kind without an editor (a world, a shader, a file): the header says what it is.
        auto label = std::make_shared<Label>(kind == "scene" ? "World / Scene" : kind);
        label->setFont(Font(15.f));
        label->setTextRole(TextRole::Secondary);
        auto section = std::make_shared<CollapsibleSection>(kind == "scene" ? "Scene (read-only)" : "Asset");
        section->addContent(std::make_shared<PropertyRow>("Type", label, false));
        m_props->addSubview(section);
        setNeedsLayout();
        return;
    }
    if (!m_editor || m_editorAsset != id) {
        AssetEditorContext context;
        context.asset = id;
        context.rebuild = [this] { requestRebuild(); };
        context.edited = [this] {
            if (m_onEdited) { m_onEdited(); }
        };
        m_editor = ui->createEditor(context);
        m_editorAsset = id;
    }
    m_editor->build(*m_props);
    setNeedsLayout();
}

void AssetEditorView::requestRebuild() {
    Context *context = getContext();
    if (!context) { return; }
    // The form may not live until the next tick (the tab was closed) — a lifetime token instead
    // of this.
    context->postDeferred([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (alive.lock()) { rebuildProperties(); }
    });
}

void AssetEditorView::applyPreview() {
    const Panda::AssetPreview none;
    const Panda::AssetPreview &preview = m_previewLease ? *m_previewLease : none;
    m_previewShown = preview.texture;
    m_preview->setTexture(preview.texture);
    m_preview->setFlipVertical(preview.flipVertical);
    // Image plate: a square with a border (like the Content Browser preview slots). A material is
    // a sphere on a transparent background, a plate would only get in its way.
    m_preview->setBackgroundColor(preview.plate ? theme().input : Color(0x00000000));
    m_preview->surface().setCornerRadius(preview.plate ? Radius::md : Radius::none);
    m_preview->surface().setBorderWidth(preview.plate ? 1.f : 0.f);
    m_preview->surface().setBorderColor(preview.plate ? theme().borderMed : Color(0x00000000));
    const bool visible = preview.texture.isValid();
    m_preview->setHidden(!visible);
    m_preview->layout().setDisplay(visible ? Display::Flex : Display::None);
}

void AssetEditorView::update(double) {
    if (!m_previewLease) { return; }
    // The form is on screen (hidden tabs get no update) — the live preview needs to be redrawn.
    m_previewLease->requested = true;
    // The lease texture appears with the first rendered frame and goes away when the project closes.
    if (!(m_previewLease->texture == m_previewShown)) { applyPreview(); }
}

void AssetEditorView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void AssetEditorView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
}

} // namespace Editor

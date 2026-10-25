#include "UI/Panels/AssetManagerPanel.hpp"

#include "UI/EditorModelHelper.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include <PandaUI/PandaUI.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float ROW_HEIGHT = 32.f;
    constexpr float ID_WIDTH = 76.f;
    constexpr float TYPE_WIDTH = 124.f;
    constexpr float STATE_WIDTH = 86.f;
    constexpr float PADDING_X = 10.f;

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    std::shared_ptr<Label> makeCellLabel(TextRole role = TextRole::Primary) {
        auto label = std::make_shared<Label>("");
        label->setFont(Typo::Caption);
        label->setTextRole(role);
        label->setNumberOfLines(1);
        label->setClipsToBounds(true);
        label->layout().setFlexShrink(1.f);
        return label;
    }

    void applyColumnWidth(View &view, float width) {
        view.layout().setWidth(Length::points(width));
        view.layout().setFlexShrink(0.f);
    }

    const char *iconForAssetType(const std::string &type) {
        if (type == "texture") { return Ic::Sprite; }
        if (type == "material") { return Ic::Material; }
        if (type == "mesh") { return Ic::Mesh; }
        if (type == "scene") { return Ic::Globe; }
        return Ic::FileGeneric;
    }

    std::shared_ptr<Panel> makeDotCell(const std::shared_ptr<Panel> &dot) {
        dot->setUserInteractionEnabled(false);
        dot->layout().setSize({8.f, 8.f});
        dot->layout().setFlexShrink(0.f);
        dot->surface().setCornerRadius(4.f);
        auto cell = std::make_shared<Panel>();
        cell->setBackgroundColor(Color(0x00000000));
        cell->layout().setFlexDirection(FlexDirection::Row);
        cell->layout().setJustifyContent(Justify::Center);
        cell->layout().setAlignItems(Align::Center);
        applyColumnWidth(*cell, STATE_WIDTH);
        cell->addSubview(dot);
        return cell;
    }

    class HeaderRow final : public Panel {
    public:
        HeaderRow() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(8.f);
            layout().setPadding(Edge::Horizontal, PADDING_X);
            layout().setHeight(Length::points(30.f));
            addColumn("ID", ID_WIDTH, 0.f, false);
            addColumn("TYPE", TYPE_WIDTH, 0.f, false);
            addColumn("NAME", 0.f, 1.f, false);
            addColumn("FOLDER", 0.f, 1.6f, false);
            addColumn("VALID", STATE_WIDTH, 0.f, true);
            addColumn("LOADED", STATE_WIDTH, 0.f, true);
        }

    private:
        void addColumn(const std::string &text, float width, float grow, bool center) {
            auto label = std::make_shared<Label>(text);
            label->setFont(Typo::Meta);
            label->setTextRole(TextRole::Dim);
            label->setLetterSpacing(0.4f);
            label->setNumberOfLines(1);
            label->setClipsToBounds(true);
            if (center) { label->setTextAlignment(TextAlignment::Center); }
            if (width > 0.f) {
                applyColumnWidth(*label, width);
            } else {
                label->layout().setFlexGrow(grow);
                label->layout().setFlexShrink(1.f);
            }
            addSubview(label);
        }
    };

    class AssetRow final : public Panel {
    public:
        AssetRow()
            : m_id(makeCellLabel(TextRole::Muted))
            , m_typeIcon(std::make_shared<ImageView>())
            , m_type(makeCellLabel(TextRole::Secondary))
            , m_name(makeCellLabel())
            , m_path(makeCellLabel(TextRole::Muted))
            , m_validDot(std::make_shared<Panel>())
            , m_loadedDot(std::make_shared<Panel>()) {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(8.f);
            layout().setPadding(Edge::Horizontal, PADDING_X);

            m_id->setFont(Typo::ValueSmall);
            m_id->setTextColor(Color(0x7D8AA0FF));
            applyColumnWidth(*m_id, ID_WIDTH);
            addSubview(m_id);

            auto typeCell = std::make_shared<Panel>();
            typeCell->setBackgroundColor(Color(0x00000000));
            typeCell->layout().setFlexDirection(FlexDirection::Row);
            typeCell->layout().setAlignItems(Align::Center);
            typeCell->layout().setGap(7.f);
            applyColumnWidth(*typeCell, TYPE_WIDTH);
            m_typeIcon->setContentMode(ImageContentMode::Fit);
            m_typeIcon->setUserInteractionEnabled(false);
            m_typeIcon->layout().setSize({15.f, 15.f});
            m_typeIcon->layout().setFlexShrink(0.f);
            typeCell->addSubview(m_typeIcon);
            typeCell->addSubview(m_type);
            addSubview(typeCell);

            m_name->layout().setFlexGrow(1.f);
            addSubview(m_name);

            m_path->setFont(Typo::ValueSmall);
            m_path->layout().setFlexGrow(1.6f);
            addSubview(m_path);

            addSubview(makeDotCell(m_validDot));
            addSubview(makeDotCell(m_loadedDot));
        }

        void bind(uint32_t id, const std::string &name, const std::string &type, const std::string &folder,
                  bool valid, bool loaded) {
            const Theme &t = theme();
            m_id->setText(std::to_string(id));
            m_typeIcon->setTexture(icons().get(iconForAssetType(type)));
            m_typeIcon->setTintColor(valid ? t.textMuted : t.textDisabled);
            m_type->setText(type);
            m_name->setText(name);
            m_path->setText(folder);
            m_name->setTextRole(valid ? TextRole::Primary : TextRole::Disabled);
            m_validDot->setBackgroundColor(valid ? t.success : t.danger);
            if (loaded) {
                m_loadedDot->setBackgroundColor(t.accent);
                m_loadedDot->surface().setBorderWidth(0.f);
            } else {
                m_loadedDot->setBackgroundColor(Color(0x00000000));
                m_loadedDot->surface().setBorderWidth(1.5f);
                m_loadedDot->surface().setBorderColor(t.textDim);
            }
        }

    private:
        std::shared_ptr<Label> m_id;
        std::shared_ptr<ImageView> m_typeIcon;
        std::shared_ptr<Label> m_type;
        std::shared_ptr<Label> m_name;
        std::shared_ptr<Label> m_path;
        std::shared_ptr<Panel> m_validDot;
        std::shared_ptr<Panel> m_loadedDot;
    };

} // namespace

AssetManagerPanelView::AssetManagerPanelView(AssetManagerOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void AssetManagerPanelView::addSubviews() {
    addSubview(std::make_shared<HeaderRow>());

    m_table = std::make_shared<CollectionView>(std::make_shared<ListCollectionLayout>(ROW_HEIGHT));
    m_table->setSelectionMode(CollectionSelectionMode::Multiple);
    m_table->layout().setFlexGrow(1.f);
    m_table->setBackgroundColor(Color(0x00000000));

    CollectionViewDataSource source;
    source.numberOfItems = [this] { return static_cast<int>(m_ids.size()); };
    source.cellForReuse = [] { return std::make_shared<AssetRow>(); };
    source.configureCell = [this](View &cell, int index) {
        auto *handler = assetHandler();
        if (!handler) { return; }
        const auto &registry = handler->getRegistry();
        auto it = registry.find(m_ids[index]);
        if (it == registry.end()) { return; }
        const Panda::AssetInfo &info = it->second;
        static_cast<AssetRow &>(cell).bind(static_cast<uint32_t>(m_ids[index]), handler->getAssetName(m_ids[index]),
                                           assetTypeString(info.type), assetTypeFolder(info.type),
                                           handler->assetFilesExist(m_ids[index]), handler->isLoaded(m_ids[index]));
    };
    source.canDragItem = [this](int index) { return index >= 0 && index < static_cast<int>(m_ids.size()); };
    source.fillDragPayload = [this](DragPayload &payload, const std::vector<int> &indices) {
        auto *handler = assetHandler();
        if (!handler) { return; }
        const auto &registry = handler->getRegistry();
        std::string kind;
        std::string firstName;
        bool uniform = true;
        for (int i : indices) {
            if (i < 0 || i >= static_cast<int>(m_ids.size())) { continue; }
            auto it = registry.find(m_ids[i]);
            if (it == registry.end()) { continue; }
            const std::string k = assetTypeString(it->second.type);
            if (payload.items.empty()) {
                kind = k;
                firstName = handler->getAssetName(it->first);
            } else if (k != kind) {
                uniform = false;
            }
            payload.items.push_back(static_cast<std::uint64_t>(m_ids[i]));
        }
        payload.type = (uniform && !kind.empty()) ? "asset." + kind : "asset";
        payload.ghostText = payload.items.size() == 1 ? firstName : std::to_string(payload.items.size()) + " assets";
    };
    m_table->setDataSource(source);
    m_table->setOnSelectionChanged([this](CollectionView &) {
        emitSelection();
        updateStatus();
    });
    m_table->setOnContextMenu([this](CollectionView &, int index, Point position) {
        if (index >= 0) { showContextMenu(index, position); }
    });
    // Double click on a world — open it as the current one.
    m_table->setOnActivate([this](int index) {
        if (isWorldAsset(index) && m_output) { m_output->assetManagerDidRequestOpenWorld(m_ids[index]); }
    });
    addSubview(m_table);

    m_status = std::make_shared<Label>("");
    m_status->setFont(Typo::Caption);
    m_status->setTextRole(TextRole::Muted);
    m_status->layout().setPadding(Edge::Horizontal, 12.f);
    m_status->layout().setPadding(Edge::Vertical, 8.f);
    addSubview(m_status);

    updateStatus();
}

void AssetManagerPanelView::refresh() {
    m_ids.clear();
    if (auto *handler = assetHandler()) {
        for (const auto &entry : handler->getRegistry()) { m_ids.push_back(entry.first); }
    }
    std::sort(m_ids.begin(), m_ids.end());
    m_table->reloadData();
    updateStatus();
}

void AssetManagerPanelView::setSelectedAssetIds(const std::vector<Panda::AssetId> &ids) {
    std::unordered_set<std::uint32_t> selected;
    for (Panda::AssetId id : ids) { selected.insert(static_cast<std::uint32_t>(id)); }
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i) {
        if (selected.count(static_cast<std::uint32_t>(m_ids[i]))) { indices.push_back(i); }
    }
    m_table->setSelectedIndices(std::move(indices));
    updateStatus();
}

std::vector<Panda::AssetId> AssetManagerPanelView::selectedAssetIds() const {
    std::vector<Panda::AssetId> ids;
    for (int index : m_table->selectedIndices()) {
        if (index >= 0 && index < static_cast<int>(m_ids.size())) { ids.push_back(m_ids[index]); }
    }
    return ids;
}

void AssetManagerPanelView::emitSelection() {
    if (m_output) { m_output->assetManagerDidSelectAssets(selectedAssetIds()); }
}

void AssetManagerPanelView::deleteSelection() {
    std::vector<Panda::AssetId> ids = selectedAssetIds();
    if (!ids.empty() && m_output) { m_output->assetManagerDidRequestDelete(ids); }
}

bool AssetManagerPanelView::isWorldAsset(int index) const {
    if (index < 0 || index >= static_cast<int>(m_ids.size())) { return false; }
    auto *handler = assetHandler();
    if (!handler) { return false; }
    const auto &registry = handler->getRegistry();
    auto it = registry.find(m_ids[index]);
    return it != registry.end() && it->second.type == Panda::AssetType::WORLD;
}

void AssetManagerPanelView::showContextMenu(int index, Point position) {
    Context *context = getContext();
    if (!context) { return; }
    auto menu = std::make_shared<Menu>();
    if (isWorldAsset(index)) {
        const Panda::AssetId worldId = m_ids[index];
        menu->addItem("Open World", [this, worldId] {
            if (m_output) { m_output->assetManagerDidRequestOpenWorld(worldId); }
        });
    }
    menu->addItem("Delete", [this] { deleteSelection(); });
    auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
    menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
    context->addOverlay(popup);
}

bool AssetManagerPanelView::keyDown(KeyEvent &event) {
    if (event.key == Key::Delete || event.key == Key::Backspace) {
        deleteSelection();
        return true;
    }
    return false;
}

bool AssetManagerPanelView::pointerDown(PointerEvent &) {
    m_table->clearSelection();
    emitSelection();
    updateStatus();
    return true;
}

void AssetManagerPanelView::updateStatus() {
    const int selected = static_cast<int>(m_table ? m_table->selectedIndices().size() : 0);
    const int total = static_cast<int>(m_ids.size());
    int loaded = 0;
    int invalid = 0;
    if (auto *handler = assetHandler()) {
        for (Panda::AssetId id : m_ids) {
            if (handler->isLoaded(id)) { ++loaded; }
            if (!handler->assetFilesExist(id)) { ++invalid; }
        }
    }
    std::string text = "Total: " + std::to_string(total) + "  Loaded: " + std::to_string(loaded);
    if (invalid > 0) { text += "  Missing: " + std::to_string(invalid); }
    if (selected > 0) { text += "  Selected: " + std::to_string(selected); }
    if (m_status) { m_status->setText(text); }
}

void AssetManagerPanelView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void AssetManagerPanelView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
    if (m_table) {
        m_table->setBackgroundColor(theme().windowBackground);
        m_table->reloadData(); // the rows bind their colors from the theme
    }
}

} // namespace Editor

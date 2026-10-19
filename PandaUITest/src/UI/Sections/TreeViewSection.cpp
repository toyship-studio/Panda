#include "UI/Sections/TreeViewSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Demo {

using namespace PandaUI;

namespace {

    DemoSceneNode *asNode(TreeItem item) {
        return static_cast<DemoSceneNode *>(item);
    }

    // Tree row content: a caption, vertically centered. Reused — setText sets everything.
    class TreeCell final : public Panel {
    public:
        TreeCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            m_label = std::make_shared<Label>("");
            m_label->setFont(Font(17.f));
            addSubview(m_label);
        }
        void setText(const std::string &text) {
            m_label->setText(text);
        }

    private:
        std::shared_ptr<Label> m_label;
    };

    std::shared_ptr<DemoSceneNode> makeNode(const std::string &name, DemoSceneNode *parent) {
        auto node = std::make_shared<DemoSceneNode>();
        node->name = name;
        node->parent = parent;
        return node;
    }

} // namespace

TreeViewSection::TreeViewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void TreeViewSection::reparent(DemoSceneNode *node, DemoSceneNode *newParent) {
    if (!node->parent || !newParent) { return; } // the root is not moved
    auto &source = node->parent->children;
    auto it = std::find_if(source.begin(), source.end(), [node](const std::shared_ptr<DemoSceneNode> &candidate) {
        return candidate.get() == node;
    });
    if (it == source.end()) { return; }
    std::shared_ptr<DemoSceneNode> owned = *it;
    source.erase(it);
    node->parent = newParent;
    newParent->children.push_back(owned);
}

void TreeViewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Tree", "TreeDataSource on top of its own model: selection, context menu, drag-and-drop onto nodes."
    ));

    auto example = std::make_shared<DemoExample>(
        "Scene hierarchy", "Arrow — expand; click — select (⌘/Ctrl — add); drag onto a node; right-click — menu."
    );

    // --- model (source of truth) ---
    auto scene = makeNode("Scene", nullptr);
    auto camera = makeNode("Main Camera", scene.get());
    auto light = makeNode("Directional Light", scene.get());
    auto environment = makeNode("Environment", scene.get());
    environment->children.push_back(makeNode("Sky", environment.get()));
    environment->children.push_back(makeNode("Fog", environment.get()));
    auto player = makeNode("Player", scene.get());
    player->children.push_back(makeNode("Mesh", player.get()));
    player->children.push_back(makeNode("Collider", player.get()));
    auto scripts = makeNode("Scripts", player.get());
    scripts->children.push_back(makeNode("Movement", scripts.get()));
    scripts->children.push_back(makeNode("Health", scripts.get()));
    player->children.push_back(scripts);
    scene->children = {camera, light, environment, player};
    m_root = scene;

    // --- the tree as a projection of the model ---
    m_tree = std::make_shared<TreeView>();
    m_tree->layout().setWidth(Length::points(360.f));
    m_tree->layout().setHeight(Length::points(260.f));
    m_tree->surface().setCornerRadius(8.f);
    m_tree->surface().setBorderWidth(1.f);
    m_tree->setSelectionMode(TreeSelectionMode::Multiple);

    TreeDataSource dataSource;
    dataSource.rootItem = [this]() -> TreeItem { return m_root.get(); };
    dataSource.numberOfChildren = [](TreeItem item) { return static_cast<int>(asNode(item)->children.size()); };
    dataSource.childAt = [](TreeItem item, int index) -> TreeItem { return asNode(item)->children[index].get(); };
    dataSource.cellForReuse = [] { return std::make_shared<TreeCell>(); };
    dataSource.configureCell = [](View &cell, TreeItem item, int) { static_cast<TreeCell &>(cell).setText(asNode(item)->name); };
    dataSource.titleForItem = [](TreeItem item) { return asNode(item)->name; };
    m_tree->setDataSource(dataSource);

    m_tree->setExpanded(scene.get(), true);
    m_tree->setExpanded(player.get(), true);

    // drag-and-drop: apply the move to the model, then reload.
    m_tree->setOnMove([this](const std::vector<TreeItem> &items, TreeItem newParent) {
        for (TreeItem item : items) { reparent(asNode(item), asNode(newParent)); }
        m_tree->reloadData();
    });

    m_status = std::make_shared<Label>("Selected: —");
    m_status->setFont(Font(Style::BodyFont));
    m_status->setTextRole(TextRole::Secondary);
    m_tree->setOnSelectionChanged([this](TreeView &) { updateStatus(); });

    // Context menu: add child / rename / delete — everything changes the model + reloadData.
    m_tree->setOnContextMenu([this](TreeView &tree, TreeItem item, Point position) {
        Context *context = tree.getContext();
        if (!context) { return; }
        DemoSceneNode *node = asNode(item);

        auto menu = std::make_shared<Menu>();
        menu->addItem("Add child", [this, node] {
            node->children.push_back(makeNode("New Node", node));
            m_tree->setExpanded(node, true);
            m_tree->reloadData();
        });
        menu->addItem("Rename", [this, node] {
            node->name += " *";
            m_tree->reloadData();
        });
        menu->addSeparator();
        menu->addItem("Delete", [this, node] {
            if (!node->parent) { return; } // the root is not deleted
            auto &siblings = node->parent->children;
            siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [node](const std::shared_ptr<DemoSceneNode> &n) {
                return n.get() == node;
            }), siblings.end());           // remove from the parent (destroys the subtree)
            m_tree->reloadData();
            updateStatus();
        });

        auto popup = std::make_shared<Popup>(menu, Rect(position.x, position.y, 0.f, 0.f));
        menu->setOnItemSelected([popup = popup.get()] { popup->dismiss(); });
        context->addOverlay(popup);
    });

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setGap(8.f);
    column->addSubview(m_tree);
    column->addSubview(m_status);
    example->addContent(column);
    addSubview(example);
}

void TreeViewSection::updateStatus() {
    const std::vector<TreeItem> &items = m_tree->selectedItems();
    if (items.empty()) {
        m_status->setText("Selected: —");
        return;
    }
    std::string text = "Selected (" + std::to_string(items.size()) + "): ";
    for (size_t i = 0; i < items.size(); ++i) { text += (i ? ", " : "") + asNode(items[i])->name; }
    m_status->setText(text);
}

void TreeViewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void TreeViewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
    if (m_tree) {
        m_tree->setBackgroundColor(theme().surfaceVariant);
        m_tree->surface().setBorderColor(theme().border);
    }
}

} // namespace Demo

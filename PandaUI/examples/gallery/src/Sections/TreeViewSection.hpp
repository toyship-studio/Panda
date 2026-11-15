#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>
#include <string>
#include <vector>

namespace PandaUI::Gallery {

// The demo's tree model is the single source of truth (TreeView is merely its projection).
struct DemoTreeNode {
    std::string name;
    std::vector<std::shared_ptr<DemoTreeNode>> children;
    DemoTreeNode *parent = nullptr;
};

// Tree section: a hierarchy via TreeDataSource on top of its own model — expansion, selection,
// context menu, drag-and-drop reparent (the move is applied to the model, then reloadData).
class TreeViewSection final : public DemoComponent {
public:
    TreeViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    void reparent(DemoTreeNode *node, DemoTreeNode *newParent); // move within the model
    void updateStatus();

    std::shared_ptr<DemoTreeNode> m_root; // the only root (source of truth)
    std::shared_ptr<PandaUI::TreeView> m_tree;
    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace PandaUI::Gallery

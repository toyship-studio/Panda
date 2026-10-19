#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Demo {

// The demo's tree model is the single source of truth (TreeView is merely its projection).
struct DemoSceneNode {
    std::string name;
    std::vector<std::shared_ptr<DemoSceneNode>> children;
    DemoSceneNode *parent = nullptr;
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

    void reparent(DemoSceneNode *node, DemoSceneNode *newParent); // move within the model
    void updateStatus();

    std::shared_ptr<DemoSceneNode> m_root; // the only root (source of truth)
    std::shared_ptr<PandaUI::TreeView> m_tree;
    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace Demo

#pragma once

#include "Project/Settings/EditorSettings.hpp" // Panda::DockLayoutState (the layout of the editor)
#include "UI/Widgets/SplitView.hpp"

#include <PandaUI/PandaUI.hpp>

#include <memory>

namespace Editor {

// VIEW: what the tab of a world shows — the editor of the world document: the tree of its entities
// on the left, the Scene in the middle, the inspector of the selected entities on the right (the
// document outline and the inspector of Interface Builder, inside the tab of the file). Everything
// of a world is in its tab: where an entity is, what it is, how it looks — and Save, Undo and the
// dirty dot of the tab are of that one file.
//
// The middle column shows the Scene — the editor camera, the gizmo, the tools — or the Game — the
// frame of the game through the main camera, with the input of the game: Play shows the Game and
// Stop the Scene, and the switch of the toolbar goes from one to the other (Unity's two views in
// the place of one). The game plays in the tab of its world, so the tree and the inspector show
// the playing copy while it runs, and Stop takes back what happened to it.
//
// One instance: it lives in the tab of the active world (WorldTabs moves it there, as it moved the
// one Scene viewport before), and its parts read the world in front. The tree and the inspector are
// hidden and shown by the commands of the Window menu and the buttons of the visibility strip;
// whether they show and the shares of the three columns are kept with the layout of the editor.
class WorldEditorView final : public PandaUI::Panel {
public:
    WorldEditorView(
        std::shared_ptr<PandaUI::View> tree, std::shared_ptr<PandaUI::View> scene, std::shared_ptr<PandaUI::View> game,
        std::shared_ptr<PandaUI::View> inspector
    );

    // The middle column: the Game (true) or the Scene.
    void showGame(bool game);
    bool isShowingGame() const {
        return m_showingGame;
    }

    bool isTreeVisible() const {
        return m_treeVisible;
    }
    bool isInspectorVisible() const {
        return m_inspectorVisible;
    }
    void setTreeVisible(bool visible);
    void setInspectorVisible(bool visible);

    void saveLayout(Panda::DockLayoutState &state) const;
    void applyLayout(const Panda::DockLayoutState &state);

private:
    std::shared_ptr<SplitView> m_columns; // [tree | the Scene or the Game | inspector]
    std::shared_ptr<PandaUI::View> m_scene;
    std::shared_ptr<PandaUI::View> m_game;
    bool m_showingGame = false;
    bool m_treeVisible = true;
    bool m_inspectorVisible = true;
};

} // namespace Editor

#include "UI/Documents/WorldEditorView.hpp"

#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    // The columns and their default shares: in a tab 1080 points wide (a window 1280 wide) the
    // tree is 170 points, the inspector 340 — the width the form of an entity needs for the three
    // numbers of a vector — and the Scene the rest.
    enum Column { TREE = 0, SCENE = 1, INSPECTOR = 2 };
    constexpr float TREE_WEIGHT = 1.f;
    constexpr float SCENE_WEIGHT = 3.35f;
    constexpr float INSPECTOR_WEIGHT = 2.f;

} // namespace

WorldEditorView::WorldEditorView(
    std::shared_ptr<View> tree, std::shared_ptr<View> scene, std::shared_ptr<View> game, std::shared_ptr<View> inspector
)
    : m_scene(std::move(scene))
    , m_game(std::move(game)) {
    setBackgroundColor(ThemeColor::windowBackground);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
    layout().setFlexGrow(1.f);
    // The middle column holds both surfaces; the one that is not shown is out of the layout and
    // hidden, so it is not on screen and does not render (EditorController::viewportSurfaces).
    auto middle = std::make_shared<Panel>();
    middle->setBackgroundColor(ThemeColor::windowBackground);
    middle->layout().setFlexDirection(FlexDirection::Column);
    middle->layout().setAlignItems(Align::Stretch);
    m_scene->layout().setFlexGrow(1.f);
    m_game->layout().setFlexGrow(1.f);
    middle->addSubview(m_scene);
    middle->addSubview(m_game);
    m_columns = std::make_shared<SplitView>(SplitOrientation::Horizontal);
    m_columns->addPane(std::move(tree), TREE_WEIGHT);
    m_columns->addPane(middle, SCENE_WEIGHT);
    m_columns->addPane(std::move(inspector), INSPECTOR_WEIGHT);
    addSubview(m_columns);
    m_showingGame = true; // so that the call below applies
    showGame(false);
}

void WorldEditorView::showGame(bool game) {
    if (m_showingGame == game) { return; }
    m_showingGame = game;
    m_game->setHidden(!game);
    m_game->layout().setDisplay(game ? Display::Flex : Display::None);
    m_scene->setHidden(game);
    m_scene->layout().setDisplay(game ? Display::None : Display::Flex);
    setNeedsLayout();
}

void WorldEditorView::setTreeVisible(bool visible) {
    m_treeVisible = visible;
    m_columns->setPaneVisible(TREE, visible);
}

void WorldEditorView::setInspectorVisible(bool visible) {
    m_inspectorVisible = visible;
    m_columns->setPaneVisible(INSPECTOR, visible);
}

void WorldEditorView::saveLayout(Panda::DockLayoutState &state) const {
    state.worldTreeVisible = m_treeVisible;
    state.worldInspectorVisible = m_inspectorVisible;
    state.worldTreeWeight = m_columns->paneWeight(TREE);
    state.worldSceneWeight = m_columns->paneWeight(SCENE);
    state.worldInspectorWeight = m_columns->paneWeight(INSPECTOR);
}

void WorldEditorView::applyLayout(const Panda::DockLayoutState &state) {
    if (state.worldTreeWeight > 0.f) { m_columns->setPaneWeight(TREE, state.worldTreeWeight); }
    if (state.worldSceneWeight > 0.f) { m_columns->setPaneWeight(SCENE, state.worldSceneWeight); }
    if (state.worldInspectorWeight > 0.f) { m_columns->setPaneWeight(INSPECTOR, state.worldInspectorWeight); }
    setTreeVisible(state.worldTreeVisible);
    setInspectorVisible(state.worldInspectorVisible);
}

} // namespace Editor

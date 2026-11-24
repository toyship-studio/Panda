#pragma once

#include "UI/Panels/UpdateIndicator.hpp"

#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <memory>

namespace Editor {

class Commands;

// Toolbar intents — implemented by the coordinator (like ToolbarOutput in PandaEditor).
class ToolbarOutput {
public:
    virtual ~ToolbarOutput() = default;
    virtual void toolbarDidTapPlay() = 0; // the world starts playing (a copy of it runs)
    virtual void toolbarDidTapStop() = 0; // back to editing
    // The middle of the world tab shows the Game (true: the main camera, the input of the game) or
    // the Scene (the editor camera).
    virtual void toolbarDidSelectView(bool game) = 0;
    virtual void toolbarDidSave() = 0;   // every unsaved document
    virtual void toolbarDidExport() = 0; // the Export window
    virtual void toolbarDidUndo() = 0;
    virtual void toolbarDidRedo() = 0;
    virtual void toolbarDidNavigateBack() = 0;    // selection history: back
    virtual void toolbarDidNavigateForward() = 0; // selection history: forward
    virtual void toolbarDidActivateUpdate() = 0;  // the update indicator was clicked
    virtual void toolbarDidDismissUpdate() = 0;   // the update indicator was dismissed
};

// VIEW: the top toolbar of the editor. On the left — the selection history, Undo and Redo, Save
// All and the export; in the middle — the World | Game switch of the world tab and Play / Stop;
// on the right — the update indicator (UpdateIndicatorView). Intents go out through
// ToolbarOutput. There is no Reload button: the editor takes what changes on disk by itself
// (App/DiskWatch.hpp).
// (The Move/Rotate/Scale gizmo and the camera reset have moved to the viewport overlay toolbar.)
class ToolbarPanelView final : public EditorView, public UpdateIndicatorOutput {
public:
    explicit ToolbarPanelView(ToolbarOutput *output);
    void setKeyHints(const Commands &commands); // the tooltips with the keys of the keymap
    void setUpdateStatus(const UpdateIndicatorModel &model); // the updater state, driven by the coordinator

    // Input: button availability according to the editor state (driven by the coordinator every frame).
    void setCanUndo(bool canUndo);
    void setCanRedo(bool canRedo);
    void setCanNavigate(bool back, bool forward); // selection history buttons
    void setCanPlay(bool canPlay);            // Play (Edit mode)
    void setCanStop(bool canStop);            // Stop (the world is running)
    // What the middle of the world tab shows; the switch is off while no world is open.
    void setShownView(bool game, bool enabled);

    void updateIndicatorDidActivate() override;
    void updateIndicatorDidDismiss() override;

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    ToolbarOutput *m_output;
    std::shared_ptr<IconButton> m_undo;
    std::shared_ptr<IconButton> m_back;
    std::shared_ptr<IconButton> m_forward;
    std::shared_ptr<IconButton> m_redo;
    std::shared_ptr<IconButton> m_save;
    std::shared_ptr<PandaUI::SegmentedControl> m_view; // World | Game
    bool m_settingView = false;                        // the switch is set from the state, not by a click
    std::shared_ptr<IconButton> m_play;
    std::shared_ptr<IconButton> m_stop;
    std::shared_ptr<UpdateIndicatorView> m_update;
};

} // namespace Editor

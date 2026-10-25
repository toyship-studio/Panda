#pragma once

#include "UI/Panels/UpdateIndicator.hpp"

#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <memory>

namespace Editor {

// Scene state — as in PandaEditor: Edit (editing), Play (running), Simulate (physics without logic).
enum class SceneState { Edit, Play, Simulate };

// Toolbar intents — implemented by the coordinator (like ToolbarOutput in PandaEditor).
class ToolbarOutput {
public:
    virtual ~ToolbarOutput() = default;
    virtual void toolbarDidPickSceneState(SceneState state) = 0; // Edit/Play/Simulate
    virtual void toolbarDidTapReload() = 0;                      // sync with the disk (Reload)
    virtual void toolbarDidSave() = 0;                           // save the world
    virtual void toolbarDidExport() = 0;                         // export / cook
    virtual void toolbarDidUndo() = 0;
    virtual void toolbarDidRedo() = 0;
    virtual void toolbarDidNavigateBack() = 0;    // selection history: back
    virtual void toolbarDidNavigateForward() = 0; // selection history: forward
    virtual void toolbarDidActivateUpdate() = 0;  // the update indicator was clicked
    virtual void toolbarDidDismissUpdate() = 0;   // the update indicator was dismissed
};

// VIEW: the top toolbar of the editor. On the left — the Edit/Play/Simulate scene mode and script
// reload; on the right — the update indicator (UpdateIndicatorView). Intents go out through
// ToolbarOutput.
// (The Move/Rotate/Scale gizmo and the camera reset have moved to the viewport overlay toolbar.)
class ToolbarPanelView final : public EditorView, public UpdateIndicatorOutput {
public:
    explicit ToolbarPanelView(ToolbarOutput *output);
    void setUpdateStatus(const UpdateIndicatorModel &model); // the updater state, driven by the coordinator

    // Input: button availability according to the editor state (driven by the coordinator every frame).
    void setCanUndo(bool canUndo);
    void setCanRedo(bool canRedo);
    void setCanNavigate(bool back, bool forward); // selection history buttons
    void setCanPlay(bool canPlay);            // Play + Simulate (Edit mode AND scripting is loaded)
    void setCanStop(bool canStop);            // Stop (the world is running)
    void setCanReloadScripts(bool canReload); // Reload is available only in Edit

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
    std::shared_ptr<IconButton> m_reload;
    std::shared_ptr<IconButton> m_play;
    std::shared_ptr<IconButton> m_simulate;
    std::shared_ptr<IconButton> m_stop;
    std::shared_ptr<UpdateIndicatorView> m_update;
};

} // namespace Editor

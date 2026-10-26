#pragma once

#include "RecentProject.hpp"

namespace Panda {

// The editor's own files live in the user data directory (SystemTools::userDataDirectory; not in
// the application bundle, which an update replaces): `settings.json` — the sections of the editor
// scope (the settings registry: EditorViewportSettings, EditorAppearanceSettings and the sections
// of the modules), applied at once and saved on every change; `session.json` — the state of the
// last session (EditorSession).

/// Window state at the last opened project. Must not be saved if the start menu is open.
/// Must be saved only while the editor is open.
/// width/height — the size of the NORMAL state: for a maximized/fullscreen window the size
/// is not overwritten, so that "restore" after a restart brings back the previous bounds.
struct LastOpenedProjectWindowState final : public Rain::Codable {
    bool isFullScreen = false;
    bool isMaximized = false;
    int width = 0;
    int height = 0;

    bool isSizeValid() {
        return width >= 800 && height >= 600;
    }

    RAIN_FIELDS_BEGIN(LastOpenedProjectWindowState)
    RAIN_FIELD(isFullScreen)
    RAIN_FIELD(isMaximized)
    RAIN_FIELD(width)
    RAIN_FIELD(height)
    RAIN_FIELDS_END
};

/// Panel placement in the docking: stable panel id + zone (Editor::Dock as int) + whether it is open.
/// The order in the list = the order of the tabs.
struct DockPanelPlacement final : public Rain::Codable {
    std::string id;
    int dock = 0; // Editor::Dock: 0=Left 1=Right 2=Bottom 3=Center
    bool open = true;

    RAIN_FIELDS_BEGIN(DockPanelPlacement)
    RAIN_FIELD(id)
    RAIN_FIELD(dock)
    RAIN_FIELD(open)
    RAIN_FIELDS_END
};

/// A pinned editor tab of an object: the id encodes the object ("edit:e:<uuid>" / "edit:a:<id>"),
/// dock — where it lived. Restored after the world is loaded (dead targets are skipped).
struct PinnedTabPlacement final : public Rain::Codable {
    std::string id;
    int dock = 1; // Editor::Dock (default Right)

    RAIN_FIELDS_BEGIN(PinnedTabPlacement)
    RAIN_FIELD(id)
    RAIN_FIELD(dock)
    RAIN_FIELDS_END
};

/// The docking layout of the editor: panels, dock visibility, split weights, active tabs.
/// An empty panel list = the state has not been saved (first launch) — the defaults are applied.
struct DockLayoutState final : public Rain::Codable {
    std::vector<DockPanelPlacement> panels;
    std::vector<PinnedTabPlacement> pinnedTabs; // pinned editor tabs (the preview one is not saved)
    bool leftVisible = true;
    bool rightVisible = true;
    bool bottomVisible = true;
    float leftWeight = 0.f; // split weights; 0 = not set (keep the default)
    float centerColumnWeight = 0.f;
    float rightWeight = 0.f;
    float centerWeight = 0.f;
    float bottomWeight = 0.f;
    std::string selectedLeft; // id of the active tab of each dock (empty = do not change)
    std::string selectedRight;
    std::string selectedBottom;
    bool worldSettingsVisible = false; // the world settings sidebar of the viewport

    bool isValid() const {
        return !panels.empty();
    }

    RAIN_FIELDS_BEGIN(DockLayoutState)
    RAIN_FIELD(panels)
    RAIN_FIELD(leftVisible)
    RAIN_FIELD(rightVisible)
    RAIN_FIELD(bottomVisible)
    RAIN_FIELD(leftWeight)
    RAIN_FIELD(centerColumnWeight)
    RAIN_FIELD(rightWeight)
    RAIN_FIELD(centerWeight)
    RAIN_FIELD(bottomWeight)
    RAIN_FIELD(selectedLeft)
    RAIN_FIELD(selectedRight)
    RAIN_FIELD(selectedBottom)
    RAIN_FIELD(pinnedTabs)
    RAIN_FIELD(worldSettingsVisible)
    RAIN_FIELDS_END
};

/// "viewport": the viewport display options. Extensible: wireframe, grid, etc.
struct EditorViewportSettings final : public Rain::Codable {
    bool showHelperIcons = true;    // icons of light sources/cameras in the scene
    bool showPostprocessing = true; // postprocessing chain of the main camera (WYSIWYG)
    float cameraSpeed = 20.f;       // fly speed of the viewport camera (m/s)
    bool wireframe = false;         // debug wireframe of the scene

    RAIN_FIELDS_BEGIN(EditorViewportSettings)
    RAIN_FIELD(showHelperIcons)
    RAIN_FIELD(showPostprocessing)
    RAIN_FIELD(cameraSpeed)
    RAIN_FIELD(wireframe)
    RAIN_FIELDS_END
};

/// "updates": the editor updater (App/EditorUpdater, context/infrastructure.md) and the plugin
/// store (Project/Plugins/PluginStore): the same single switch as Zed's `auto_update` covers the
/// editor build and the installed plugins; the channel is a property of the build, not a setting.
struct EditorUpdateSettings final : public Rain::Codable {
    bool autoUpdate = true; // check at startup and every hour, download and install in the background

    RAIN_FIELDS_BEGIN(EditorUpdateSettings)
    RAIN_FIELD(autoUpdate)
    RAIN_FIELDS_END
};

/// "appearance": the look of the editor.
struct EditorAppearanceSettings final : public Rain::Codable {
    std::string theme;      // plugin theme id ("<plugin>/<file>"); empty or not found — the built-in dark one
    std::string uiFont;     // a family installed on this machine for the UI text; empty — Inter Display
    std::string monoFont;   // the same for numbers, paths and code; empty — JetBrains Mono
    float uiFontSize = 16.f; // the body size; the editor is designed at 16, every text scales with it

    RAIN_FIELDS_BEGIN(EditorAppearanceSettings)
    RAIN_FIELD(theme)
    RAIN_FIELD(uiFont)
    RAIN_FIELD(monoFont)
    RAIN_FIELD(uiFontSize)
    RAIN_FIELDS_END
};

/// The state of the last session: recent projects, the window and the docking layout.
struct EditorSession final : public Rain::Codable {
    std::vector<RecentProject> recentProjects;
    bool hasOpenedProject = false;
    LastOpenedProjectWindowState windowState;
    DockLayoutState dockLayout;

    RAIN_FIELDS_BEGIN(EditorSession)
    RAIN_FIELD(recentProjects)
    RAIN_FIELD(hasOpenedProject)
    RAIN_FIELD(windowState)
    RAIN_FIELD(dockLayout)
    RAIN_FIELDS_END
};

} // namespace Panda

#pragma once

#include "RecentProject.hpp"

#include <optional>

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

/// An open tab of a document the layout reopens: the id names the document ("asset:<id>",
/// "doc:<type>"), dock — where it was. Restored after the project is loaded (an asset that is
/// gone is skipped). The worlds are reopened from the session of the project instead.
struct OpenTabPlacement final : public Rain::Codable {
    std::string id;
    int dock = 3; // Editor::Dock (default Center)

    RAIN_FIELDS_BEGIN(OpenTabPlacement)
    RAIN_FIELD(id)
    RAIN_FIELD(dock)
    RAIN_FIELDS_END
};

/// What is open around the middle of a world tab: the docks of the window and the two columns of
/// the tab (its tree, its inspector).
struct SidePanelsState final : public Rain::Codable {
    bool left = true;
    bool right = true;
    bool bottom = true;
    bool tree = true;
    bool inspector = true;

    bool operator==(const SidePanelsState &other) const {
        return left == other.left && right == other.right && bottom == other.bottom && tree == other.tree &&
               inspector == other.inspector;
    }

    RAIN_FIELDS_BEGIN(SidePanelsState)
    RAIN_FIELD(left)
    RAIN_FIELD(right)
    RAIN_FIELD(bottom)
    RAIN_FIELD(tree)
    RAIN_FIELD(inspector)
    RAIN_FIELDS_END
};

/// The docking layout of the editor: panels, dock visibility, split weights, active tabs.
/// An empty panel list = the state has not been saved (first launch) — the defaults are applied.
struct DockLayoutState final : public Rain::Codable {
    // The model of the window the layout was saved for. A layout of another one names panels and
    // docks that are other ones now (the Hierarchy was a panel of the left dock before the world
    // tab held it): it is not applied, the window opens with the defaults.
    static constexpr int MODEL = 2;
    int model = 0;
    std::vector<DockPanelPlacement> panels;
    std::vector<OpenTabPlacement> openTabs; // the tabs of documents but the worlds
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
    std::string selectedCenter;
    int focusedDock = -1; // Editor::Dock whose active tab had the keyboard focus; -1 = unknown (leave the focus alone)
    // What a world tab shows beside its Scene (WorldEditorView): the tree and the inspector,
    // shown or hidden, and the shares of the three columns (0 = not set: the defaults).
    bool worldTreeVisible = true;
    bool worldInspectorVisible = true;
    float worldTreeWeight = 0.f;
    float worldSceneWeight = 0.f;
    float worldInspectorWeight = 0.f;
    // The Game of a world tab keeps the panels it was left with (EditorController::showView): its
    // own ones, once they differ from the ones above, which are the World view's. None — the
    // Game shows with what is open.
    std::optional<SidePanelsState> gamePanels;
    std::string contentBrowserView;     // how the Content Browser shows Assets: "list", or icons (anything else)
    bool contentBrowserFolders = true;  // its folder tree, beside the icons

    bool isValid() const {
        return model == MODEL && !panels.empty();
    }

    RAIN_FIELDS_BEGIN(DockLayoutState)
    RAIN_FIELD(model)
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
    RAIN_FIELD(selectedCenter)
    RAIN_FIELD(focusedDock)
    RAIN_FIELD(openTabs)
    RAIN_FIELD(worldTreeVisible)
    RAIN_FIELD(worldInspectorVisible)
    RAIN_FIELD(worldTreeWeight)
    RAIN_FIELD(worldSceneWeight)
    RAIN_FIELD(worldInspectorWeight)
    RAIN_FIELD(gamePanels)
    RAIN_FIELD(contentBrowserView)
    RAIN_FIELD(contentBrowserFolders)
    RAIN_FIELDS_END
};

/// "viewport": the viewport display options. Extensible: wireframe, grid, etc.
struct EditorViewportSettings final : public Rain::Codable {
    bool showGrid = true;           // the grid of the plane y = 0
    bool showHelperIcons = true;    // icons of light sources/cameras in the scene
    bool showPostprocessing = true; // postprocessing chain of the main camera (WYSIWYG)
    float cameraSpeed = 20.f;       // fly speed of the viewport camera (m/s)
    bool wireframe = false;         // debug wireframe of the scene
    // The steps of the gizmo while the primary key is held (⌘ on macOS, Ctrl elsewhere): a move,
    // a turn and a scale go by them, counted from where the drag began (Unity's increment snap).
    float snapMove = 0.25f;  // m
    float snapRotate = 15.f; // degrees
    float snapScale = 0.1f;  // of the scale the drag began with

    RAIN_FIELDS_BEGIN(EditorViewportSettings)
    RAIN_FIELD(showGrid)
    RAIN_FIELD(showHelperIcons)
    RAIN_FIELD(showPostprocessing)
    RAIN_FIELD(cameraSpeed)
    RAIN_FIELD(wireframe)
    RAIN_FIELD(snapMove)
    RAIN_FIELD(snapRotate)
    RAIN_FIELD(snapScale)
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

/// "externalTools": the programs the editor hands files to.
struct EditorExternalToolsSettings final : public Rain::Codable {
    // The text editor "Open in External Editor" starts with the file: a command (`zed`, `code`)
    // or the path of an executable; empty — the application the system opens the file with.
    std::string editor;

    RAIN_FIELDS_BEGIN(EditorExternalToolsSettings)
    RAIN_FIELD(editor)
    RAIN_FIELDS_END
};

/// "toolchain": what the editor builds the C++ of a project with on this machine (the CMake
/// panel: Project/Build/ProjectBuilds). Every field is a command found in PATH and the usual
/// install folders, or the path of an executable; empty — the default named next to it. A change
/// of the generator or of a compiler makes the next Configure or Build start from a clean build
/// folder: cmake does not reuse a tree made with another.
struct EditorToolchainSettings final : public Rain::Codable {
    std::string cmake;       // empty — the cmake that comes with the editor, then the SDK's, then PATH
    std::string generator;   // cmake -G for the scripts ("Ninja", "Unix Makefiles"); empty — cmake's default
    std::string cCompiler;   // CMAKE_C_COMPILER; empty — cmake finds one
    std::string cxxCompiler; // CMAKE_CXX_COMPILER; empty — cmake finds one

    RAIN_FIELDS_BEGIN(EditorToolchainSettings)
    RAIN_FIELD(cmake)
    RAIN_FIELD(generator)
    RAIN_FIELD(cCompiler)
    RAIN_FIELD(cxxCompiler)
    RAIN_FIELDS_END
};

/// "appearance": the look of the editor.
struct EditorAppearanceSettings final : public Rain::Codable {
    std::string theme;      // plugin theme id ("<plugin>/<file>"); empty or not found — the built-in dark one
    std::string uiFont;     // a family installed on this machine for the UI text; empty — Inter Display
    std::string monoFont;   // the same for numbers, paths and code; empty — JetBrains Mono
    float uiFontSize = 16.f; // the body size; the editor is designed at 16, every text scales with it
    // The memory, the CPU and the GPU of the editor at the right of the visibility strip; off —
    // nothing is sampled for it (EditorStatistics::updateStatus).
    bool showSystemStatus = true;

    RAIN_FIELDS_BEGIN(EditorAppearanceSettings)
    RAIN_FIELD(theme)
    RAIN_FIELD(uiFont)
    RAIN_FIELD(monoFont)
    RAIN_FIELD(uiFontSize)
    RAIN_FIELD(showSystemStatus)
    RAIN_FIELDS_END
};

// The settings of the editor it owns — the user's `settings.json` — as fields, one for each
// key: `editorSettings().viewport`. The modules add their sections to the same file through the
// settings registry.
struct EditorSettings final {
    EditorViewportSettings viewport;
    EditorAppearanceSettings appearance;
    EditorUpdateSettings updates;
    EditorExternalToolsSettings externalTools;
    EditorToolchainSettings toolchain;
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

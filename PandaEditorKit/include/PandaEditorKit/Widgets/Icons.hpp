#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Editor {

// Icon tokens: semantic names = base names of the PNGs in
// PandaEditor/resources/ui/editor-icons/<name>.png. The files are white alpha masks (rgb=1,
// alpha=shape), the color comes from the ImageView tint. The library loads ALL png files of the
// folder (EditorUI::loadEditorIcons): a new icon = a new file + a token here + a row in the author
// guide (PandaSDK/docs/plugins/icons.md: icon plugins replace icons by these names). Packages bring
// their own folders (EditorExtensions::iconFolders) and keep the tokens on their side.
namespace Ic {
    inline constexpr const char *Undo = "undo";
    inline constexpr const char *Redo = "redo";
    inline constexpr const char *Reload = "reload";
    inline constexpr const char *Save = "save";
    inline constexpr const char *Export = "export";
    inline constexpr const char *Play = "play";
    inline constexpr const char *Stop = "stop";
    inline constexpr const char *Simulate = "simulate";
    inline constexpr const char *Plus = "plus";
    inline constexpr const char *Maximize = "maximize";
    inline constexpr const char *Restore = "restore"; // collapse from fullscreen (the counterpart of Maximize)
    inline constexpr const char *GizmoMove = "gizmo-move";
    inline constexpr const char *GizmoRotate = "gizmo-rotate";
    inline constexpr const char *GizmoScale = "gizmo-scale";
    inline constexpr const char *SpaceLocal = "space-local"; // gizmo: local space
    inline constexpr const char *SpaceWorld = "space-world"; // gizmo: world space
    inline constexpr const char *Home = "home";
    inline constexpr const char *Folder = "folder";
    inline constexpr const char *FolderOpen = "folder-open";
    inline constexpr const char *ChevronLeft = "chevron-left"; // back navigation (selection history)
    inline constexpr const char *ChevronRight = "chevron-right";
    inline constexpr const char *ChevronUp = "chevron-up";
    inline constexpr const char *Material = "material";
    inline constexpr const char *Mesh = "mesh";
    // Object type icons (editor tabs, Hierarchy). Resolution by components — ObjectIcons.
    inline constexpr const char *EntityCube = "entity-cube"; // an entity without a "special" type
    inline constexpr const char *LightSun = "light-sun";     // directional light
    inline constexpr const char *LightPoint = "light-point"; // point light (light bulb)
    inline constexpr const char *Camera = "camera";
    inline constexpr const char *More = "more-h"; // three dots — actions menu (component etc.)
    inline constexpr const char *Clear = "clear";
    inline constexpr const char *Copy = "copy";
    inline constexpr const char *Info = "info";
    inline constexpr const char *Warning = "warning";
    inline constexpr const char *Error = "error";
    inline constexpr const char *Sprite = "sprite";
    inline constexpr const char *Script = "script";
    inline constexpr const char *Globe = "globe";
    inline constexpr const char *Audio = "audio";
    inline constexpr const char *Trash = "trash";
    inline constexpr const char *Configure = "tune";          // settings (sliders)
    inline constexpr const char *Plugins = "plugins";         // the Plugins tab (a puzzle piece)
    inline constexpr const char *CMakeConfigure = "configure"; // CMake: regeneration — two arrows in a circle
    inline constexpr const char *Build = "hammer";             // CMake: build
    // Docks (visibility strip): visibility toggles of the editor zones.
    inline constexpr const char *PanelHierarchy = "panel-hierarchy";
    inline constexpr const char *PanelBottomDock = "panel-bottom-dock";
    inline constexpr const char *PanelInspector = "panel-inspector";
    inline constexpr const char *PanelContentBrowser = "panel-content-browser";
    inline constexpr const char *PanelOutput = "panel-output";
    inline constexpr const char *PanelAssetManager = "panel-asset-manager";
    inline constexpr const char *PanelStatistics = "panel-statistics";
    inline constexpr const char *PanelProfiler = "panel-profiler";
    inline constexpr const char *PanelWorldSettings = "panel-world-settings";
    inline constexpr const char *PanelViewport = "panel-viewport";
    inline constexpr const char *PanelCMake = "panel-cmake";
    // File type previews (file-icons, 168px) — for the Content Browser grid.
    inline constexpr const char *FilePng = "file-png";
    inline constexpr const char *FileCpp = "file-cpp";
    inline constexpr const char *AssetWorld = "asset-world";
    inline constexpr const char *Prefab = "prefab";
    inline constexpr const char *FileGeneric = "file";

    // Icon sizes by context (design.md §4).
    inline constexpr float ToolbarSize = 17.f;
    inline constexpr float GizmoSize = 18.f;
    inline constexpr float HeaderSize = 15.f;
    inline constexpr float RowSize = 14.f;
} // namespace Ic

// Icon registry: name → TextureHandle. The application loads it once onto the shared renderer and
// sets it globally (setIcons), panels read it through makeIcon — like PandaUI::theme()/fonts: a
// static chrome resource, not part of the MVC data. The color comes from the tint (masks are white).
class IconLibrary {
public:
    void set(const std::string &name, PandaUI::TextureHandle texture);
    void merge(const IconLibrary &other); // names from other override its own
    PandaUI::TextureHandle get(const std::string &name) const; // {} if there is no such name

private:
    std::unordered_map<std::string, PandaUI::TextureHandle> m_textures;
};

void setIcons(IconLibrary library);
void addIcons(const IconLibrary &library); // package folders that arrived after startup (names override)
const IconLibrary &icons();

// An icon as an ImageView of the given size and color (tint). Without a color — theme().textMuted.
std::shared_ptr<PandaUI::ImageView> makeIcon(const std::string &name, float size, PandaUI::ThemeColor color);
std::shared_ptr<PandaUI::ImageView> makeIcon(const std::string &name, float size);
// Icon button: a square Control with a tinted icon and a hover background (toolbar 28, headers 23).
class IconButton final : public PandaUI::Control {
public:
    using ClickCallback = std::function<void()>;

    IconButton(const std::string &iconName, float iconSize, float buttonSize);

    void setOnClick(ClickCallback callback);
    void setOnContextMenu(std::function<void()> callback); // RMB (context menu, e.g. Dock Left/Right)
    void setButtonSize(float width, float height);         // rectangular (e.g. transport 30×28)
    void setIconName(const std::string &name);     // change the icon (e.g. maximize ↔ restore)
    void setIconColor(PandaUI::ThemeColor rest);   // icon color at rest (default ThemeColor::textMuted)
    void setFixedColor(PandaUI::ThemeColor color); // constant color (play=teal/stop=pink): hover does not change it
    void setActive(bool active);                   // selected (gizmo tool/dock tab): accentSoft background + accentText
    void setCornerRadius(float radius);

private:
    void controlStateChanged(PandaUI::ControlState, PandaUI::ControlState) override;
    void performPrimaryAction() override;
    bool pointerDown(PandaUI::PointerEvent &event) override; // RMB → m_onContextMenu, otherwise the base
    void themeChanged() override;
    void applyStyle();

    std::shared_ptr<PandaUI::ImageView> m_icon;
    ClickCallback m_onClick;
    std::function<void()> m_onContextMenu;
    PandaUI::ThemeColor m_restColor;
    bool m_hasFixedColor = false;
    PandaUI::ThemeColor m_fixedColor;
    bool m_active = false;
};

} // namespace Editor

#pragma once

#include <Foundation/Foundation.hpp>
#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/WorldCommands/WorldCommand.hpp>
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Panda {
class CommandHistory;
}

namespace Editor {

enum class HostLogLevel { Info, Warning, Error };

// Editor host for the editor parts of packages (context/plugins.md, stage 2): everything a
// form, a tool or a dialog needs from the editor besides the engine (the engine is used directly:
// World, AssetRef, GameContext registries). Implemented by the editor coordinator; one per process —
// editorHost(). Asset data edits go as commands into the document stack (the document model):
// the host does not know the content of the edits, it provides the stack, the tabs and the viewport.
class EditorHost {
public:
    virtual ~EditorHost() = default;

    // --- Assets and documents ---
    virtual std::string assetTitle(Panda::AssetId id) = 0; // asset name in the project (empty — not in the registry)
    // Undo stack of the asset document (created on demand); SAVE of a command applies it and
    // marks the document as "unsaved".
    virtual Panda::CommandHistory &assetUndoStack(Panda::AssetId id) = 0;
    // The document data has been recreated as a whole (resize, import): the history described the
    // previous data — a reset, as on reload.
    virtual void clearAssetUndoStack(Panda::AssetId id) = 0;
    virtual void selectAsset(Panda::AssetId id) = 0; // selection in the editor → the asset's editor tab
    // Asset thumbnail (material — a sphere, texture — a picture); invalid — none or still baking.
    virtual PandaUI::TextureHandle assetThumbnail(Panda::AssetId id) = 0;
    // Asset Picker at point at: project assets of kind kind (AssetKind::name); onPick(id) on selection.
    virtual void pickAsset(
        PandaUI::Context *context, PandaUI::Point at, const std::string &kind, std::function<void(uint32_t)> onPick
    ) = 0;
    virtual std::optional<Panda::path_t> openFileDialog() = 0; // native file selection dialog

    // --- Viewport ---
    // Editing session in the viewport: an accent border and a banner with title and a Stop button.
    // document — the asset whose stack receives the edits: while the session is active, Undo from the
    // viewport is addressed to it, and closing its tab ends the session. onStop is called on Stop and
    // when the document tab is closed — the tool releases the capture and calls endViewportEditing.
    virtual void beginViewportEditing(
        const std::string &title, Panda::AssetId document, std::function<void()> onStop
    ) = 0;
    virtual void endViewportEditing() = 0;

    // --- Panels ---
    virtual void refreshFromWorld() = 0; // the data has been recreated structurally: the panels re-read the world
    virtual void refreshAssetEditors(Panda::AssetId id) = 0; // the asset's tab forms re-read the document
    virtual void log(HostLogLevel level, const std::string &message) = 0; // the Output panel

    // --- Settings (the settings scopes, context/plugins.md) ---
    // The store of a scope: the context of the world the panels show, the project store, the
    // editor store.
    virtual Panda::TypeContext &settings(Panda::SettingsScope scope) = 0;
    // An edit of a section of the scope as a command (Kit: commitSettings<T>). The world scope —
    // the undo stack of the world and its dirty flag; the project scope — the stack of the project
    // settings document and its dirty flag; the editor scope — executed, applied and saved at once.
    virtual void commitSettings(Panda::SettingsScope scope, Foundation::Shared<Panda::WorldCommand> command) = 0;
    // The settings UI of a scope: the world sidebar of the viewport, the project or the editor
    // settings document; section — the title of the section to reveal (empty — as is).
    virtual void openSettings(Panda::SettingsScope scope, const std::string &section = {}) = 0;
    // Closes the editor through the unsaved-changes dialog and starts it again: the way a changed
    // module set takes effect (modules are linked statically, nothing is loaded or unloaded live).
    virtual void restartEditor() = 0;
};

// The process host: set by the editor before the editor parts are loaded (before that — nullptr).
EditorHost &editorHost();
void setEditorHost(EditorHost *host);

} // namespace Editor

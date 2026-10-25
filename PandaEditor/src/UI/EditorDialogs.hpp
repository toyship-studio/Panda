#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Editor {

// Editor dialogs are of two kinds:
//   POPOVERS — a Popup overlay at the point `at` (cursor/menu/button position), closed by a click
//     outside and by Escape, the UI under them stays live: quick in-place input and pickers;
//   MODALS — PandaUI::Dialog through Context::modals(): confirmations, forms, About. They block
//     the UI until answered. Forms of the modules' subject domains live in their editor parts
//     (PandaUI::Dialog directly, no registry is needed).

// Name input popover: a field (prefill, selected) + Cancel/Create; onSubmit(text) on a non-empty name.
void showNameDialog(
    PandaUI::Context *context, PandaUI::Point at, const std::string &title, const std::string &initialText,
    std::function<void(const std::string &)> onSubmit
);
// Modal confirmation: title + explanation + Cancel/<confirmTitle>; destructive —
// a dangerous action (deletion): the button has the danger color. onConfirm() on confirmation.
void showConfirmDialog(
    PandaUI::Context *context, const std::string &title, const std::string &message,
    const std::string &confirmTitle, bool destructive, std::function<void()> onConfirm
);
// The modal question "apply to children too?" (as Unity does for the Static flag): Cancel / "No, this object
// only" / "Yes, change children" (Enter). onApply(includeChildren) — according to the choice; onDismiss — on
// any close, after onApply (to return the widgets to the actual state).
void showApplyToChildrenDialog(
    PandaUI::Context *context, const std::string &title, const std::string &message,
    std::function<void(bool includeChildren)> onApply, std::function<void()> onDismiss
);
// Script class selection (PickScript): search field + list of classes; onPick(name) when a row is selected.
void showScriptPicker(
    PandaUI::Context *context, PandaUI::Point at, const std::vector<std::string> &scripts,
    std::function<void(const std::string &)> onPick
);
// Asset Picker (palette): search + project assets of the kind given by kind
// ("texture"/"material"/"mesh"/"postfx"/…) from the registry; onPick(assetId) when a row is selected.
// Closed by a click outside/Escape.
void showAssetPicker(
    PandaUI::Context *context, PandaUI::Point at, const std::string &kind,
    std::function<void(uint32_t)> onPick
);
// The "About" modal window: the version, the build and the commit (the manifest of the release
// workflow names the same build, context/infrastructure.md), the state of the updater, and the
// actions of the Help menu. Copy puts the version line on the clipboard for a report.
struct AboutInfo {
    std::string version;      // "0.1.0-preview.7", or "0.1.0" for a local build
    std::string channel;      // "preview", "stable", empty for a local build
    uint32_t build = 0;       // 0 for a local build
    std::string commit;       // short sha, empty for a local build
    std::string platform;     // "macOS arm64"
    std::string updateStatus; // one line of the updater, empty when idle
    bool updatesEnabled = false;
    std::function<void()> onCheckForUpdates;
    std::function<void()> onReleaseNotes;
};
void showAboutDialog(PandaUI::Context *context, const AboutInfo &info);
// A modal with a title, a message and OK.
void showMessageDialog(PandaUI::Context *context, const std::string &title, const std::string &message);
// A downloaded update: the version, its release notes (the plain text of the manifest), Restart
// Now / Later / Release Notes.
void showUpdateReadyDialog(
    PandaUI::Context *context, const std::string &version, const std::string &notes,
    std::function<void()> onRestart, std::function<void()> onReleaseNotes
);

} // namespace Editor

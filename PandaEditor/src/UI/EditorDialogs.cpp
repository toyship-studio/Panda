#include <Panda/Extensions/Extensions.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "UI/EditorDialogs.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"
#include <PandaEditorKit/Widgets/ObjectIcons.hpp>

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Picker.hpp>

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Base/Version.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // Dialog card: a surface with a border/rounding, a column with paddings.
    std::shared_ptr<Panel> makeCard() {
        auto card = std::make_shared<Panel>();
        applyElevation(*card, Elevation::Modal);
        card->layout().setFlexDirection(FlexDirection::Column);
        card->layout().setPadding(16.f);
        card->layout().setGap(12.f);
        card->layout().setMinWidth(Length::points(260.f));
        return card;
    }

    // A row of buttons aligned to the right: [Cancel] [action].
    std::shared_ptr<Panel> makeButtonRow(const std::shared_ptr<View> &cancel, const std::shared_ptr<View> &action) {
        auto row = std::make_shared<Panel>();
        row->setBackgroundColor(Color(0x00000000));
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setJustifyContent(Justify::End);
        row->layout().setGap(8.f);
        row->addSubview(cancel);
        row->addSubview(action);
        return row;
    }

} // namespace

void showNameDialog(
    Context *context, Point at, const std::string &title, const std::string &initialText,
    std::function<void(const std::string &)> onSubmit, const std::string &action
) {
    if (!context) { return; }

    auto card = makeCard();

    auto titleLabel = std::make_shared<Label>(title);
    titleLabel->setFont(Typo::DialogTitle);
    titleLabel->setFont(titleLabel->getFont().withWeight(FontWeight::Bold));
    card->addSubview(titleLabel);

    auto field = std::make_shared<TextField>();
    field->setText(initialText);
    field->layout().setWidth(Length::percent(100.f));
    card->addSubview(field);

    auto cancel = std::make_shared<Button>("Cancel");
    auto create = std::make_shared<Button>(action);
    create->setStyle(ButtonStyle::Accent);
    card->addSubview(makeButtonRow(cancel, create));

    auto popup = std::make_shared<Popup>(card, Rect(at.x, at.y, 0.f, 0.f));
    Popup *p = popup.get();
    // field is held by a RAW pointer: the popup owns card → field, and submit is called only while the
    // popup is alive. A shared_ptr here would create the cycle field.m_onSubmit → submit → field →
    // a TextField leak.
    TextField *fieldPtr = field.get();
    auto submit = [fieldPtr, onSubmit, p] {
        const std::string text = fieldPtr->getText();
        if (!text.empty() && onSubmit) { onSubmit(text); } // run BEFORE dismiss (the overlay is removed deferred)
        p->dismiss();
    };
    create->setOnClick([submit](Button &) { submit(); });
    field->setOnSubmit([submit](TextField &) { submit(); }); // Enter confirms
    cancel->setOnClick([p](Button &) { p->dismiss(); });

    context->addOverlay(popup);
    context->setFocusedView(field.get()); // typing is possible right away
    field->selectAll();                   // the prefill is selected → input replaces it as a whole
}

void showConfirmDialog(
    Context *context, const std::string &title, const std::string &message, const std::string &confirmTitle,
    bool destructive, std::function<void()> onConfirm
) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>(title);
    dialog->setMessage(message);
    dialog->addAction("Cancel", DialogActionRole::Cancel);
    dialog->addAction(
        confirmTitle, destructive ? DialogActionRole::Destructive : DialogActionRole::Primary, std::move(onConfirm)
    );
    context->modals().present(dialog);
}

void showApplyToChildrenDialog(
    Context *context, const std::string &title, const std::string &message,
    std::function<void(bool includeChildren)> onApply, std::function<void()> onDismiss
) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>(title);
    dialog->setMessage(message);
    dialog->addAction("Cancel", DialogActionRole::Cancel);
    dialog->addAction("No, this object only", DialogActionRole::Default, [onApply] { onApply(false); });
    dialog->addAction("Yes, change children", DialogActionRole::Primary, [onApply] { onApply(true); });
    dialog->setOnDismiss(std::move(onDismiss));
    context->modals().present(dialog);
}

void showScriptPicker(
    Context *context, Point at, const std::vector<std::string> &scripts,
    std::function<void(const std::string &)> onPick
) {
    std::vector<PickerItem> items;
    items.reserve(scripts.size());
    for (const std::string &name : scripts) { items.push_back({name, {}, Ic::Script}); }
    showPicker(context, at, "Search scripts…", std::move(items), [scripts, onPick](int index) {
        if (onPick) { onPick(scripts[static_cast<size_t>(index)]); }
    });
}

std::vector<PickerItem> projectAssetItems(std::vector<uint32_t> &ids) {
    struct Found {
        uint32_t id;
        std::string name;
        std::string folder;
        Panda::AssetType type;
    };
    std::vector<Found> found;
    if (auto *handler = Panda::AssetHandlerEditor::current()) {
        for (const auto &[id, info] : handler->getRegistry()) {
            found.push_back({static_cast<uint32_t>(id), handler->getAssetName(id),
                             handler->assetPath(id).parent_path().generic_string(), info.type});
        }
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        return a.name != b.name ? a.name < b.name : a.folder < b.folder;
    });
    std::vector<PickerItem> items;
    items.reserve(found.size());
    ids.clear();
    ids.reserve(found.size());
    for (Found &asset : found) {
        items.push_back({std::move(asset.name), std::move(asset.folder), assetIconName(asset.type), {}});
        ids.push_back(asset.id);
    }
    return items;
}

void showAssetPicker(
    Context *context, Point at, const std::string &kind, std::function<void(uint32_t)> onPick
) {
    // The project registry directly (the panels' style): assets of the kind given by kind, sorted
    // by name; the folder of an asset tells two of one name apart.
    struct Found {
        uint32_t id;
        std::string name;
        std::string folder;
    };
    std::vector<Found> found;
    auto *handler = Panda::AssetHandlerEditor::current();
    Panda::AssetType type = Panda::AssetType::NONE;
    if (handler) {
        // The kind tag is the name of the kind in the registry (assetTypeString).
        if (const Panda::AssetKind *registered = Panda::GameContext::getExtensions().assetKinds.find(kind)) {
            type = registered->type;
        }
        for (const auto &[id, info] : handler->getRegistry()) {
            if (info.type != type) { continue; }
            // The path of an asset is relative to the project: its folder is the detail of the row.
            found.push_back({static_cast<uint32_t>(id), handler->getAssetName(id),
                             handler->assetPath(id).parent_path().generic_string()});
        }
        std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
            return a.name != b.name ? a.name < b.name : a.folder < b.folder;
        });
    }
    std::vector<PickerItem> items;
    std::vector<uint32_t> ids;
    items.reserve(found.size());
    ids.reserve(found.size());
    const char *icon = assetIconName(type);
    for (Found &asset : found) {
        items.push_back({std::move(asset.name), std::move(asset.folder), icon});
        ids.push_back(asset.id);
    }
    showPicker(context, at, "Search " + kind + "…", std::move(items), [ids, onPick](int index) {
        if (onPick) { onPick(ids[static_cast<size_t>(index)]); }
    });
}

void showAboutDialog(Context *context, const AboutInfo &info) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>("Panda Editor");
    dialog->setMessage("Free 2D and 3D cross-platform game engine by Toyship.");
    dialog->setContentWidth(500.f);

    auto body = std::make_shared<Panel>();
    body->setBackgroundColor(Color(0x00000000));
    body->layout().setFlexDirection(FlexDirection::Column);
    body->layout().setGap(6.f);
    auto addLine = [body](const std::string &text, TextRole role) {
        auto label = std::make_shared<Label>(text);
        label->setFont(Typo::Caption);
        label->setTextRole(role);
        label->setNumberOfLines(0);
        body->addSubview(label);
    };
    // The build of the release workflow names the exact code of a tester's report; a local build
    // has no number and no channel.
    std::string channel = info.channel;
    if (!channel.empty()) { channel[0] = char(std::toupper(static_cast<unsigned char>(channel[0]))); }
    addLine(std::string("Version ") + info.version + (channel.empty() ? " (local build)" : " · " + channel), TextRole::Primary);
    if (info.build != 0) {
        addLine("Build " + std::to_string(info.build) + (info.commit.empty() ? "" : " · commit " + info.commit), TextRole::Muted);
    }
    addLine(info.platform, TextRole::Muted);
    if (!info.updateStatus.empty()) { addLine(info.updateStatus, TextRole::Secondary); }
    for (const char *line :
         {"Developer: Mikhail Andreichev",
          "Inspired by the Zed editor, zed.dev",
          "Panda Engine License 1.0, pandaengine.dev/license",
          "https://github.com/toyship-studio/Panda"}) {
        addLine(line, TextRole::Muted);
    }
    dialog->setBody(body);

    std::string report = "Panda Editor " + info.version;
    if (info.build != 0) { report += " (build " + std::to_string(info.build) + (info.commit.empty() ? ")" : ", commit " + info.commit + ")"); }
    report += ", " + info.platform;
    dialog->addAction("Copy", DialogActionRole::Default, [report] { setClipboardText(report); });
    if (info.onReleaseNotes) { dialog->addAction("Release Notes", DialogActionRole::Default, info.onReleaseNotes); }
    if (info.onCheckForUpdates) {
        dialog->addAction("Check for Updates", DialogActionRole::Default, info.onCheckForUpdates)->setEnabled(info.updatesEnabled);
    }
    dialog->addAction("OK", DialogActionRole::Primary);
    context->modals().present(dialog);
}

void showMessageDialog(Context *context, const std::string &title, const std::string &message) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>(title);
    dialog->setMessage(message);
    dialog->addAction("OK", DialogActionRole::Primary);
    context->modals().present(dialog);
}

void showUpdateReadyDialog(
    Context *context, const std::string &version, const std::string &notes, std::function<void()> onRestart,
    std::function<void()> onReleaseNotes
) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>("Panda " + version + " is ready");
    dialog->setMessage("The update is installed. It runs after a restart of the editor; your work is saved first.");
    dialog->setContentWidth(520.f);
    if (!notes.empty()) {
        auto body = std::make_shared<Panel>();
        body->setBackgroundColor(Color(0x00000000));
        body->layout().setFlexDirection(FlexDirection::Column);
        auto label = std::make_shared<Label>(notes);
        label->setFont(Typo::Caption);
        label->setTextRole(TextRole::Secondary);
        label->setNumberOfLines(0);
        body->addSubview(label);
        dialog->setBody(body);
    }
    dialog->addAction("Later", DialogActionRole::Cancel);
    if (onReleaseNotes) { dialog->addAction("Release Notes", DialogActionRole::Default, std::move(onReleaseNotes)); }
    dialog->addAction("Restart Now", DialogActionRole::Primary, std::move(onRestart));
    context->modals().present(dialog);
}

} // namespace Editor

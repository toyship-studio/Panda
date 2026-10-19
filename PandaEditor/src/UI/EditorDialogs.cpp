#include <Panda/Extensions/Extensions.hpp>
#include "UI/EditorDialogs.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"

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
        card->setBackgroundColor(theme().surface);
        card->surface().setCornerRadius(10.f);
        card->surface().setBorderWidth(1.f);
        card->surface().setBorderColor(theme().border);
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
    std::function<void(const std::string &)> onSubmit
) {
    if (!context) { return; }

    auto card = makeCard();

    auto titleLabel = std::make_shared<Label>(title);
    titleLabel->setFont(Font(17.f));
    titleLabel->setFont(titleLabel->getFont().withWeight(FontWeight::Bold));
    card->addSubview(titleLabel);

    auto field = std::make_shared<TextField>();
    field->setText(initialText);
    field->layout().setWidth(Length::percent(100.f));
    card->addSubview(field);

    auto cancel = std::make_shared<Button>("Cancel");
    auto create = std::make_shared<Button>("Create");
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
    if (!context) { return; }

    // Case-insensitive search: query as a substring inside the class name.
    auto matches = [](const std::string &name, const std::string &query) {
        if (query.empty()) { return true; }
        auto it = std::search(
            name.begin(), name.end(), query.begin(), query.end(),
            [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }
        );
        return it != name.end();
    };

    auto card = makeCard();

    auto title = std::make_shared<Label>("Select Script");
    title->setFont(Font(17.f));
    title->setFont(title->getFont().withWeight(FontWeight::Bold));
    card->addSubview(title);

    auto search = std::make_shared<TextField>();
    search->setPlaceholder("Search scripts...");
    search->layout().setWidth(Length::percent(100.f));
    card->addSubview(search);

    auto list = std::make_shared<ListView>();
    list->setSelectionMode(ListSelectionMode::Single);
    list->layout().setWidth(Length::percent(100.f));
    list->layout().setHeight(Length::points(240.f)); // fixed height → the list scrolls
    card->addSubview(list);

    auto popup = std::make_shared<Popup>(card, Rect(at.x, at.y, 0.f, 0.f));
    Popup *p = popup.get();
    ListView *listPtr = list.get(); // RAW: list lives in card→popup; the list→onSelChanged→list cycle is avoided

    // The current filtered set (row index → class name). Shared between rebuild and the selection.
    auto filtered = std::make_shared<std::vector<std::string>>();
    auto rebuild = [listPtr, scripts, filtered, matches](const std::string &query) {
        listPtr->clearItems();
        filtered->clear();
        for (const std::string &name : scripts) {
            if (matches(name, query)) {
                listPtr->addItem(name);
                filtered->push_back(name);
            }
        }
    };
    rebuild("");

    list->setOnSelectionChanged([listPtr, filtered, onPick, p](ListView &) {
        const int index = listPtr->selectedIndex();
        if (index < 0 || index >= static_cast<int>(filtered->size())) { return; }
        if (onPick) { onPick((*filtered)[index]); } // run BEFORE dismiss (the overlay is removed deferred)
        p->dismiss();
    });
    search->setOnTextChanged([rebuild](TextField &f) { rebuild(f.getText()); });

    context->addOverlay(popup);
    context->setFocusedView(search.get()); // focus on the search — filtering is possible right away
}

void showAssetPicker(
    Context *context, Point at, const std::string &kind, std::function<void(uint32_t)> onPick
) {
    if (!context) { return; }

    // The project registry directly (the panels' style): assets of the kind given by kind, sorted by name.
    struct Item {
        uint32_t id;
        std::string name;
    };
    auto items = std::make_shared<std::vector<Item>>();
    auto *handler = static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    if (handler) {
        // The kind tag (assetTypeString): built-in kinds have historical ones, kinds from the registry —
        // their name.
        const Panda::AssetType type = [&kind] {
            if (kind == "scene") { return Panda::AssetType::WORLD; }
            const Panda::AssetKind *registered = Panda::GameContext::getExtensions().assetKinds.find(kind);
            return registered != nullptr ? registered->type : Panda::AssetType::NONE;
        }();
        for (const auto &[id, info] : handler->getRegistry()) {
            if (info.type != type) { continue; }
            items->push_back({static_cast<uint32_t>(id), handler->getAssetName(id)});
        }
        std::sort(items->begin(), items->end(), [](const Item &a, const Item &b) {
            return a.name < b.name;
        });
    }

    auto matches = [](const std::string &name, const std::string &query) {
        if (query.empty()) { return true; }
        auto it = std::search(
            name.begin(), name.end(), query.begin(), query.end(),
            [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }
        );
        return it != name.end();
    };

    auto card = makeCard();

    auto search = std::make_shared<TextField>();
    search->setPlaceholder("Search " + kind + "...");
    search->layout().setWidth(Length::percent(100.f));
    card->addSubview(search);

    auto list = std::make_shared<ListView>();
    list->setSelectionMode(ListSelectionMode::Single);
    list->layout().setWidth(Length::percent(100.f));
    list->layout().setHeight(Length::points(240.f)); // fixed height → the list scrolls
    card->addSubview(list);

    auto popup = std::make_shared<Popup>(card, Rect(at.x, at.y, 0.f, 0.f));
    Popup *p = popup.get();
    ListView *listPtr = list.get(); // raw: the list lives in card→popup (see showScriptPicker)

    // The current filtered set (row index → asset id).
    auto filtered = std::make_shared<std::vector<uint32_t>>();
    auto rebuild = [listPtr, items, filtered, matches](const std::string &query) {
        listPtr->clearItems();
        filtered->clear();
        for (const Item &item : *items) {
            if (matches(item.name, query)) {
                listPtr->addItem(item.name);
                filtered->push_back(item.id);
            }
        }
    };
    rebuild("");

    list->setOnSelectionChanged([listPtr, filtered, onPick, p](ListView &) {
        const int index = listPtr->selectedIndex();
        if (index < 0 || index >= static_cast<int>(filtered->size())) { return; }
        if (onPick) { onPick((*filtered)[index]); } // run BEFORE dismiss (the overlay is removed deferred)
        p->dismiss();
    });
    search->setOnTextChanged([rebuild](TextField &f) { rebuild(f.getText()); });

    context->addOverlay(popup);
    context->setFocusedView(search.get()); // focus on the search — filtering is possible right away
}

void showAboutDialog(Context *context) {
    if (!context) { return; }
    auto dialog = std::make_shared<Dialog>("Panda");
    dialog->setMessage("Free 2D and 3D cross-platform game engine by Toyship.");

    auto body = std::make_shared<Panel>();
    body->setBackgroundColor(Color(0x00000000));
    body->layout().setFlexDirection(FlexDirection::Column);
    body->layout().setGap(6.f);
    // The build of the release workflow names the exact code of a tester's report
    // (context/infrastructure.md); a local build has no number.
    std::string version = std::string("Version ") + Panda::ENGINE_VERSION;
    if (Panda::ENGINE_BUILD_NUMBER != 0) {
        version += ", build " + std::to_string(Panda::ENGINE_BUILD_NUMBER);
        if (Panda::ENGINE_BUILD_COMMIT[0] != '\0') {
            version += std::string(" (") + Panda::ENGINE_BUILD_COMMIT + ")";
        }
    } else {
        version += ", local build";
    }
    for (const std::string &line :
         {version,
          std::string("Developer: Mikhail Andreichev"),
          std::string("Inspired by the Zed editor, zed.dev"),
          std::string("Panda Engine License 1.0, pandaengine.dev/license"),
          std::string("https://github.com/toyship-studio/Panda")}) {
        auto label = std::make_shared<Label>(line);
        label->setFont(Font(14.f));
        label->setTextRole(TextRole::Muted);
        body->addSubview(label);
    }
    dialog->setBody(body);
    dialog->addAction("OK", DialogActionRole::Primary);
    context->modals().present(dialog);
}

} // namespace Editor

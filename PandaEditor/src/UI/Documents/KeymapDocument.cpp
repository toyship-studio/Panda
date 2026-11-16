#include "UI/Documents/KeymapDocument.hpp"

#include "UI/Commands.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include <PandaUI/PandaUI.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float ROW_HEIGHT = 30.f;
    constexpr float KEYS_WIDTH = 190.f;
    constexpr float SCOPE_WIDTH = 150.f;
    constexpr float SOURCE_WIDTH = 80.f;
    constexpr double DOUBLE_CLICK_SECONDS = 0.45;

    std::string lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    const char *scopeText(CommandScope scope) {
        return scope == CommandScope::World ? "Viewport, Hierarchy" : "Editor";
    }

    std::string chordsText(const std::vector<KeyChord> &chords) {
        std::string text;
        for (const KeyChord &chord : chords) { text += (text.empty() ? "" : "   ") + chord.text(); }
        return text;
    }

    std::shared_ptr<Panel> clearPanel() {
        auto panel = std::make_shared<Panel>();
        panel->setBackgroundColor(Color(0x00000000));
        return panel;
    }

    std::shared_ptr<Label> cell(const std::string &text, Font font, TextRole role, float width) {
        auto label = std::make_shared<Label>(text);
        label->setFont(font);
        label->setTextRole(role);
        if (width > 0.f) {
            label->layout().setWidth(Length::points(width));
            label->layout().setFlexShrink(0.f);
        } else {
            label->layout().setFlexGrow(1.f);
            label->layout().setFlexShrink(1.f);
            label->layout().setFlexBasis(0.f);
        }
        return label;
    }

    // The box that records a chord: while it has the keyboard, the next key with whatever
    // modifiers are held is the chord — Escape and Enter too. After a chord it lets go of the
    // keyboard (Enter then confirms the dialog); a click records again.
    class ChordRecorder final : public Panel {
    public:
        ChordRecorder(Commands &commands, std::function<void(KeyChord)> onChord)
            : m_commands(commands)
            , m_onChord(std::move(onChord)) {
            setFocusable(true);
            layout().setHeight(Length::points(44.f));
            layout().setAlignItems(Align::Center);
            layout().setJustifyContent(Justify::Center);
            surface().setCornerRadius(Radius::md);
            surface().setBorderWidth(1.f);
            m_label = std::make_shared<Label>("");
            m_label->setUserInteractionEnabled(false);
            addSubview(m_label);
            applyState();
        }
        ~ChordRecorder() override {
            m_commands.setCapture(nullptr);
        }

        void setFocusAfterChord(View *view) {
            m_after = view;
        }
        void setChord(KeyChord chord) {
            m_chord = chord;
            applyState();
        }

        bool keyDown(KeyEvent &event) override {
            if (!isFocused()) { return false; }
            const KeyChord chord = KeyChord::of(event);
            if (chord.isSet()) { take(chord); }
            return true; // a modifier alone is a part of the chord to come
        }
        bool pointerDown(PointerEvent &event) override {
            if (Context *context = getContext()) { context->setFocusedView(this); }
            return true;
        }

    protected:
        void focusChanged(bool focused) override {
            // A chord of the menu bar never reaches the UI: the menu reports its command instead.
            if (focused) {
                m_commands.setCapture([this](KeyChord chord) { take(chord); });
            } else {
                m_commands.setCapture(nullptr);
            }
            applyState();
        }

    private:
        void take(KeyChord chord) {
            m_chord = chord;
            if (m_onChord) { m_onChord(chord); }
            // The keyboard goes back to the dialog: Enter saves, Escape cancels.
            if (Context *context = getContext()) { context->setFocusedView(m_after); }
            applyState();
        }
        void applyState() {
            const bool recording = isFocused();
            setBackgroundColor(ThemeColor::inputDeep);
            surface().setBorderColor(recording ? ThemeColor::accent : ThemeColor::borderMed);
            if (recording) {
                m_label->setText("Press the keys…");
                m_label->setFont(Typo::Body);
                m_label->setTextRole(TextRole::Dim);
            } else if (m_chord.isSet()) {
                m_label->setText(m_chord.text());
                m_label->setFont(Font(18.f));
                m_label->setTextRole(TextRole::Primary);
            } else {
                m_label->setText("No keys — click to record");
                m_label->setFont(Typo::Body);
                m_label->setTextRole(TextRole::Dim);
            }
        }

        Commands &m_commands;
        std::function<void(KeyChord)> m_onChord;
        std::shared_ptr<Label> m_label;
        KeyChord m_chord;
        View *m_after = nullptr;
    };

} // namespace

class KeymapView final : public Panel {
public:
    KeymapView(Commands &commands, std::function<void()> openFile)
        : m_commands(commands) {
        setBackgroundColor(ThemeColor::windowBackground);
        layout().setFlexDirection(FlexDirection::Column);
        layout().setFlexGrow(1.f);
        layout().setPadding(Spacing::xl);
        layout().setGap(Spacing::md);

        auto top = clearPanel();
        top->layout().setFlexDirection(FlexDirection::Row);
        top->layout().setAlignItems(Align::Center);
        top->layout().setGap(Spacing::md);
        m_search = std::make_shared<TextField>();
        m_search->setPlaceholder("Search commands and keys…");
        m_search->setClearsOnEscape(true);
        m_search->layout().setFlexGrow(1.f);
        m_search->setOnTextChanged([this](TextField &) { rebuild(); });
        top->addSubview(m_search);
        m_change = makeButton("Change Keys…", [this] { editSelected(); });
        top->addSubview(m_change);
        m_reset = makeButton("Reset", [this] {
            if (const Commands::Command *command = selected()) { m_commands.resetBinding(std::string(command->id)); }
        });
        top->addSubview(m_reset);
        top->addSubview(makeButton("Open Keymap File", std::move(openFile)));
        addSubview(top);

        auto header = clearPanel();
        header->layout().setFlexDirection(FlexDirection::Row);
        header->layout().setPadding(Edge::Horizontal, Spacing::md);
        header->layout().setGap(Spacing::md);
        header->addSubview(cell("COMMAND", Typo::PanelTitle, TextRole::Muted, 0.f));
        header->addSubview(cell("KEYS", Typo::PanelTitle, TextRole::Muted, KEYS_WIDTH));
        header->addSubview(cell("WHERE", Typo::PanelTitle, TextRole::Muted, SCOPE_WIDTH));
        header->addSubview(cell("SOURCE", Typo::PanelTitle, TextRole::Muted, SOURCE_WIDTH));
        addSubview(header);

        m_list = std::make_shared<ListView>();
        ListViewAppearance appearance = m_list->appearance();
        appearance.rowHeight = ROW_HEIGHT;
        appearance.rowRadius = Radius::sm;
        m_list->setAppearance(appearance);
        m_list->setSelectionMode(ListSelectionMode::Single);
        m_list->layout().setFlexGrow(1.f);
        m_list->layout().setFlexBasis(0.f);
        m_list->setOnSelectionChanged([this](ListView &) { syncButtons(); });
        // A second click on a row in a short time is a double click: the keys are edited.
        m_list->setOnItemActivated([this](ListView &, int row) {
            const auto now = std::chrono::steady_clock::now();
            const bool again = row == m_lastActivated && std::chrono::duration<double>(now - m_lastActivatedAt).count() < DOUBLE_CLICK_SECONDS;
            m_lastActivated = again ? -1 : row;
            m_lastActivatedAt = now;
            if (again) { editSelected(); }
        });
        addSubview(m_list);

        auto footer = std::make_shared<Label>(
            "Double-click a command to change its keys. Several chords for one command and comments live in the keymap file."
        );
        footer->setFont(Typo::Caption);
        footer->setTextRole(TextRole::Dim);
        addSubview(footer);

        rebuild();
    }

    void rebuild() {
        const Commands::Command *kept = selected();
        const std::string keptId = kept != nullptr ? kept->id : std::string();
        const std::string query = lower(m_search->getText());
        m_list->clearItems();
        m_rows.clear();
        // By group, then by name: the order of a reference table, not of registration.
        std::vector<const Commands::Command *> commands;
        for (const Commands::Command &command : m_commands.all()) {
            const std::string haystack = lower(command.group + ": " + command.label + " " + chordsText(command.chords) + " " + command.id);
            std::string names;
            for (const KeyChord &chord : command.chords) { names += " " + chord.name(); }
            if (query.empty() || haystack.find(query) != std::string::npos || names.find(query) != std::string::npos) {
                commands.push_back(&command);
            }
        }
        std::stable_sort(commands.begin(), commands.end(), [](const Commands::Command *a, const Commands::Command *b) {
            return a->group != b->group ? a->group < b->group : a->label < b->label;
        });
        int select = -1;
        for (const Commands::Command *command : commands) {
            auto row = clearPanel();
            row->setUserInteractionEnabled(false); // the list row under it takes the click
            row->layout().setFlexDirection(FlexDirection::Row);
            row->layout().setAlignItems(Align::Center);
            row->layout().setGap(Spacing::md);
            row->layout().setFlexGrow(1.f);
            row->layout().setFlexShrink(1.f);
            row->addSubview(cell(command->group + ": " + command->label, Typo::Item, TextRole::Primary, 0.f));
            row->addSubview(cell(
                chordsText(command->chords), Typo::Item, command->customized ? TextRole::Accent : TextRole::Secondary, KEYS_WIDTH
            ));
            row->addSubview(cell(scopeText(command->scope), Typo::Caption, TextRole::Dim, SCOPE_WIDTH));
            row->addSubview(cell(command->customized ? "User" : "Default", Typo::Caption, TextRole::Dim, SOURCE_WIDTH));
            if (command->id == keptId) { select = static_cast<int>(m_rows.size()); }
            m_list->addRow(row);
            m_rows.push_back(command->id);
        }
        if (select >= 0) { m_list->setSelectedIndex(select); }
        syncButtons();
        setNeedsLayout();
    }

    // What an agent and a test ask instead of clicking: the dialog of a command.
    void edit(const std::string &id) {
        const Commands::Command *command = m_commands.find(id);
        Context *context = getContext();
        if (command == nullptr || context == nullptr) { return; }
        auto chosen = std::make_shared<KeyChord>(command->chords.empty() ? KeyChord() : command->chords.front());
        auto dialog = std::make_shared<Dialog>("Change Keys");
        dialog->setMessage(command->group + ": " + command->label);

        auto body = clearPanel();
        body->layout().setFlexDirection(FlexDirection::Column);
        body->layout().setGap(Spacing::md);
        auto conflict = std::make_shared<Label>("");
        conflict->setFont(Typo::Caption);
        conflict->setTextRole(TextRole::Dim);
        conflict->setNumberOfLines(0);
        Commands *commands = &m_commands;
        auto describe = [commands, conflict, id](KeyChord chord) {
            std::string text;
            for (const Commands::Command *other : commands->boundTo(chord, id)) {
                text += (text.empty() ? "These keys also run " : ", ") + other->group + ": " + other->label;
            }
            conflict->setText(text.empty() ? "Click the box and press the keys. Enter saves." : text + ". Both stay bound: the first that applies takes the key.");
            conflict->setTextRole(text.empty() ? TextRole::Dim : TextRole::Accent);
        };
        auto recorder = std::make_shared<ChordRecorder>(m_commands, [chosen, describe](KeyChord chord) {
            *chosen = chord;
            describe(chord);
        });
        recorder->setChord(*chosen);
        describe(*chosen);
        body->addSubview(recorder);
        body->addSubview(conflict);
        dialog->setBody(body);

        dialog->addAction("Cancel", DialogActionRole::Cancel);
        dialog->addAction("Remove Keys", DialogActionRole::Default, [commands, id] { commands->bind(id, {}); });
        dialog->addAction("Save", DialogActionRole::Primary, [commands, id, chosen] {
            if (!chosen->isSet()) { return; }
            // The first chord is replaced; the alternatives of the command stay.
            const Commands::Command *command = commands->find(id);
            if (command == nullptr) { return; }
            std::vector<KeyChord> chords = command->chords;
            const auto existing = std::find(chords.begin(), chords.end(), *chosen);
            if (existing != chords.end()) {
                std::rotate(chords.begin(), existing, existing + 1); // an alternative becomes the first
            } else if (chords.empty()) {
                chords.push_back(*chosen);
            } else {
                chords.front() = *chosen;
            }
            commands->bind(id, std::move(chords));
        });
        recorder->setFocusAfterChord(dialog.get());
        dialog->setInitialFocus(recorder.get());
        context->modals().present(dialog);
    }

private:
    std::shared_ptr<Button> makeButton(const std::string &title, std::function<void()> action) {
        auto button = std::make_shared<Button>(title);
        button->setSize(ButtonSize::Small);
        button->layout().setFlexShrink(0.f);
        button->setOnClick([action = std::move(action)](Button &) {
            if (action) { action(); }
        });
        return button;
    }

    const Commands::Command *selected() const {
        const int row = m_list ? m_list->selectedIndex() : -1;
        if (row < 0 || row >= static_cast<int>(m_rows.size())) { return nullptr; }
        return m_commands.find(m_rows[static_cast<size_t>(row)]);
    }

    void editSelected() {
        if (const Commands::Command *command = selected()) { edit(std::string(command->id)); }
    }

    void syncButtons() {
        const Commands::Command *command = selected();
        m_change->setEnabled(command != nullptr);
        m_reset->setEnabled(command != nullptr && command->customized);
    }

    Commands &m_commands;
    std::shared_ptr<TextField> m_search;
    std::shared_ptr<ListView> m_list;
    std::shared_ptr<Button> m_change;
    std::shared_ptr<Button> m_reset;
    std::vector<std::string> m_rows; // row → command id
    int m_lastActivated = -1;
    std::chrono::steady_clock::time_point m_lastActivatedAt;
};

KeymapDocument::KeymapDocument(Commands &commands, std::function<void()> openFile)
    : m_commands(commands)
    , m_view(std::make_shared<KeymapView>(commands, std::move(openFile))) {}

std::string KeymapDocument::title() {
    return "Keymap";
}

std::string KeymapDocument::icon() {
    return Ic::Configure;
}

std::shared_ptr<PandaUI::View> KeymapDocument::view() {
    return m_view;
}

void KeymapDocument::refresh() {
    m_view->rebuild();
}

std::filesystem::path KeymapDocument::filePath() {
    return m_commands.keymapPath();
}

} // namespace Editor

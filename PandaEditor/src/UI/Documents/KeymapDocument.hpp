#pragma once

#include <PandaEditorKit/Document.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Editor {

class Commands;
class KeymapView;

// The Keymap tab (Settings → Open Keymap): every command of the editor with the keys that run
// it, where the key works and whether the binding is the default or the user's. A search over
// the names and the keys; a row is changed by recording a chord, unbound or returned to the
// default. Every change is written to keymap.json at once — no dirty state. The file itself is
// one click away for what the tab does not edit (several chords of one command).
class KeymapDocument final : public Document {
public:
    KeymapDocument(Commands &commands, std::function<void()> openFile);
    std::string title() override;
    std::string icon() override;
    std::shared_ptr<PandaUI::View> view() override;
    void refresh() override;
    std::filesystem::path filePath() override;

private:
    Commands &m_commands;
    std::shared_ptr<KeymapView> m_view;
};

} // namespace Editor

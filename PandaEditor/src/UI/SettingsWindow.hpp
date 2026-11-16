#pragma once

#include <Foundation/Foundation.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/WorldCommands/WorldCommand.hpp>
#include <PandaEditorKit/Document.hpp>
#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
class EditorLayer;
}

namespace Editor {

class Commands;

// The content of the Settings window (a second window of the editor, over the main one): the
// pages on the left — the editor settings, the project settings, the keymap — and the document
// of the chosen page on the right. A document with unsaved changes (the project settings: they
// apply on save) gets a footer with Save and Revert; the others apply at once. Escape closes the
// window; the documents live as long as it is open.
class SettingsWindowView final : public PandaUI::Panel {
public:
    struct Page {
        std::string title;
        std::string icon;
        std::string hint; // one line under the title of the page
        Document *document = nullptr;
    };

    SettingsWindowView(std::vector<Page> pages, std::function<void()> onClose);

    void selectPage(int index);
    Document *selectedDocument() const;

    void update(double deltaTime) override; // the footer follows the dirty state of the document
    bool keyDown(PandaUI::KeyEvent &event) override;

protected:
    void contextAttached() override; // Escape closes the window whatever has the keyboard

private:
    void syncFooter();

    std::vector<Page> m_pages;
    std::function<void()> m_onClose;
    std::shared_ptr<PandaUI::ListView> m_navigation;
    std::vector<std::shared_ptr<PandaUI::Label>> m_dirtyMarks; // per page
    std::shared_ptr<PandaUI::Label> m_title;
    std::shared_ptr<PandaUI::Label> m_hint;
    std::shared_ptr<PandaUI::Panel> m_content;
    std::shared_ptr<PandaUI::Panel> m_footer;
    std::shared_ptr<PandaUI::Label> m_footerText;
    int m_selected = -1;
    bool m_footerShown = false;
    std::vector<bool> m_dirtyShown;
};

enum class SettingsPage { Editor, Project, Keymap }; // the order of the pages

// The Settings window: a second window of the editor over the main one, with the editor settings,
// the project settings and the keymap as its pages. What is edited there shows in the viewport
// behind it at once. The window itself is the application's (EditorLayer::openAuxiliaryWindow);
// its content and the documents of its pages are made here and live while it is open: the project
// settings — a document of project.json with its own undo stack, dirty until saved; the editor
// settings and the keymap — applied and written by every edit.
class SettingsWindow final {
public:
    SettingsWindow(Panda::EditorLayer &app, Commands &commands);
    ~SettingsWindow();

    // The window on the page: opened, or brought forward. false — the window could not be made.
    bool show(SettingsPage page);
    // The same with a section of the page in view (the pages of settings; empty — as is).
    void show(SettingsPage page, const std::string &section);
    void close();
    // Escape arrives from inside the key dispatch of the window's own UI, which closing destroys:
    // the owner closes the window on the next frame.
    void requestClose();
    bool isCloseRequested() const;
    bool isOpen() const {
        return m_open != nullptr;
    }

    // The window got or lost the keyboard. While it has it, Save and Undo of the editor mean the
    // document of the page in front (focusedDocument; nullptr — the keyboard is elsewhere).
    void setFocused(bool focused);
    bool isFocused() const;
    Document *focusedDocument() const;
    bool owns(const Document *document) const; // one of the documents of the open window

    // What changed outside the window, for the pages that show it.
    void settingsFileReloaded(Panda::SettingsScope scope); // settings.json / project.json was reread
    void storesChanged(); // the pages of settings read their stores again (the parts of modules were loaded)
    void keymapChanged();
    // An edit of the project scope as an undo step of its page. false — the window is closed:
    // nothing keeps a history, the caller applies the command as it is.
    bool commitProjectSettings(Foundation::Shared<Panda::WorldCommand> command);

private:
    Document *document(SettingsPage page) const; // nullptr — the window is closed

    struct Open {
        std::unique_ptr<Document> editor, project, keymap;
        std::shared_ptr<SettingsWindowView> view;
        bool focused = false;
        bool closeRequested = false;
    };

    Panda::EditorLayer &m_app;
    Commands &m_commands;
    std::unique_ptr<Open> m_open; // while the window is open
};

} // namespace Editor

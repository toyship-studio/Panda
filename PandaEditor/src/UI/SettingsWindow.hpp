#pragma once

#include <PandaEditorKit/Document.hpp>
#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Editor {

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
    int selectedPage() const {
        return m_selected;
    }
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

} // namespace Editor

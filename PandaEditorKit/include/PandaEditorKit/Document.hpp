#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {
class CommandHistory;
}

namespace Editor {

// A document in an editor tab (the document model, context/plugins.md): what the tab chrome
// needs from its content — the title, the unsaved state, saving, the undo stack and the close
// question. The world, the entity and asset tabs, the project and editor settings implement it,
// and the editor parts of modules may add document types (EditorExtensions::documentTypes).
class Document {
public:
    virtual ~Document() = default;

    virtual std::string title() = 0;
    virtual std::string icon() {
        return {};
    }
    // The tab content: one view per document, alive as long as the document.
    virtual std::shared_ptr<PandaUI::View> view() = 0;
    // Unsaved changes: the dot on the tab and the question on close.
    virtual bool isDirty() {
        return false;
    }
    virtual bool save() { // false — the write failed: the tab stays
        return true;
    }
    virtual void revert() {} // Don't Save: back to the file
    // The undo stack Cmd+Z goes to while the tab has the focus; nullptr — the document has none.
    virtual Panda::CommandHistory *undoStack() {
        return nullptr;
    }
    // The data changed outside the form (undo/redo, a reload): the form rereads the document.
    virtual void refresh() {}
};

// A document type the dock's "+" menu and the menu bar offer (the settings of the project and of
// the editor; the documents of modules later): one open instance of a type at a time — opening it
// again activates its tab.
struct DocumentType final {
    std::string id;    // the tab id and the key of the saved layout ("projectSettings")
    std::string title; // "Project Settings"
    std::string icon;  // an icon token; empty — the regular tab
    std::function<std::unique_ptr<Document>()> create;
};

class DocumentTypeRegistry final {
public:
    void add(DocumentType type);
    const std::vector<DocumentType> &types() const {
        return m_types;
    }
    const DocumentType *find(std::string_view id) const;

private:
    std::vector<DocumentType> m_types;
};

} // namespace Editor

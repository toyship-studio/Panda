#pragma once
#include <PandaEditorKit/Document.hpp>
#include <functional>
#include <memory>
#include <string>

namespace Panda {
class PluginStore;
}

namespace Editor {

class PluginsView;

// What the Plugins tab needs from the application layer besides the store: the theme in use and
// the two actions the store does not own (the theme is a setting; the folder dialog is native).
struct PluginsHost {
    Panda::PluginStore *store = nullptr;
    std::function<std::string()> currentTheme;            // the id of the applied theme ("<plugin>/<file>")
    std::function<void(const std::string &)> selectTheme; // applies and saves the choice
    std::function<void()> installDevPlugin;               // the folder dialog, then PluginStore::installDev
};

// The Plugins tab (the document model): the Extensions page of Zed over the plugin store —
// search, the All / Installed / Not Installed filter, the category chips, one card per plugin
// with its action, Install Dev Plugin. No dirty state: everything applies at once.
class PluginsDocument final : public Document {
public:
    explicit PluginsDocument(PluginsHost host);
    std::string title() override;
    std::string icon() override;
    std::shared_ptr<PandaUI::View> view() override;
    void refresh() override;

private:
    std::shared_ptr<PluginsView> m_view;
};

} // namespace Editor

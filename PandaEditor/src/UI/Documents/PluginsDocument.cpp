#include "UI/Documents/PluginsDocument.hpp"

#include "UI/Panels/PluginsView.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>

namespace Editor {

PluginsDocument::PluginsDocument(PluginsHost host)
    : m_view(std::make_shared<PluginsView>(std::move(host))) {}

std::string PluginsDocument::title() {
    return "Plugins";
}

std::string PluginsDocument::icon() {
    return Ic::Plugins;
}

std::shared_ptr<PandaUI::View> PluginsDocument::view() {
    return m_view;
}

void PluginsDocument::refresh() {
    m_view->refresh();
}

} // namespace Editor

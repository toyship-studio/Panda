#include "PandaEditorKit/EditorHost.hpp"

#include <Foundation/Foundation.hpp>

namespace Editor {

namespace {
    EditorHost *s_host = nullptr;
}

EditorHost &editorHost() {
    PND_ASSERT(s_host != nullptr, "Editor host is not set: editor parts load after the editor coordinator exists");
    return *s_host;
}

void setEditorHost(EditorHost *host) {
    s_host = host;
}

} // namespace Editor

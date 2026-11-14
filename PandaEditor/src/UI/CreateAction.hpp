#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Editor {

// One thing the editor can create: an entity of the open world or an asset of the project. The
// lists are made in one place (EditorController) and shown by everything that offers to create:
// File → New, the context menus of the Hierarchy and of the Content Browser. The title is the
// noun ("Cube", "Material"); the menu around it says "New".
struct CreateAction final {
    std::string title;
    std::function<void()> run;
};

using CreateActions = std::vector<CreateAction>;

} // namespace Editor

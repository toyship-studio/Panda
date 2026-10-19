#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Editor {

class EditorExtensions;

// The themes for the APPEARANCE section of the editor scope: the data plugins live in the
// application layer, which sets the provider before the sections are built.
struct ThemeProvider {
    std::function<std::vector<std::string>()> names; // labels in list order
    std::function<int()> current;                    // index of the current one (-1 — none)
    std::function<void(int)> select;                 // applies and saves the choice
};
void setThemeProvider(ThemeProvider provider);

// The settings sections of the core, registered in EditorExtensions::settingsSections like the
// sections of the modules (context/plugins.md, decision 2): the world scope — WORLD (lighting),
// SHADOWS, OCCLUSION VOLUME, WIND; the editor scope — VIEWPORT; the project scope — WORLD (the
// start world), MODULES. Registered before the editor parts of the modules load, so the core
// sections come first in every scope. The editor scope also gets APPEARANCE (the theme) from the
// ThemeProvider.
void registerCoreSettingsSections(EditorExtensions &extensions);

} // namespace Editor

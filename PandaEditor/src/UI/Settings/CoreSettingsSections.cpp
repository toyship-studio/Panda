#include "UI/Settings/CoreSettingsSections.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/SettingsCommands.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/WorldSettings.hpp>
#include <Panda/Renderer/SceneWind.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Editor {

using namespace PandaUI;

namespace {

    using Panda::SettingsScope;

    // Every section edits a copy of its struct and commits it as a whole (Kit: commitSettings<T>):
    // one undo step per gesture, the same path as the module sections.

    /* ------------------------------ World scope ------------------------------ */

    void buildLighting(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldLightingSettings>(store.get<Panda::WorldLightingSettings>());
        auto emit = [data] { commitSettings<Panda::WorldLightingSettings>(SettingsScope::World, *data); };
        addColorRow(section, "Ambient Color", data->ambientColor, [data, emit](const Panda::Color &color) {
            data->ambientColor = color;
            emit();
        });
        addNumberRow(section, "Ambient Intensity", 0.f, 10.f, data->ambientIntensity, [data, emit](float value) {
            data->ambientIntensity = value;
            emit();
        });
        // World environment map (equirect, png/jpg/.hdr): the environmentMap system texture — the
        // default sky and PBR reflections; empty — the built-in gradient, no reflections.
        addAssetRow(
            section, "Environment Map", "texture", static_cast<uint32_t>(data->environmentMap.getId()),
            [data, emit](uint32_t id) {
                data->environmentMap = assetRefOf(id);
                emit();
            }
        );
        addNumberRow(section, "Environment Intensity", 0.f, 64.f, data->environmentIntensity, [data, emit](float value) {
            data->environmentIntensity = value;
            emit();
        });
    }

    // Cascaded shadows from the sun.
    void buildShadows(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldGraphicsSettings>(store.get<Panda::WorldGraphicsSettings>());
        auto emit = [data] { commitSettings<Panda::WorldGraphicsSettings>(SettingsScope::World, *data); };
        addToggleRow(section, "Enabled", data->shadows.enabled, [data, emit](bool on) {
            data->shadows.enabled = on;
            emit();
        });
        addNumberRow(section, "Cascades", 1.f, 4.f, static_cast<float>(data->shadows.cascadeCount), [data, emit](float value) {
            data->shadows.cascadeCount = static_cast<uint32_t>(std::max(0.f, value));
            emit();
        });
        // Side of a cascade tile in the atlas (texels).
        addNumberRow(section, "Map Size", 256.f, 4096.f, static_cast<float>(data->shadows.tileSize), [data, emit](float value) {
            data->shadows.tileSize = static_cast<uint32_t>(std::max(0.f, value));
            emit();
        });
        addNumberRow(section, "Distance", 1.f, 4096.f, data->shadows.distance, [data, emit](float value) {
            data->shadows.distance = value;
            emit();
        });
        addNumberRow(section, "Strength", 0.f, 1.f, data->shadows.strength, [data, emit](float value) {
            data->shadows.strength = value;
            emit();
        });
        // Biases against acne: slope — the rasterizer of the depth pass; normal offset and receiver
        // bias — in cascade texels (independent of the scene scale).
        addNumberRow(section, "Slope Bias", 0.f, 16.f, data->shadows.slopeBias, [data, emit](float value) {
            data->shadows.slopeBias = value;
            emit();
        });
        addNumberRow(section, "Normal Offset", 0.f, 8.f, data->shadows.normalOffset, [data, emit](float value) {
            data->shadows.normalOffset = value;
            emit();
        });
        addNumberRow(section, "Depth Bias", 0.f, 8.f, data->shadows.receiverBias, [data, emit](float value) {
            data->shadows.receiverBias = value;
            emit();
        });
    }

    // Occlusion Volume: sky visibility and sun transmittance from the flora scattering.
    void buildOcclusion(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldGraphicsSettings>(store.get<Panda::WorldGraphicsSettings>());
        auto emit = [data] { commitSettings<Panda::WorldGraphicsSettings>(SettingsScope::World, *data); };
        addToggleRow(section, "Enabled", data->occlusion.enabled, [data, emit](bool on) {
            data->occlusion.enabled = on;
            emit();
        });
        addNumberRow(section, "AO Strength", 0.f, 1.f, data->occlusion.aoStrength, [data, emit](float value) {
            data->occlusion.aoStrength = value;
            emit();
        });
        addNumberRow(section, "Sun Near", 0.f, 1.f, data->occlusion.sunStrengthNear, [data, emit](float value) {
            data->occlusion.sunStrengthNear = value;
            emit();
        });
        addNumberRow(section, "Sun Far", 0.f, 1.f, data->occlusion.sunStrengthFar, [data, emit](float value) {
            data->occlusion.sunStrengthFar = value;
            emit();
        });
        addNumberRow(section, "Cell Size", 0.5f, 8.f, data->occlusion.cellSize, [data, emit](float value) {
            data->occlusion.cellSize = value;
            emit();
        });
        // Window — chunks per side (a chunk is 64 cells: with a 4 m cell an 8-chunk window = 2 km).
        addNumberRow(
            section, "Window Chunks", 2.f, 8.f, static_cast<float>(data->occlusion.windowChunks),
            [data, emit](float value) {
                data->occlusion.windowChunks = static_cast<int>(std::lround(value));
                emit();
            }
        );
        addNumberRow(section, "Extinction", 0.f, 8.f, data->occlusion.extinction, [data, emit](float value) {
            data->occlusion.extinction = value;
            emit();
        });
        // Sample offset from the surface (m): smaller — the occlusion is closer to the crown boundary.
        addNumberRow(section, "Surface Offset", 0.f, 8.f, data->occlusion.surfaceOffset, [data, emit](float value) {
            data->occlusion.surfaceOffset = value;
            emit();
        });
        // Sky visibility exponent: > 1 darkens the dense interior, leaves open areas untouched.
        addNumberRow(section, "Contrast", 0.5f, 4.f, data->occlusion.contrast, [data, emit](float value) {
            data->occlusion.contrast = value;
            emit();
        });
    }

    // The scene wind of the world: sways all vegetation; strength and gustiness go out as system
    // material fields (windStrength/windGustiness), direction and phases — via PANDA_FIELDS.
    void buildWind(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::SceneWindSettings>(store.get<Panda::SceneWindSettings>());
        auto emit = [data] { commitSettings<Panda::SceneWindSettings>(SettingsScope::World, *data); };
        addNumberRow(section, "Strength", 0.f, 1.f, data->strength, [data, emit](float value) {
            data->strength = value;
            emit();
        });
        addNumberRow(section, "Speed", 0.f, 8.f, data->speed, [data, emit](float value) {
            data->speed = value;
            emit();
        });
        addNumberRow(section, "Gustiness", 0.f, 1.f, data->gustiness, [data, emit](float value) {
            data->gustiness = value;
            emit();
        });
        addNumberRow(section, "Direction", 0.f, 360.f, data->directionDegrees, [data, emit](float value) {
            data->directionDegrees = value;
            emit();
        });
    }

    /* ------------------------------ Editor scope ------------------------------ */

    // The viewport display options: applied at once, saved on every change.
    void buildViewport(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::EditorViewportSettings>(store.get<Panda::EditorViewportSettings>());
        auto emit = [data] { commitSettings<Panda::EditorViewportSettings>(SettingsScope::Editor, *data); };
        addToggleRow(section, "Helper Icons", data->showHelperIcons, [data, emit](bool on) {
            data->showHelperIcons = on;
            emit();
        });
        addToggleRow(section, "Postprocessing", data->showPostprocessing, [data, emit](bool on) {
            data->showPostprocessing = on;
            emit();
        });
        // Camera fly speed (m/s): large maps need long flights.
        addNumberRow(section, "Camera Speed", 1.f, 500.f, data->cameraSpeed, [data, emit](float value) {
            data->cameraSpeed = value;
            emit();
        });
        addToggleRow(section, "Wireframe", data->wireframe, [data, emit](bool on) {
            data->wireframe = on;
            emit();
        });
    }

    ThemeProvider s_themes;
    UpdateProvider s_updates;

    // The editor updater (context/infrastructure.md, section 5): the one switch of Zed's
    // `auto_update`; the channel belongs to the build.
    void buildUpdates(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::EditorUpdateSettings>(store.get<Panda::EditorUpdateSettings>());
        auto emit = [data] { commitSettings<Panda::EditorUpdateSettings>(SettingsScope::Editor, *data); };
        addToggleRow(section, "Automatic Updates", data->autoUpdate, [data, emit](bool on) {
            data->autoUpdate = on;
            emit();
        });
        section.addContent(makeFormHint(
            "Checks for a new build at startup and once an hour, downloads and installs it in the background, "
            "and asks for a restart. Help → Check for Updates looks right away."
        ));
        if (s_updates.statusLine) { section.addContent(makeFormHint(s_updates.statusLine())); }
        if (!s_updates.enabled) {
            section.addContent(makeFormHint("Updates are off in a local build: this editor was not made by the release workflow."));
        } else if (s_updates.check) {
            section.addContent(makeFormButtonRow({makeFormButton("Check for Updates", ButtonStyle::Neutral, [] {
                s_updates.check();
            })}));
        }
    }

    // The look of the editor: the plugin themes (View → Theme lists the same ones).
    void buildAppearance(CollapsibleSection &section, Panda::TypeContext &) {
        if (!s_themes.names) {
            section.addContent(makeFormHint("No themes are available."));
            return;
        }
        const std::vector<std::string> names = s_themes.names();
        const int current = s_themes.current ? s_themes.current() : -1;
        addComboRow(section, "Theme", names, current, [](int index) {
            if (s_themes.select) { s_themes.select(index); }
        });
    }

    /* ------------------------------ Project scope ------------------------------ */

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // The main world of the project: a world asset of the project (the path is kept relative, with
    // forward slashes).
    void buildProjectWorld(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::ProjectWorldSettings>(store.get<Panda::ProjectWorldSettings>());
        auto emit = [data] { commitSettings<Panda::ProjectWorldSettings>(SettingsScope::Project, *data); };
        uint32_t current = 0;
        if (Panda::AssetHandlerEditor *handler = assetHandler(); handler && !data->startWorld.empty()) {
            current = static_cast<uint32_t>(handler->getAssetId(handler->getProjectPath() / data->startWorld));
        }
        addAssetRow(section, "Start World", "scene", current, [data, emit](uint32_t id) {
            Panda::AssetHandlerEditor *handler = assetHandler();
            data->startWorld = handler && id ? handler->assetPath(Panda::AssetId(id)).generic_string() : std::string();
            emit();
        });
        section.addContent(makeFormHint("The world the editor opens when the project has no session."));
    }

    // The module set of the project: every module of the distribution, on by default; an entry
    // with enabled=false switches one off. Modules are static libraries of the build and
    // contributions are never unloaded, so a changed set takes effect after a restart of the
    // editor: the section says so and offers the restart as soon as the set differs from what
    // this session registered.
    void buildProjectModules(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::ProjectModuleSettings>(store.get<Panda::ProjectModuleSettings>());
        auto emit = [data] { commitSettings<Panda::ProjectModuleSettings>(SettingsScope::Project, *data); };
        auto restartHint = makeFormHint(
            "The module set has changed. Restart the editor to apply it: nothing is loaded or unloaded while the editor runs."
        );
        auto restartRow = makeFormButtonRow({makeFormButton("Restart Editor", ButtonStyle::Accent, [] {
            editorHost().restartEditor();
        })});
        auto updateRestart = [data, restartHint, restartRow] {
            const bool needed = Panda::ProjectModules::restartNeeded(*data);
            restartHint->setHidden(!needed);
            restartRow->setHidden(!needed);
        };
        for (const Panda::BuiltinModule &module : Panda::builtinModules()) {
            const std::string id = module.id;
            const bool enabled = Panda::ProjectModules::isEnabled(*data, id);
            addToggleRow(section, module.id, enabled, [data, emit, id, updateRestart](bool on) {
                auto &entries = data->entries;
                auto it = std::find_if(entries.begin(), entries.end(), [&](const Panda::ModuleRef &ref) {
                    return ref.id == id;
                });
                if (on) {
                    if (it != entries.end()) { entries.erase(it); } // enabled is the default — no entry
                } else if (it != entries.end()) {
                    it->enabled = false;
                } else {
                    entries.push_back({{}, id, std::string(), false});
                }
                emit();
                updateRestart();
            });
        }
        if (Panda::builtinModules().empty()) { section.addContent(makeFormHint("The distribution has no modules.")); }
        section.addContent(makeFormHint(
            "Modules are static libraries of this build, linked into the editor, the SDK and every exported game. "
            "Switching one on or off takes effect after the editor restarts."
        ));
        section.addContent(restartHint);
        section.addContent(restartRow);
        updateRestart();
    }

} // namespace

void setThemeProvider(ThemeProvider provider) {
    s_themes = std::move(provider);
}

void setUpdateProvider(UpdateProvider provider) {
    s_updates = std::move(provider);
}

void registerCoreSettingsSections(EditorExtensions &extensions) {
    extensions.settingsSections.push_back({SettingsScope::World, "WORLD", buildLighting});
    extensions.settingsSections.push_back({SettingsScope::World, "SHADOWS", buildShadows});
    extensions.settingsSections.push_back({SettingsScope::World, "OCCLUSION VOLUME", buildOcclusion});
    extensions.settingsSections.push_back({SettingsScope::World, "WIND", buildWind});
    extensions.settingsSections.push_back({SettingsScope::Editor, "VIEWPORT", buildViewport});
    extensions.settingsSections.push_back({SettingsScope::Editor, "APPEARANCE", buildAppearance});
    extensions.settingsSections.push_back({SettingsScope::Editor, "UPDATES", buildUpdates});
    extensions.settingsSections.push_back({SettingsScope::Project, "WORLD", buildProjectWorld});
    extensions.settingsSections.push_back({SettingsScope::Project, "MODULES", buildProjectModules});
}

} // namespace Editor

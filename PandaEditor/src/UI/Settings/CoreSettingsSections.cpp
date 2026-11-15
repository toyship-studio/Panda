#include "UI/Settings/CoreSettingsSections.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

#include <Fern/Fonts/FontCatalog.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/SettingsCommands.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/WorldSettings.hpp>
#include <Panda/Renderer/SceneWind.hpp>
#include <Panda/Renderer/ShaderFeatures.hpp>

#include <algorithm>
#include <functional>
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

    // The side of a texture in texels: the sizes a person would choose from. A size written into
    // the file by hand that is not one of them keeps its place in the list.
    std::shared_ptr<PropertyRow> addTexelSizeRow(
        CollapsibleSection &section, const char *title, std::vector<uint32_t> sizes, uint32_t value,
        std::function<void(uint32_t)> onChange
    ) {
        if (std::find(sizes.begin(), sizes.end(), value) == sizes.end()) {
            sizes.insert(std::upper_bound(sizes.begin(), sizes.end(), value), value);
        }
        std::vector<std::string> items;
        for (uint32_t size : sizes) { items.push_back(std::to_string(size)); }
        const int selected = static_cast<int>(std::find(sizes.begin(), sizes.end(), value) - sizes.begin());
        return addComboRow(section, title, std::move(items), selected, [sizes, onChange = std::move(onChange)](int index) {
            if (index >= 0 && index < static_cast<int>(sizes.size())) { onChange(sizes[static_cast<size_t>(index)]); }
        });
    }

    // The shadows of the world: the cascades of the sun and the tiles of the lamps.
    void buildShadows(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldGraphicsSettings>(store.get<Panda::WorldGraphicsSettings>());
        auto emit = [data] { commitSettings<Panda::WorldGraphicsSettings>(SettingsScope::World, *data); };
        addToggleRow(section, "Enabled", data->shadows.enabled, [data, emit](bool on) {
            data->shadows.enabled = on;
            emit();
        });
        addIntegerRow(section, "Cascades", 1, 4, static_cast<int>(data->shadows.cascadeCount), [data, emit](int value) {
            data->shadows.cascadeCount = static_cast<uint32_t>(value);
            emit();
        })->setHelp("The parts the shadow distance is cut into, each with a map of its own: a depth pass over the world per cascade");
        addTexelSizeRow(section, "Map Size", {256, 512, 1024, 2048, 4096}, data->shadows.tileSize, [data, emit](uint32_t value) {
            data->shadows.tileSize = value;
            emit();
        })->setHelp("The side of the map of a cascade, texels");
        addNumberRow(section, "Distance", 1.f, 4096.f, data->shadows.distance, [data, emit](float value) {
            data->shadows.distance = value;
            emit();
        })->setHelp("How far from the camera the sun casts shadows, m");
        addNumberRow(section, "Strength", 0.f, 1.f, data->shadows.strength, [data, emit](float value) {
            data->shadows.strength = value;
            emit();
        })->setHelp("0 — no shadow, 1 — the sun does not reach a shadowed point at all");
        addNumberRow(section, "Softness", 0.f, 5.f, data->shadows.softness, [data, emit](float value) {
            data->shadows.softness = value;
            emit();
        })->setHelp("The angular radius of the sun, degrees: how fast the edge of a shadow softens with the distance from its caster. 0 — an edge of a fixed width");
        addTexelSizeRow(section, "Lamp Map Size", {128, 256, 512, 1024, 2048}, data->shadows.lampTileSize, [data, emit](uint32_t value) {
            data->shadows.lampTileSize = value;
            emit();
        })->setHelp("The side of the map of a lamp that casts shadows, texels: a spot has one map, a lamp that shines all around six");
        // Biases against acne: slope — the rasterizer of the depth pass; normal offset and receiver
        // bias — in cascade texels (independent of the scene scale).
        addNumberRow(section, "Slope Bias", 0.f, 16.f, data->shadows.slopeBias, [data, emit](float value) {
            data->shadows.slopeBias = value;
            emit();
        })->setHelp("Pushes the depth of a caster away by its slope to the light: against the stripes a surface casts on itself");
        addNumberRow(section, "Normal Offset", 0.f, 8.f, data->shadows.normalOffset, [data, emit](float value) {
            data->shadows.normalOffset = value;
            emit();
        })->setHelp("Moves the point that is looked up along its normal, texels of the map: against stripes on surfaces the light grazes");
        addNumberRow(section, "Depth Bias", 0.f, 8.f, data->shadows.receiverBias, [data, emit](float value) {
            data->shadows.receiverBias = value;
            emit();
        })->setHelp("Moves the point that is looked up towards the light, texels of the map");
    }

    // Screen-space ambient occlusion: the contact darkening of the ambient light (the `ssao`
    // feature of the project).
    void buildSsao(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldGraphicsSettings>(store.get<Panda::WorldGraphicsSettings>());
        auto emit = [data] { commitSettings<Panda::WorldGraphicsSettings>(SettingsScope::World, *data); };
        addToggleRow(section, "Enabled", data->ssao.enabled, [data, emit](bool on) {
            data->ssao.enabled = on;
            emit();
        });
        addNumberRow(section, "Radius", 0.05f, 4.f, data->ssao.radius, [data, emit](float value) {
            data->ssao.radius = value;
            emit();
        })->setHelp("How far an occluder reaches, m");
        addNumberRow(section, "Intensity", 0.f, 4.f, data->ssao.intensity, [data, emit](float value) {
            data->ssao.intensity = value;
            emit();
        })->setHelp("0 — none, 1 — the occlusion as computed, above — darker");

        // The budget: directions around a pixel × depth samples along each, at half or at full
        // resolution. The world file holds the three numbers; the quality is a name for a set of
        // them, not a setting of its own.
        struct Quality {
            const char *name;
            int slices;
            int steps;
            bool fullResolution;
        };
        static const Quality QUALITIES[] = {
            {"Low", 2, 3, false}, {"Medium", 3, 4, false}, {"High", 3, 4, true}, {"Ultra", 6, 6, true}};
        const int qualityCount = static_cast<int>(std::size(QUALITIES));
        auto qualityOf = [qualityCount](const Panda::SsaoSettings &ssao) {
            for (int i = 0; i < qualityCount; ++i) {
                const Quality &quality = QUALITIES[i];
                if (ssao.slices == quality.slices && ssao.steps == quality.steps && ssao.fullResolution == quality.fullResolution) {
                    return i;
                }
            }
            return qualityCount; // "Custom"
        };
        // The rows show each other's changes; they are held by the section, the callbacks by the
        // rows: raw pointers, no cycle.
        auto refresh = std::make_shared<std::function<void()>>();
        auto changed = [emit, refresh] {
            emit();
            if (*refresh) { (*refresh)(); }
        };
        std::vector<std::string> names;
        for (const Quality &quality : QUALITIES) { names.push_back(quality.name); }
        names.push_back("Custom");
        auto qualityRow = addComboRow(section, "Quality", names, qualityOf(data->ssao), [data, changed, qualityCount](int index) {
            if (index < 0 || index >= qualityCount) { return; } // "Custom" is what the numbers are
            data->ssao.slices = QUALITIES[index].slices;
            data->ssao.steps = QUALITIES[index].steps;
            data->ssao.fullResolution = QUALITIES[index].fullResolution;
            changed();
        });
        qualityRow->setHelp("What the occlusion may cost: a set of the three numbers below");
        auto slicesRow = addIntegerRow(section, "Slices", 1, Panda::SsaoSettings::MAX_SLICES, data->ssao.slices, [data, changed](int value) {
            data->ssao.slices = value;
            changed();
        });
        slicesRow->setHelp("Directions the surroundings of a pixel are searched in: more — less grain");
        auto stepsRow = addIntegerRow(section, "Steps", 1, Panda::SsaoSettings::MAX_STEPS, data->ssao.steps, [data, changed](int value) {
            data->ssao.steps = value;
            changed();
        });
        stepsRow->setHelp("Depth samples along a direction, each way: more — a small occluder is not missed");
        auto fullRow = addToggleRow(section, "Full Resolution", data->ssao.fullResolution, [data, changed](bool on) {
            data->ssao.fullResolution = on;
            changed();
        });
        fullRow->setHelp("At the resolution of the frame instead of half of it: the occlusion follows thin details, four times the pixels");
        for (const auto &row : {slicesRow, stepsRow, fullRow}) { row->setIndent(14.f); }
        auto cost = makeFormHint({});
        section.addContent(cost);

        auto *quality = dynamic_cast<Combobox *>(qualityRow->editor().get());
        auto *slices = dynamic_cast<NumberField *>(slicesRow->editor().get());
        auto *steps = dynamic_cast<NumberField *>(stepsRow->editor().get());
        auto *full = dynamic_cast<Toggle *>(fullRow->editor().get());
        Label *costLabel = cost.get();
        *refresh = [data, qualityOf, quality, slices, steps, full, costLabel] {
            const Panda::SsaoSettings &ssao = data->ssao;
            if (quality != nullptr) { quality->setSelectedIndex(qualityOf(ssao)); }
            // A field the user is not in takes the value (a preset was chosen); setValue of the
            // field being typed into would rewrite the text under the caret.
            if (slices != nullptr && static_cast<int>(std::lround(slices->getValue())) != ssao.slices) {
                slices->setValue(static_cast<float>(ssao.slices));
            }
            if (steps != nullptr && static_cast<int>(std::lround(steps->getValue())) != ssao.steps) {
                steps->setValue(static_cast<float>(ssao.steps));
            }
            if (full != nullptr && full->isOn() != ssao.fullResolution) { full->setOn(ssao.fullResolution); }
            costLabel->setText(
                std::to_string(2 * ssao.slices * ssao.steps + 5) + " depth samples a pixel, at " +
                (ssao.fullResolution ? "the resolution of the frame." : "half the resolution of the frame.")
            );
        };
        (*refresh)();
    }

    // Fog: the air between the camera and what it sees (FogRenderer). No feature of the project
    // behind it: a world without fog runs no pass.
    void buildFog(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::WorldGraphicsSettings>(store.get<Panda::WorldGraphicsSettings>());
        auto emit = [data] { commitSettings<Panda::WorldGraphicsSettings>(SettingsScope::World, *data); };
        addToggleRow(section, "Enabled", data->fog.enabled, [data, emit](bool on) {
            data->fog.enabled = on;
            emit();
        });
        auto density = addNumberRow(section, "Density", 0.f, 1.f, data->fog.density, [data, emit](float value) {
            data->fog.density = value;
            emit();
        });
        density->setHelp("The light a meter of air takes away at the base height: 0.002 — a hazy day, 0.01 — a misty one, 0.1 — a thick fog");
        if (auto *field = dynamic_cast<NumberField *>(density->editor().get())) { field->setStep(0.001f); }
        addNumberRow(section, "Height Falloff", 0.f, 2000.f, data->fog.heightFalloff, [data, emit](float value) {
            data->fog.heightFalloff = value;
            emit();
        })->setHelp("Every this many meters up the fog is half as dense; 0 — the same at every height");
        addNumberRow(section, "Base Height", -10000.f, 10000.f, data->fog.baseHeight, [data, emit](float value) {
            data->fog.baseHeight = value;
            emit();
        })->setHelp("The height the density is given at, m");
        addNumberRow(section, "Start Distance", 0.f, 10000.f, data->fog.startDistance, [data, emit](float value) {
            data->fog.startDistance = value;
            emit();
        })->setHelp("The air closer than this to the camera is clear, m");
        addNumberRow(section, "Max Opacity", 0.f, 1.f, data->fog.maxOpacity, [data, emit](float value) {
            data->fog.maxOpacity = value;
            emit();
        })->setHelp("The most the fog hides: below 1 the far things stay visible through it");
        addColorRow(section, "Tint", data->fog.tint, [data, emit](const Panda::Color &color) {
            data->fog.tint = color;
            emit();
        })->setHelp("Multiplies the light the fog takes from the sky of the world; white — the color of the sky near the horizon");
        addNumberRow(section, "Sun Scatter", 0.f, 4.f, data->fog.sunScatter, [data, emit](float value) {
            data->fog.sunScatter = value;
            emit();
        })->setHelp("The glow of the fog around the sun; 0 — none");
        addToggleRow(section, "Affect Sky", data->fog.affectSky, [data, emit](bool on) {
            data->fog.affectSky = on;
            emit();
        })->setHelp("The sky is behind all the air of its direction: the horizon dissolves in the fog, the zenith stays");
        section.addContent(makeFormHint("The fog is lit by the world: its color follows the environment map and the sun."));
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
        addIntegerRow(section, "Window Chunks", 2, 8, data->occlusion.windowChunks, [data, emit](int value) {
            data->occlusion.windowChunks = value;
            emit();
        })->setHelp("Chunks per side of the window around the camera; a chunk is 64 cells");
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
        addToggleRow(section, "Grid", data->showGrid, [data, emit](bool on) {
            data->showGrid = on;
            emit();
        });
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
        })->setHelp("How fast the editor camera flies, m/s");
        addToggleRow(section, "Wireframe", data->wireframe, [data, emit](bool on) {
            data->wireframe = on;
            emit();
        });
    }

    ThemeProvider s_themes;
    UpdateProvider s_updates;

    // The external text editor: the documents of the project are text files, and "Open in External
    // Editor" (a tab, an asset, the settings files) hands the file to this program. The list is
    // the common editors by their command; any other command is written into settings.json.
    void buildExternalTools(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::EditorExternalToolsSettings>(store.get<Panda::EditorExternalToolsSettings>());
        auto emit = [data] { commitSettings<Panda::EditorExternalToolsSettings>(SettingsScope::Editor, *data); };
        static const std::pair<const char *, const char *> EDITORS[] = {
            {"System Default", ""}, {"Zed", "zed"}, {"Visual Studio Code", "code"},
            {"Cursor", "cursor"},   {"Sublime Text", "subl"},
        };
        std::vector<std::string> items;
        int selected = -1;
        for (const auto &[title, command] : EDITORS) {
            if (data->editor == command) { selected = static_cast<int>(items.size()); }
            items.push_back(title);
        }
        if (selected < 0) { // a command of the user's own, from settings.json
            selected = static_cast<int>(items.size());
            items.push_back(data->editor);
        }
        const std::string custom = data->editor;
        addComboRow(section, "Text Editor", items, selected, [data, emit, custom](int index) {
            const int known = static_cast<int>(std::size(EDITORS));
            data->editor = index >= 0 && index < known ? EDITORS[index].second : custom;
            emit();
        });
        section.addContent(makeFormHint(
            "Another program: \"externalTools\": {\"editor\": \"<command or path>\"} in the settings file."
        ));
    }

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
            "Checks at startup and once an hour, installs in the background and asks for a restart. "
            "Plugins update the same way."
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

    // The look of the editor: the plugin themes (the Plugins tab applies the same ones from the cards)
    // and the fonts — any family installed on this machine (Fern::Fonts), the bundled ones by default,
    // the size of the UI text relative to the 16 the editor is designed at.
    void buildAppearance(CollapsibleSection &section, Panda::TypeContext &store) {
        if (s_themes.names) {
            const std::vector<std::string> names = s_themes.names();
            const int current = s_themes.current ? s_themes.current() : -1;
            addComboRow(section, "Theme", names, current, [](int index) {
                if (s_themes.select) { s_themes.select(index); }
            });
        } else {
            section.addContent(makeFormHint("No themes are available."));
        }

        const Panda::EditorAppearanceSettings appearance = store.get<Panda::EditorAppearanceSettings>();
        const std::vector<std::string> installed = Fern::Fonts::families();
        // A font row: the bundled face first, then every installed family; the choice is written into the
        // appearance settings as the family name (empty — bundled), read anew at commit time so that a
        // theme chosen meanwhile is kept.
        auto fontRow = [&section, &installed](
                           const char *title, const char *bundledName, const std::string &chosen,
                           std::function<void(Panda::EditorAppearanceSettings &, const std::string &)> assign
                       ) {
            std::vector<std::string> items;
            items.push_back(std::string(bundledName) + " (default)");
            items.insert(items.end(), installed.begin(), installed.end());
            int selected = 0;
            for (size_t i = 0; i < installed.size(); ++i) {
                if (installed[i] == chosen) { selected = static_cast<int>(i) + 1; }
            }
            addComboRow(section, title, items, selected, [installed, assign](int index) {
                auto data = editorHost().settings(SettingsScope::Editor).get<Panda::EditorAppearanceSettings>();
                assign(data, index <= 0 ? std::string() : installed[static_cast<size_t>(index) - 1]);
                commitSettings<Panda::EditorAppearanceSettings>(SettingsScope::Editor, data);
            });
        };
        fontRow("UI Font", "Inter Display", appearance.uiFont, [](Panda::EditorAppearanceSettings &data, const std::string &family) {
            data.uiFont = family;
        });
        fontRow("Mono Font", "JetBrains Mono", appearance.monoFont, [](Panda::EditorAppearanceSettings &data, const std::string &family) {
            data.monoFont = family;
        });
        addIntegerRow(section, "UI Font Size", 12, 20, static_cast<int>(std::lround(appearance.uiFontSize)), [](int value) {
            auto data = editorHost().settings(SettingsScope::Editor).get<Panda::EditorAppearanceSettings>();
            data.uiFontSize = static_cast<float>(value);
            commitSettings<Panda::EditorAppearanceSettings>(SettingsScope::Editor, data);
        })->setHelp("The size of the text of the editor; it is designed at 16");
        section.addContent(makeFormHint("Any font installed on this computer. A character the font lacks is drawn with the bundled one."));
    }

    /* ------------------------------ Project scope ------------------------------ */


    // The main world of the project: a world asset of the project (the path is kept relative, with
    // forward slashes).
    void buildProjectWorld(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::ProjectWorldSettings>(store.get<Panda::ProjectWorldSettings>());
        auto emit = [data] { commitSettings<Panda::ProjectWorldSettings>(SettingsScope::Project, *data); };
        uint32_t current = 0;
        if (Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current(); handler && !data->startWorld.empty()) {
            current = static_cast<uint32_t>(handler->getAssetId(handler->getProjectPath() / data->startWorld));
        }
        addAssetRow(section, "Start World", "world", current, [data, emit](uint32_t id) {
            Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
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
            restartHint->setCollapsed(!needed);
            restartRow->setCollapsed(!needed);
        };
        for (const Panda::BuiltinModule &module : Panda::builtinModules()) {
            const std::string id = module.id;
            const bool enabled = Panda::ProjectModules::isEnabled(*data, id);
            auto row = addToggleRow(section, module.name, enabled, [data, emit, id, updateRestart](bool on) {
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
            row->setDescription(std::string(module.id) + " · " + module.version); // what project.json names
        }
        if (Panda::builtinModules().empty()) { section.addContent(makeFormHint("The distribution has no modules.")); }
        section.addContent(makeFormHint(
            "Modules are linked into the editor and every exported game. A change takes effect after the editor restarts."
        ));
        section.addContent(restartHint);
        section.addContent(restartRow);
        updateRestart();
    }

    // The shader features of the project (context/shader-features.md): the standard scene data the
    // materials get. A feature that is off is not computed, not bound and not compiled into the
    // shaders. The set applies when the project settings are saved: the scene shaders of the engine
    // and the project shaders recompile for it.
    void buildProjectRendering(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::ProjectRenderingSettings>(store.get<Panda::ProjectRenderingSettings>());
        auto emit = [data] { commitSettings<Panda::ProjectRenderingSettings>(SettingsScope::Project, *data); };
        const Panda::ShaderFeatureSet current = Panda::ShaderFeatureSet::fromNames(data->features);
        for (const Panda::ShaderFeatureInfo &info : Panda::ShaderFeatures::catalog()) {
            const Panda::ShaderFeature feature = info.feature;
            addToggleRow(section, info.title, current.has(feature), [data, emit, feature](bool on) {
                // The dependencies come along: shadows and occlusion read the sun of the lights
                // (the rows of the dependent features catch up when the section is rebuilt).
                Panda::ShaderFeatureSet set = Panda::ShaderFeatureSet::fromNames(data->features);
                set.set(feature, on);
                data->features = set.names();
                emit();
            })->setDescription(info.cost);
        }
        section.addContent(makeFormHint(
            "Shadows and occlusion need lights. Applied on save: the shaders of the project recompile for the set."
        ));
    }

    // The MCP server of the editor for this project: an AI agent in the project folder looks at
    // the editor and drives it through it (App/EditorMcp). Follows the section at once.
    void buildProjectMcp(CollapsibleSection &section, Panda::TypeContext &store) {
        auto data = std::make_shared<Panda::ProjectMcpSettings>(store.get<Panda::ProjectMcpSettings>());
        auto emit = [data] { commitSettings<Panda::ProjectMcpSettings>(SettingsScope::Project, *data); };
        addToggleRow(section, "Enabled", data->enabled, [data, emit](bool on) {
            data->enabled = on;
            emit();
        });
        addIntegerRow(section, "Port", 1024, 65535, data->port, [data, emit](int value) {
            data->port = value;
            emit();
        });
        section.addContent(makeFormHint(
            "An AI agent (Claude Code and other MCP clients) sees the editor of this project and drives it: "
            "screenshots of the viewports, the log, Reload, Play. The address is http://127.0.0.1:<port>/mcp, "
            "local programs only; .mcp.json of the project names the same port."
        ));
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
    extensions.settingsSections.push_back({SettingsScope::World, "AMBIENT OCCLUSION", buildSsao});
    extensions.settingsSections.push_back({SettingsScope::World, "FOG", buildFog});
    extensions.settingsSections.push_back({SettingsScope::World, "OCCLUSION VOLUME", buildOcclusion});
    extensions.settingsSections.push_back({SettingsScope::World, "WIND", buildWind});
    extensions.settingsSections.push_back({SettingsScope::Editor, "VIEWPORT", buildViewport});
    extensions.settingsSections.push_back({SettingsScope::Editor, "APPEARANCE", buildAppearance});
    extensions.settingsSections.push_back({SettingsScope::Editor, "EXTERNAL TOOLS", buildExternalTools});
    extensions.settingsSections.push_back({SettingsScope::Editor, "UPDATES", buildUpdates});
    extensions.settingsSections.push_back({SettingsScope::Project, "WORLD", buildProjectWorld});
    extensions.settingsSections.push_back({SettingsScope::Project, "RENDERING", buildProjectRendering});
    extensions.settingsSections.push_back({SettingsScope::Project, "MODULES", buildProjectModules});
    extensions.settingsSections.push_back({SettingsScope::Project, "MCP SERVER", buildProjectMcp});
}

} // namespace Editor

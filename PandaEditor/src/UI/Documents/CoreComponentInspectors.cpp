#include "UI/Documents/CoreComponentInspectors.hpp"

#include <PandaEditorKit/ComponentForm.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/World.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float FORM_INDENT = 14.f; // a row that belongs to the toggle above it

    // Each section edits a copy of its component and writes it whole (ComponentForm). The rows are
    // the kit's (FormWidgets): a number is typed or scrubbed by its label, a row that does not
    // apply now is out of the form, the tooltip of a label says what the field is.

    void buildCamera(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using Cam = Panda::CameraComponent;
        using Projection = Panda::WorldCamera::ProjectionType;
        const ComponentForm<Cam> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        // The rows that depend on another field are shown by this, set once they all exist. The
        // rows are held by the section, the callbacks by the rows: raw pointers, no cycle.
        auto refresh = std::make_shared<std::function<void()>>();
        auto changed = [emit, refresh] {
            emit();
            if (*refresh) { (*refresh)(); }
        };

        addMainRow(section, form, "camera", "The camera the game looks through; another one is a spare (a script makes it the main one)");
        addComboRow(
            section, "Projection", {"Perspective", "Orthographic"}, static_cast<int>(data->camera.getProjectionType()),
            [data, changed](int index) {
                data->camera.setProjectionType(static_cast<Projection>(index));
                changed();
            },
            "camera/projectionType"
        );
        PropertyRow *fieldOfView = addNumberRow(
            section, "Field of View", 1.f, 179.f, data->camera.getFieldOfView(),
            [data, emit](float v) { data->camera.setFieldOfView(v); emit(); },
            "camera/fieldOfView"
        ).get();
        fieldOfView->setHelp("The vertical angle of the view, degrees");
        PropertyRow *orthoSize = addNumberRow(
            section, "Size", 0.f, 1000.f, data->camera.getOrthoSize(),
            [data, emit](float v) { data->camera.setOrthoSize(v); emit(); },
            "camera/orthoSize"
        ).get();
        orthoSize->setHelp("The height of the view, world units");

        // How the frame reaches the display: the exposure, the curve, the glow, the smoothing.
        addNumberRow(section, "Exposure", -8.f, 8.f, data->exposure, [data, emit](float v) { data->exposure = v; emit(); },
                     "exposure")
            ->setHelp("Exposure compensation in stops: +1 is twice as bright. With auto exposure — a correction on top of it");
        addToggleRow(section, "Auto Exposure", data->autoExposureEnabled,
                     [data, changed](bool on) { data->autoExposureEnabled = on; changed(); },
                     "autoExposureEnabled")
            ->setHelp("The exposure follows the brightness of the frame, as an eye adapts");
        PropertyRow *exposureKey = addNumberRow(
            section, "Key", 0.01f, 1.f, data->autoExposureKey, [data, emit](float v) { data->autoExposureKey = v; emit(); },
            "autoExposureKey"
        ).get();
        exposureKey->setHelp("The average brightness the frame is brought to (0.18 — a gray card)");
        PropertyRow *exposureSpeed = addNumberRow(
            section, "Speed", 0.1f, 20.f, data->autoExposureSpeed,
            [data, emit](float v) { data->autoExposureSpeed = v; emit(); },
            "autoExposureSpeed"
        ).get();
        exposureSpeed->setHelp("How fast the exposure adapts, 1/s");
        PropertyRow *exposureMin = addNumberRow(
            section, "Min", 0.01f, 4.f, data->autoExposureMin, [data, emit](float v) { data->autoExposureMin = v; emit(); },
            "autoExposureMin"
        ).get();
        exposureMin->setHelp("The lowest exposure the adaptation may reach");
        PropertyRow *exposureMax = addNumberRow(
            section, "Max", 0.25f, 16.f, data->autoExposureMax, [data, emit](float v) { data->autoExposureMax = v; emit(); },
            "autoExposureMax"
        ).get();
        exposureMax->setHelp("The highest exposure the adaptation may reach");
        addComboRow(
            section, "Tonemap", {"AgX", "ACES", "Neutral", "Reinhard", "None"}, static_cast<int>(data->tonemap),
            [data, emit](int index) {
                data->tonemap = static_cast<Panda::Tonemap>(index);
                emit();
            },
            "tonemap"
        )->setHelp("The curve that brings the light of the frame into the range of a display");
        // Built-in camera bloom (a pyramid in the engine; the glow source is the emissive of materials).
        addToggleRow(section, "Bloom", data->bloomEnabled, [data, changed](bool on) { data->bloomEnabled = on; changed(); },
                     "bloomEnabled")
            ->setHelp("The glow around what emits light (the emissive of materials)");
        PropertyRow *bloomIntensity = addNumberRow(
            section, "Intensity", 0.f, 100.f, data->bloomIntensity, [data, emit](float v) { data->bloomIntensity = v; emit(); },
            "bloomIntensity"
        ).get();
        PropertyRow *bloomRadius = addNumberRow(
            section, "Radius", 0.f, 4.f, data->bloomRadius, [data, emit](float v) { data->bloomRadius = v; emit(); },
            "bloomRadius"
        ).get();
        bloomRadius->setHelp("How far the glow spreads (0.5..2)");
        addToggleRow(section, "Antialiasing", data->antialiasing, [data, emit](bool on) { data->antialiasing = on; emit(); },
                     "antialiasing")
            ->setHelp("Smooths the edges of the finished frame (FXAA)");
        // The contact darkening of the ambient light (the `ssao` feature of the project; what it
        // may cost — Project Settings → Ambient Occlusion).
        addToggleRow(section, "Ambient Occlusion", data->ambientOcclusion,
                     [data, changed](bool on) { data->ambientOcclusion = on; changed(); }, "ambientOcclusion")
            ->setHelp("Darkens the light of the sky where things meet: corners, contacts, under objects");
        PropertyRow *occlusionRadius = addNumberRow(
            section, "Radius", 0.05f, 4.f, data->ambientOcclusionRadius,
            [data, emit](float v) { data->ambientOcclusionRadius = v; emit(); }, "ambientOcclusionRadius"
        ).get();
        occlusionRadius->setHelp("How far an occluder reaches, m");
        PropertyRow *occlusionIntensity = addNumberRow(
            section, "Intensity", 0.f, 4.f, data->ambientOcclusionIntensity,
            [data, emit](float v) { data->ambientOcclusionIntensity = v; emit(); }, "ambientOcclusionIntensity"
        ).get();
        occlusionIntensity->setHelp("0 — none, 1 — as computed, above — darker");

        addAssetRow(section, "Postprocessing", "postfx", static_cast<uint32_t>(data->postprocessing.getId()),
                    [data, changed](uint32_t id) { data->postprocessing = assetRefOf(id); changed(); }, "postprocessing");
        PropertyRow *postfxEnabled = addToggleRow(
            section, "Postprocessing Enabled", data->postprocessingEnabled,
            [data, emit](bool on) { data->postprocessingEnabled = on; emit(); },
            "postprocessingEnabled"
        ).get();
        postfxEnabled->setHelp("The passes of the postprocessing asset run; a script turns them on and off");

        for (PropertyRow *row : {exposureKey, exposureSpeed, exposureMin, exposureMax, bloomIntensity, bloomRadius,
                                 occlusionRadius, occlusionIntensity}) {
            row->setIndent(FORM_INDENT);
        }
        *refresh = [=] {
            const bool perspective = data->camera.getProjectionType() == Projection::PERSPECTIVE;
            fieldOfView->setVisible(perspective);
            orthoSize->setVisible(!perspective);
            for (PropertyRow *row : {exposureKey, exposureSpeed, exposureMin, exposureMax}) {
                row->setVisible(data->autoExposureEnabled);
            }
            bloomIntensity->setVisible(data->bloomEnabled);
            bloomRadius->setVisible(data->bloomEnabled);
            occlusionRadius->setVisible(data->ambientOcclusion);
            occlusionIntensity->setVisible(data->ambientOcclusion);
            postfxEnabled->setVisible(static_cast<bool>(data->postprocessing.getId()));
        };
        (*refresh)();
    }

    void buildDirectionalLight(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using DL = Panda::DirectionalLightComponent;
        const ComponentForm<DL> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        auto refresh = std::make_shared<std::function<void()>>();

        addMainRow(section, form, "sun", "The sun of the world: it lights it and casts the shadows; another one is a spare (the moon of a night)");
        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    "color");
        addNumberRow(section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                     "intensity")
            ->setHelp("What a white matte surface facing the sun sends back");
        addToggleRow(section, "Cast Shadows", data->castShadows,
                     [data, emit, refresh](bool on) {
                         data->castShadows = on;
                         emit();
                         if (*refresh) { (*refresh)(); }
                     },
                     "castShadows")
            ->setHelp("The cascaded shadows of the sun; their maps are the project's (Project Settings → Shadows)");
        PropertyRow *strength = addNumberRow(
            section, "Strength", 0.f, 1.f, data->shadowStrength, [data, emit](float v) { data->shadowStrength = v; emit(); },
            "shadowStrength"
        ).get();
        strength->setHelp("0 — no shadow, 1 — the sun does not reach a shadowed point at all");
        PropertyRow *softness = addNumberRow(
            section, "Softness", 0.f, 5.f, data->shadowSoftness, [data, emit](float v) { data->shadowSoftness = v; emit(); },
            "shadowSoftness"
        ).get();
        softness->setHelp("The angular radius of the sun, degrees: how fast the edge of a shadow softens with the distance from its caster. 0 — an edge of a fixed width");
        for (PropertyRow *row : {strength, softness}) { row->setIndent(FORM_INDENT); }
        addToggleRow(section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                     "isEnabled");
        *refresh = [data, strength, softness] {
            strength->setVisible(data->castShadows);
            softness->setVisible(data->castShadows);
        };
        (*refresh)();
    }

    void buildPointLight(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using PL = Panda::PointLightComponent;
        const ComponentForm<PL> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        auto refresh = std::make_shared<std::function<void()>>();

        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    "color");
        addNumberRow(section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                     "intensity")
            ->setHelp("What a white matte surface 1 m away, facing the lamp, sends back");
        addNumberRow(section, "Radius", 0.f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                     "radius")
            ->setHelp("Where the light ends, m: it falls with the square of the distance and reaches zero here");
        // A cone makes the lamp a spot that shines along the -Z of the entity; 0 — all around.
        addNumberRow(section, "Cone Angle", 0.f, 179.f, data->coneAngle,
                     [data, emit, refresh](float v) {
                         data->coneAngle = v;
                         emit();
                         if (*refresh) { (*refresh)(); }
                     },
                     "coneAngle")
            ->setHelp("0 — the lamp shines all around. Above — a spot along the -Z of the entity; the full angle of its cone, degrees");
        PropertyRow *softness = addNumberRow(
            section, "Softness", 0.f, 1.f, data->coneSoftness, [data, emit](float v) { data->coneSoftness = v; emit(); },
            "coneSoftness"
        ).get();
        softness->setHelp("The share of the cone over which the light fades to its edge");
        softness->setIndent(FORM_INDENT);
        addToggleRow(section, "Cast Shadows", data->castShadows, [data, emit](bool on) { data->castShadows = on; emit(); },
                     "castShadows")
            ->setHelp("A spot costs one more pass over the world every frame, a lamp that shines all around — six");
        addToggleRow(section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                     "isEnabled");

        *refresh = [data, softness] { softness->setVisible(data->coneAngle > 0.f); };
        (*refresh)();
    }

    void buildSpriteRenderer(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using SR = Panda::SpriteRendererComponent;
        const ComponentForm<SR> form(context);
        const auto data = form.data;
        const auto emit = form.commit;

        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    "color", /*alpha*/ true);
        addAssetRow(section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()),
                    [data, emit](uint32_t id) { data->texture = assetRefOf(id); emit(); }, "texture");
        // A sprite sheet: the texture is a grid of frames and the sprite shows one of them.
        addIntegerRow(section, "Columns", 1, 30, data->cols, [data, emit](int v) { data->cols = v; emit(); },
                      "cols")
            ->setHelp("The frames of the sprite sheet in a row of the texture");
        addIntegerRow(section, "Rows", 1, 30, data->rows, [data, emit](int v) { data->rows = v; emit(); },
                      "rows")
            ->setHelp("The rows of frames in the texture");
        addIntegerRow(section, "Index", 0, 900, data->index, [data, emit](int v) { data->index = v; emit(); },
                      "index")
            ->setHelp("The frame shown, counted from 0 along the rows");
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); }, "material");
    }

    void buildRuntimeMesh(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::RuntimeMeshComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addAssetRow(section, "Mesh", "mesh", static_cast<uint32_t>(data->runtimeMesh.getId()),
                    [data, emit](uint32_t id) { data->runtimeMesh = assetRefOf(id); emit(); });
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    }

    void buildPrimitiveMesh(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using PM = Panda::PrimitiveMeshComponent;
        const ComponentForm<PM> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addComboRow(
            section, "Shape", {"Cube", "Sphere", "Capsule", "Plane", "Cylinder"}, static_cast<int>(data->kind),
            [data, emit](int index) {
                data->kind = static_cast<Panda::PrimitiveMeshKind>(index);
                emit();
            },
            "kind"
        );
        // An empty material = the built-in checker (the sky pattern: empty means the engine material).
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); }, "material");
    }

    void buildImportedSubmesh(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::ImportedSubmeshComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addAssetRow(section, "Mesh", "mesh", static_cast<uint32_t>(data->importedMesh.getId()),
                    [data, emit](uint32_t id) { data->importedMesh = assetRefOf(id); emit(); }, "importedMesh");
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); }, "overrideMaterial");
        section.addContent(makeFormHint("Empty — the material the model was imported with."));
    }

    // The sky of the world: what is drawn behind everything, the light of the sky and the fog.
    void buildSky(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::SkyComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        auto refresh = std::make_shared<std::function<void()>>();
        auto changed = [emit, refresh] {
            emit();
            if (*refresh) { (*refresh)(); }
        };

        addMainRow(section, form, "sky", "The sky of the world; another one is a spare (a script makes it the main one: a sky of the night)");
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); }, "material")
            ->setHelp("Empty — the default sky: the environment map, or the built-in gradient");
        // The environment map (equirect, png/jpg/.hdr): the environmentMap system texture — the
        // default sky and PBR reflections, and the light of the sky; empty — the built-in gradient.
        addAssetRow(section, "Environment Map", "texture", static_cast<uint32_t>(data->environmentMap.getId()),
                    [data, emit](uint32_t id) { data->environmentMap = assetRefOf(id); emit(); }, "environmentMap")
            ->setHelp("The sky and the light it gives: an equirectangular picture (LDR or .hdr). Empty — the built-in gradient");
        addNumberRow(section, "Environment Intensity", 0.f, 64.f, data->environmentIntensity,
                     [data, emit](float v) { data->environmentIntensity = v; emit(); }, "environmentIntensity")
            ->setHelp("The exposure of the environment map: the sky and its light together");
        addColorRow(section, "Ambient Color", data->ambientColor,
                    [data, emit](const Panda::Color &color) { data->ambientColor = color; emit(); }, "ambientColor");
        addNumberRow(section, "Ambient Intensity", 0.f, 10.f, data->ambientIntensity,
                     [data, emit](float v) { data->ambientIntensity = v; emit(); }, "ambientIntensity")
            ->setHelp("A flat light from everywhere, added to the light of the sky");

        // The fog: the air between the camera and what it sees (FogRenderer). No feature of the
        // project behind it: a sky without fog runs no pass.
        addToggleRow(section, "Fog", data->fog.enabled, [data, changed](bool on) { data->fog.enabled = on; changed(); }, "fog/enabled")
            ->setHelp("The air between the camera and what it sees; it is lit by the sky and the sun");
        std::vector<PropertyRow *> fogRows;
        auto density = addNumberRow(section, "Density", 0.f, 1.f, data->fog.density,
                                    [data, emit](float v) { data->fog.density = v; emit(); }, "fog/density");
        density->setHelp("The light a meter of air takes away at the base height: 0.002 — a hazy day, 0.01 — a misty one, 0.1 — a thick fog");
        if (auto *field = dynamic_cast<NumberField *>(density->editor().get())) { field->setStep(0.001f); }
        fogRows.push_back(density.get());
        fogRows.push_back(addNumberRow(section, "Height Falloff", 0.f, 2000.f, data->fog.heightFalloff,
                                       [data, emit](float v) { data->fog.heightFalloff = v; emit(); }, "fog/heightFalloff").get());
        fogRows.back()->setHelp("Every this many meters up the fog is half as dense; 0 — the same at every height");
        fogRows.push_back(addNumberRow(section, "Base Height", -10000.f, 10000.f, data->fog.baseHeight,
                                       [data, emit](float v) { data->fog.baseHeight = v; emit(); }, "fog/baseHeight").get());
        fogRows.back()->setHelp("The height the density is given at, m");
        fogRows.push_back(addNumberRow(section, "Start Distance", 0.f, 10000.f, data->fog.startDistance,
                                       [data, emit](float v) { data->fog.startDistance = v; emit(); }, "fog/startDistance").get());
        fogRows.back()->setHelp("The air closer than this to the camera is clear, m");
        fogRows.push_back(addNumberRow(section, "Max Opacity", 0.f, 1.f, data->fog.maxOpacity,
                                       [data, emit](float v) { data->fog.maxOpacity = v; emit(); }, "fog/maxOpacity").get());
        fogRows.back()->setHelp("The most the fog hides: below 1 the far things stay visible through it");
        fogRows.push_back(addColorRow(section, "Tint", data->fog.tint,
                                      [data, emit](const Panda::Color &color) { data->fog.tint = color; emit(); }, "fog/tint").get());
        fogRows.back()->setHelp("Multiplies the light the fog takes from the sky; white — the color of the sky near the horizon");
        fogRows.push_back(addNumberRow(section, "Sun Scatter", 0.f, 4.f, data->fog.sunScatter,
                                       [data, emit](float v) { data->fog.sunScatter = v; emit(); }, "fog/sunScatter").get());
        fogRows.back()->setHelp("The glow of the fog around the sun; 0 — none");
        fogRows.push_back(addToggleRow(section, "Affect Sky", data->fog.affectSky,
                                       [data, emit](bool on) { data->fog.affectSky = on; emit(); }, "fog/affectSky").get());
        fogRows.back()->setHelp("The sky is behind all the air of its direction: the horizon dissolves in the fog, the zenith stays");
        for (PropertyRow *row : fogRows) { row->setIndent(FORM_INDENT); }
        *refresh = [data, fogRows] {
            for (PropertyRow *row : fogRows) { row->setVisible(data->fog.enabled); }
        };
        (*refresh)();
    }

    // The wind of the world: it sways every plant.
    void buildWind(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::WindComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addMainRow(section, form, "wind", "The wind of the world; another one is a spare (a script makes it the main one: a storm)");
        addNumberRow(section, "Strength", 0.f, 1.f, data->strength, [data, emit](float v) { data->strength = v; emit(); }, "strength")
            ->setHelp("0 — calm, 1 — a storm");
        addNumberRow(section, "Speed", 0.f, 8.f, data->speed, [data, emit](float v) { data->speed = v; emit(); }, "speed")
            ->setHelp("The tempo of the gusts and the flutter");
        addNumberRow(section, "Gustiness", 0.f, 1.f, data->gustiness, [data, emit](float v) { data->gustiness = v; emit(); }, "gustiness")
            ->setHelp("0 — a steady flow, 1 — lulls and squalls");
        addNumberRow(section, "Direction", 0.f, 360.f, data->directionDegrees,
                     [data, emit](float v) { data->directionDegrees = v; emit(); }, "directionDegrees")
            ->setHelp("Where it blows to, degrees in the ground plane: 0 — along +X");
    }

    // component — the name of the type in the registry of the engine (the key of the world file).
    template<typename T>
    ComponentInspector inspectorOf(
        const char *component, const char *displayName, const char *icon,
        void (*build)(CollapsibleSection &, const ComponentInspectorContext &), bool characteristic = true
    ) {
        ComponentInspector inspector;
        inspector.component = component;
        inspector.displayName = displayName;
        inspector.icon = icon;
        inspector.characteristic = characteristic;
        inspector.setPresence = setComponentPresence<T>;
        inspector.build = build;
        return inspector;
    }

} // namespace

void registerCoreComponentInspectors(EditorExtensions &extensions) {
    ComponentInspectorRegistry &inspectors = extensions.componentInspectors;
    // What an entity is — its icon — comes first; what many kinds of entities carry (a mesh, a
    // sprite, a sky) gives its icon to an entity that has nothing more telling.
    using namespace Panda;
    inspectors.add(inspectorOf<CameraComponent>("cameraComponent", "Camera", Ic::Camera, buildCamera));
    inspectors.add(inspectorOf<DirectionalLightComponent>(
        "directionalLightComponent", "Directional Light", Ic::LightSun, buildDirectionalLight
    ));
    inspectors.add(inspectorOf<PointLightComponent>("pointLightComponent", "Point Light", Ic::LightPoint, buildPointLight));
    inspectors.add(inspectorOf<SkyComponent>("skyComponent", "Sky", Ic::Globe, buildSky, false));
    inspectors.add(inspectorOf<WindComponent>("windComponent", "Wind", Ic::Wind, buildWind));
    inspectors.add(inspectorOf<SpriteRendererComponent>(
        "spriteRendererComponent", "Sprite Renderer", Ic::Sprite, buildSpriteRenderer, false
    ));
    inspectors.add(inspectorOf<RuntimeMeshComponent>("runtimeMeshComponent", "Runtime Mesh", Ic::Mesh, buildRuntimeMesh, false));
    inspectors.add(inspectorOf<PrimitiveMeshComponent>(
        "primitiveMeshComponent", "Primitive Mesh", Ic::Mesh, buildPrimitiveMesh, false
    ));
    inspectors.add(inspectorOf<ImportedSubmeshComponent>(
        "importedSubmeshComponent", "Imported Submesh", Ic::Mesh, buildImportedSubmesh, false
    ));
}

} // namespace Editor

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

        addToggleRow(section, "Primary", data->isPrimary, [data, emit](bool on) { data->isPrimary = on; emit(); },
                     form.mixed([](Cam &c) { return c.isPrimary; }))
            ->setHelp("The camera the game looks through");
        addComboRow(
            section, "Projection", {"Perspective", "Orthographic"}, static_cast<int>(data->camera.getProjectionType()),
            [data, changed](int index) {
                data->camera.setProjectionType(static_cast<Projection>(index));
                changed();
            },
            form.mixed([](Cam &c) { return static_cast<int>(c.camera.getProjectionType()); })
        );
        PropertyRow *fieldOfView = addNumberRow(
            section, "Field of View", 1.f, 179.f, data->camera.getFieldOfView(),
            [data, emit](float v) { data->camera.setFieldOfView(v); emit(); },
            form.mixed([](Cam &c) { return c.camera.getFieldOfView(); })
        ).get();
        fieldOfView->setHelp("The vertical angle of the view, degrees");
        PropertyRow *orthoSize = addNumberRow(
            section, "Size", 0.f, 1000.f, data->camera.getOrthoSize(),
            [data, emit](float v) { data->camera.setOrthoSize(v); emit(); },
            form.mixed([](Cam &c) { return c.camera.getOrthoSize(); })
        ).get();
        orthoSize->setHelp("The height of the view, world units");

        // How the frame reaches the display: the exposure, the curve, the glow, the smoothing.
        addNumberRow(section, "Exposure", -8.f, 8.f, data->exposure, [data, emit](float v) { data->exposure = v; emit(); },
                     form.mixed([](Cam &c) { return c.exposure; }))
            ->setHelp("Exposure compensation in stops: +1 is twice as bright. With auto exposure — a correction on top of it");
        addToggleRow(section, "Auto Exposure", data->autoExposureEnabled,
                     [data, changed](bool on) { data->autoExposureEnabled = on; changed(); },
                     form.mixed([](Cam &c) { return c.autoExposureEnabled; }))
            ->setHelp("The exposure follows the brightness of the frame, as an eye adapts");
        PropertyRow *exposureKey = addNumberRow(
            section, "Key", 0.01f, 1.f, data->autoExposureKey, [data, emit](float v) { data->autoExposureKey = v; emit(); },
            form.mixed([](Cam &c) { return c.autoExposureKey; })
        ).get();
        exposureKey->setHelp("The average brightness the frame is brought to (0.18 — a gray card)");
        PropertyRow *exposureSpeed = addNumberRow(
            section, "Speed", 0.1f, 20.f, data->autoExposureSpeed,
            [data, emit](float v) { data->autoExposureSpeed = v; emit(); },
            form.mixed([](Cam &c) { return c.autoExposureSpeed; })
        ).get();
        exposureSpeed->setHelp("How fast the exposure adapts, 1/s");
        PropertyRow *exposureMin = addNumberRow(
            section, "Min", 0.01f, 4.f, data->autoExposureMin, [data, emit](float v) { data->autoExposureMin = v; emit(); },
            form.mixed([](Cam &c) { return c.autoExposureMin; })
        ).get();
        exposureMin->setHelp("The lowest exposure the adaptation may reach");
        PropertyRow *exposureMax = addNumberRow(
            section, "Max", 0.25f, 16.f, data->autoExposureMax, [data, emit](float v) { data->autoExposureMax = v; emit(); },
            form.mixed([](Cam &c) { return c.autoExposureMax; })
        ).get();
        exposureMax->setHelp("The highest exposure the adaptation may reach");
        addComboRow(
            section, "Tonemap", {"AgX", "ACES", "Neutral", "Reinhard", "None"}, static_cast<int>(data->tonemap),
            [data, emit](int index) {
                data->tonemap = static_cast<Panda::Tonemap>(index);
                emit();
            },
            form.mixed([](Cam &c) { return static_cast<int>(c.tonemap); })
        )->setHelp("The curve that brings the light of the frame into the range of a display");
        // Built-in camera bloom (a pyramid in the engine; the glow source is the emissive of materials).
        addToggleRow(section, "Bloom", data->bloomEnabled, [data, changed](bool on) { data->bloomEnabled = on; changed(); },
                     form.mixed([](Cam &c) { return c.bloomEnabled; }))
            ->setHelp("The glow around what emits light (the emissive of materials)");
        PropertyRow *bloomIntensity = addNumberRow(
            section, "Intensity", 0.f, 100.f, data->bloomIntensity, [data, emit](float v) { data->bloomIntensity = v; emit(); },
            form.mixed([](Cam &c) { return c.bloomIntensity; })
        ).get();
        PropertyRow *bloomRadius = addNumberRow(
            section, "Radius", 0.f, 4.f, data->bloomRadius, [data, emit](float v) { data->bloomRadius = v; emit(); },
            form.mixed([](Cam &c) { return c.bloomRadius; })
        ).get();
        bloomRadius->setHelp("How far the glow spreads (0.5..2)");
        addToggleRow(section, "Antialiasing", data->antialiasing, [data, emit](bool on) { data->antialiasing = on; emit(); },
                     form.mixed([](Cam &c) { return c.antialiasing; }))
            ->setHelp("Smooths the edges of the finished frame (FXAA)");

        addAssetRow(section, "Postprocessing", "postfx", static_cast<uint32_t>(data->postprocessing.getId()),
                    [data, changed](uint32_t id) { data->postprocessing = assetRefOf(id); changed(); });
        PropertyRow *postfxEnabled = addToggleRow(
            section, "Postprocessing Enabled", data->postprocessingEnabled,
            [data, emit](bool on) { data->postprocessingEnabled = on; emit(); },
            form.mixed([](Cam &c) { return c.postprocessingEnabled; })
        ).get();
        postfxEnabled->setHelp("The passes of the postprocessing asset run; a script turns them on and off");

        for (PropertyRow *row : {exposureKey, exposureSpeed, exposureMin, exposureMax, bloomIntensity, bloomRadius}) {
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
            postfxEnabled->setVisible(static_cast<bool>(data->postprocessing.getId()));
        };
        (*refresh)();
    }

    void buildDirectionalLight(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using DL = Panda::DirectionalLightComponent;
        const ComponentForm<DL> form(context);
        const auto data = form.data;
        const auto emit = form.commit;

        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    form.mixed([](DL &c) { return c.color; }));
        addNumberRow(section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                     form.mixed([](DL &c) { return c.intensity; }))
            ->setHelp("What a white matte surface facing the sun sends back");
        addToggleRow(section, "Cast Shadows", data->castShadows, [data, emit](bool on) { data->castShadows = on; emit(); },
                     form.mixed([](DL &c) { return c.castShadows; }))
            ->setHelp("The first enabled sun of a world casts the cascaded shadows (World Settings → Shadows)");
        addToggleRow(section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                     form.mixed([](DL &c) { return c.isEnabled; }));
    }

    void buildPointLight(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using PL = Panda::PointLightComponent;
        const ComponentForm<PL> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        auto refresh = std::make_shared<std::function<void()>>();

        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    form.mixed([](PL &c) { return c.color; }));
        addNumberRow(section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                     form.mixed([](PL &c) { return c.intensity; }))
            ->setHelp("What a white matte surface 1 m away, facing the lamp, sends back");
        addNumberRow(section, "Radius", 0.f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                     form.mixed([](PL &c) { return c.radius; }))
            ->setHelp("Where the light ends, m: it falls with the square of the distance and reaches zero here");
        // A cone makes the lamp a spot that shines along the -Z of the entity; 0 — all around.
        addNumberRow(section, "Cone Angle", 0.f, 179.f, data->coneAngle,
                     [data, emit, refresh](float v) {
                         data->coneAngle = v;
                         emit();
                         if (*refresh) { (*refresh)(); }
                     },
                     form.mixed([](PL &c) { return c.coneAngle; }))
            ->setHelp("0 — the lamp shines all around. Above — a spot along the -Z of the entity; the full angle of its cone, degrees");
        PropertyRow *softness = addNumberRow(
            section, "Softness", 0.f, 1.f, data->coneSoftness, [data, emit](float v) { data->coneSoftness = v; emit(); },
            form.mixed([](PL &c) { return c.coneSoftness; })
        ).get();
        softness->setHelp("The share of the cone over which the light fades to its edge");
        softness->setIndent(FORM_INDENT);
        addToggleRow(section, "Cast Shadows", data->castShadows, [data, emit](bool on) { data->castShadows = on; emit(); },
                     form.mixed([](PL &c) { return c.castShadows; }))
            ->setHelp("A spot costs one more pass over the world every frame, a lamp that shines all around — six");
        addToggleRow(section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                     form.mixed([](PL &c) { return c.isEnabled; }));

        *refresh = [data, softness] { softness->setVisible(data->coneAngle > 0.f); };
        (*refresh)();
    }

    void buildSpriteRenderer(CollapsibleSection &section, const ComponentInspectorContext &context) {
        using SR = Panda::SpriteRendererComponent;
        const ComponentForm<SR> form(context);
        const auto data = form.data;
        const auto emit = form.commit;

        addColorRow(section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                    form.mixed([](SR &c) { return c.color; }), /*alpha*/ true);
        addAssetRow(section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()),
                    [data, emit](uint32_t id) { data->texture = assetRefOf(id); emit(); });
        // A sprite sheet: the texture is a grid of frames and the sprite shows one of them.
        addIntegerRow(section, "Columns", 1, 30, data->cols, [data, emit](int v) { data->cols = v; emit(); },
                      form.mixed([](SR &c) { return c.cols; }))
            ->setHelp("The frames of the sprite sheet in a row of the texture");
        addIntegerRow(section, "Rows", 1, 30, data->rows, [data, emit](int v) { data->rows = v; emit(); },
                      form.mixed([](SR &c) { return c.rows; }))
            ->setHelp("The rows of frames in the texture");
        addIntegerRow(section, "Index", 0, 900, data->index, [data, emit](int v) { data->index = v; emit(); },
                      form.mixed([](SR &c) { return c.index; }))
            ->setHelp("The frame shown, counted from 0 along the rows");
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
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
            form.mixed([](PM &c) { return static_cast<int>(c.kind); })
        );
        // An empty material = the built-in checker (the sky pattern: empty means the engine material).
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    }

    void buildImportedSubmesh(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::ImportedSubmeshComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addAssetRow(section, "Mesh", "mesh", static_cast<uint32_t>(data->importedMesh.getId()),
                    [data, emit](uint32_t id) { data->importedMesh = assetRefOf(id); emit(); });
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
        section.addContent(makeFormHint("Empty — the material the model was imported with."));
    }

    void buildSky(CollapsibleSection &section, const ComponentInspectorContext &context) {
        const ComponentForm<Panda::SkyComponent> form(context);
        const auto data = form.data;
        const auto emit = form.commit;
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                    [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
        section.addContent(
            makeFormHint("Empty — the default sky: the environment map of the world, or the built-in gradient.")
        );
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
    inspectors.add(inspectorOf<SkyComponent>("cubeMapComponent", "Sky", Ic::Globe, buildSky, false));
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

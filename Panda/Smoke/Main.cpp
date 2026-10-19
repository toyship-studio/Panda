// Engine smoke test. `--write-shader-includes <default-shaders/include>` — write
// the generated HLSL UBO blocks (ShaderInterface) and exit; otherwise — the tests: freshness
// of the generated includes in Resources, temporal stability of the occlusion volume (the CPU
// logic of the window) and cascaded sun shadows (CSM) end to end. A World with a plane,
// a cube above it and a sun at the side is rendered in the usual way (World::update → SCENE_VIEW,
// without physics and scripts) into an offscreen target; the cube shadow on the plane is checked
// by pixels along the middle row (the camera looks straight down, screen +x = world +x — the row
// order on readback differs between backends, the columns are the same). Without shadows the same
// points are equally bright.

#include <Fern/Fern.hpp>
#include <Foundation/Foundation.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Miren/Miren.hpp>
#include <Panda.hpp>
#include <Panda/Assets/Base/AssetHandler.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>
#include <Panda/Renderer/OcclusionVolume.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderInterface.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include <Panda/Packages/PackageLoader.hpp>
#if PANDA_SMOKE_HAS_PHYSICS
#include <Panda/Physics/PhysicsExtensions.hpp>
#endif
#if PANDA_SMOKE_HAS_TERRAIN
#include <Panda/Terrain/FloraDistances.hpp>
#include <Panda/Terrain/Terrain2DAsset.hpp>
#include <Panda/Terrain/TerrainComponents.hpp>
#include <Panda/Terrain/TerrainExtensions.hpp>
#endif

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace {

constexpr int TARGET_SIZE = 512;

// Perspective for the clip range of the backend: GL — [-1,1] (NO), Metal/D3D — [0,1] (ZO).
// The variant does not affect the screen geometry (w is the same, only z differs),
// but ZO does not lose the slab at the near plane to clipping and is twice as precise in depth.
glm::mat4 perspectiveForBackend(float fovyRadians, float aspect, float zNear, float zFar) {
    if (Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth) {
        return glm::perspective(fovyRadians, aspect, zNear, zFar);
    }
    return glm::perspectiveZO(fovyRadians, aspect, zNear, zFar);
}

// The test has no file assets — only the builtin objects of StaticResources and runtime assets
// (the shadow atlas): the resolve returns what has already been created.
class SmokeAssetHandler final : public Panda::AssetHandler {
public:
    SmokeAssetHandler()
        : Panda::AssetHandler(Foundation::getAllocator()) {}

protected:
    Panda::Asset *loadInternal(Panda::AssetId id, bool forcedReload = false) override {
        auto it = m_loadedAssets.find(id);
        return it != m_loadedAssets.end() ? it->second.asset : nullptr;
    }
};

// Brightness (R) of the pixel under a world point: projection through viewProj; the row order on
// readback follows the backend convention (GL — from the bottom, Metal/D3D — from the top).
float sampleWorld(const std::vector<uint8_t> &pixels, const glm::mat4 &viewProj, glm::vec3 worldPos) {
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const float v = ndc.y * 0.5f + 0.5f;
    // Bilinear sampling at a subpixel position (pixel centers are at +0.5): the shadow edge
    // is measured more precisely than the pixel size.
    const float fx = (ndc.x * 0.5f + 0.5f) * TARGET_SIZE - 0.5f;
    const float fy = (Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? v : 1.f - v) * TARGET_SIZE - 0.5f;
    const int x0 = glm::clamp(static_cast<int>(std::floor(fx)), 0, TARGET_SIZE - 2);
    const int y0 = glm::clamp(static_cast<int>(std::floor(fy)), 0, TARGET_SIZE - 2);
    const float tx = glm::clamp(fx - static_cast<float>(x0), 0.f, 1.f);
    const float ty = glm::clamp(fy - static_cast<float>(y0), 0.f, 1.f);
    const auto at = [&](int x, int y) { return static_cast<float>(pixels[(y * TARGET_SIZE + x) * 4]); };
    const float top = glm::mix(at(x0, y0), at(x0 + 1, y0), tx);
    const float bottom = glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx);
    return glm::mix(top, bottom, ty);
}

// World x coordinate of the shadow edge along the x axis (z = 0): the first crossing of the
// midpoint between dark and bright when scanning from x0 to x1 (linear interpolation between
// steps).
float shadowEdgeX(const std::vector<uint8_t> &pixels, const glm::mat4 &viewProj, float x0, float x1, float mid) {
    const float step = (x1 - x0) / 200.f;
    float previous = sampleWorld(pixels, viewProj, glm::vec3(x0, 0.f, 0.f));
    for (int i = 1; i <= 200; i++) {
        const float x = x0 + step * static_cast<float>(i);
        const float current = sampleWorld(pixels, viewProj, glm::vec3(x, 0.f, 0.f));
        if ((previous - mid) * (current - mid) <= 0.f && previous != current) {
            return x - step + step * (mid - previous) / (current - previous);
        }
        previous = current;
    }
    return x1;
}

// Average brightness (R) of a block of pixels of the middle row around the world point (x, 0, 0).
float sampleGround(const std::vector<uint8_t> &pixels, const glm::mat4 &viewProj, float worldX) {
    const glm::vec4 clip = viewProj * glm::vec4(worldX, 0.f, 0.f, 1.f);
    const float ndcX = clip.x / clip.w;
    const int column = static_cast<int>((ndcX * 0.5f + 0.5f) * TARGET_SIZE);
    float sum = 0.f;
    int count = 0;
    for (int y = TARGET_SIZE / 2 - 2; y <= TARGET_SIZE / 2 + 2; y++) {
        for (int x = column - 2; x <= column + 2; x++) {
            if (x < 0 || x >= TARGET_SIZE) { continue; }
            sum += pixels[(y * TARGET_SIZE + x) * 4];
            count++;
        }
    }
    return count > 0 ? sum / count : 0.f;
}

} // namespace

int main(int argc, const char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // output is visible even when redirected to a file
    if (argc == 3 && std::strcmp(argv[1], "--write-shader-includes") == 0) {
        std::string error;
        if (!Panda::ShaderInterface::writeIncludes(argv[2], error)) {
            std::printf("FAIL: %s\n", error.c_str());
            return 1;
        }
        std::printf("shader includes written to %s\n", argv[2]);
        return 0;
    }
    Fern::initialize();
    Fern::Window *window =
        Fern::createWindow("Panda Smoke: shadows", Fern::Rect(0, 0, 320, 320), Fern::WindowState::WindowStateNormal);
    Miren::initialize(window->getDrawingContext());
    auto *application = new Panda::Application(); // frame time for the material fields
    application->setMainWindow(window);

    auto *assetHandler = new SmokeAssetHandler();
    Panda::GameContext::setAssetHandler(assetHandler);
#if PANDA_SMOKE_HAS_PHYSICS
    Panda::GameContext::getPackageLoader().loadStatic(
        Panda::Physics::MODULE_ID, Panda::Physics::MODULE_VERSION, Panda::Physics::registerExtensions
    );
#endif
#if PANDA_SMOKE_HAS_TERRAIN
    Panda::GameContext::getPackageLoader().loadStatic(
        Panda::Terrain::MODULE_ID, Panda::Terrain::MODULE_VERSION, Panda::Terrain::registerExtensions
    );
#endif
    Panda::StaticResources::initStaticResources();

    // Offscreen scene target: color + emissive (the MRT of lit shaders) + depth, as in the runtime.
    Miren::TextureCreate colorCreate;
    colorCreate.m_format = Miren::TextureFormat::RGBA8;
    colorCreate.m_width = TARGET_SIZE;
    colorCreate.m_height = TARGET_SIZE;
    colorCreate.m_wrap = Miren::CLAMP;
    Miren::TextureHandle sceneColor = Miren::createTexture(colorCreate);
    Miren::TextureHandle sceneEmissive = Miren::createTexture(colorCreate);
    colorCreate.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    Miren::TextureHandle sceneDepth = Miren::createTexture(colorCreate);
    Miren::FrameBufferAttachment attachments[] = {sceneColor, sceneEmissive, sceneDepth};
    Miren::FrameBufferHandle sceneFrameBuffer =
        Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 3));
    Miren::setViewFrameBuffer(Panda::Views::SCENE_VIEW, sceneFrameBuffer);
    Miren::setViewport(Panda::Views::SCENE_VIEW, Miren::Rect(0, 0, TARGET_SIZE, TARGET_SIZE));
    Miren::setViewClear(Panda::Views::SCENE_VIEW, 0x000000ff);

    // World: a 20×20 plane (white lit material), a 2 m cube above it, the sun from +x at 45°.
    auto *world = new Panda::World();
    Panda::Entity cubeEntity;
    {
        Panda::Entity ground = world->instantiateEntity();
        ground.setName("Ground");
        ground.getTransform().setScale({20.f, 1.f, 20.f});
        auto &groundMesh = ground.addComponent<Panda::RuntimeMeshComponent>();
        groundMesh.runtimeMesh = Panda::StaticResources::planeMesh.asBaseAsset();
        groundMesh.material = Panda::StaticResources::defaultLitMaterial.asBaseAsset();

        Panda::Entity cube = world->instantiateEntity();
        cubeEntity = cube;
        cube.setName("Cube");
        cube.getTransform().setPosition({0.f, 1.5f, 0.f});
        cube.getTransform().setScale({2.f, 2.f, 2.f});
        auto &cubeMesh = cube.addComponent<Panda::RuntimeMeshComponent>();
        cubeMesh.runtimeMesh = Panda::StaticResources::cubeMesh.asBaseAsset();
        cubeMesh.material = Panda::StaticResources::defaultLitMaterial.asBaseAsset();

        // Light: the component's forward = world -Z after the rotation; the direction TO the
        // light = -forward.
        Panda::Entity sun = world->instantiateEntity();
        sun.setName("Sun");
        const glm::vec3 toLight = glm::normalize(glm::vec3(1.f, 1.f, 0.f));
        sun.getTransform().setRotation(glm::quatLookAt(-toLight, glm::vec3(0.f, 1.f, 0.f)));
        auto &light = sun.addComponent<Panda::DirectionalLightComponent>();
        light.intensity = 1.f;
    }
    world->getLightingSettings().ambientIntensity = 0.1f;

    // The camera looks straight down from a height of 12 m: screen +x = world +x, the middle
    // row = z = 0.
    glm::vec3 cameraPosition(0.f, 12.f, 0.f);
    const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.f), glm::vec3(0.f, 0.f, -1.f));
    const glm::mat4 projection = perspectiveForBackend(glm::radians(60.f), 1.f, 0.1f, 100.f);
    glm::mat4 viewProj = projection * view;
    glm::mat4 skyViewProj = viewProj;

    std::vector<uint8_t> pixels(TARGET_SIZE * TARGET_SIZE * 4);
    auto renderAndRead = [&]() {
        for (int frame = 0; frame < 3; frame++) {
            world->update(1.0 / 60.0, {.viewProj = viewProj, .skyViewProj = skyViewProj, .cameraPosition = cameraPosition});
            Fern::pollEvents();
            Miren::frame();
        }
        std::fill(pixels.begin(), pixels.end(), 0);
        const uint32_t ready =
            Miren::readFrameBuffer(sceneFrameBuffer, 0, 0, 0, TARGET_SIZE, TARGET_SIZE, pixels.data());
        while (Miren::getFrameNumber() <= ready) {
            world->update(1.0 / 60.0, {.viewProj = viewProj, .skyViewProj = skyViewProj, .cameraPosition = cameraPosition});
            Fern::pollEvents();
            Miren::frame();
        }
    };

    int failures = 0;
    // The generated includes of the UBO blocks in Resources must match the C++ structures.
    std::printf("shader includes\n");
    {
        const std::string stale = Panda::ShaderInterface::checkIncludes(Panda::AssetImporterBase::getShaderIncludeRoot());
        std::printf("  [%s] generated includes match ShaderBlocks.hpp%s%s\n", stale.empty() ? " OK " : "FAIL",
                    stale.empty() ? "" : ": ", stale.c_str());
        if (!stale.empty()) { failures++; }
    }
    const auto report = [&](const char *name, bool ok) {
        std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", name);
        if (!ok) { failures++; }
    };

    // Occlusion volume, temporal stability (context/lighting.md, "Temporal stability") — the CPU logic of the
    // window: chunks fade in with a ramp and do not go out while they lie within the falloff
    // radius from the focus; a rebake for the sun is a single channel transition with a smooth
    // cross-fade, and it does not touch the chunk weights.
    std::printf("occlusion volume stability\n");
    {
        Panda::OcclusionVolume volume;
        Panda::OcclusionVolume::Settings settings;
        settings.cellSize = 2.f;
        settings.windowChunks = 4;
        // A lattice of crowns 20 × 20 m over the whole field.
        const auto collect =
            [](const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Panda::Occluder> &out) {
                for (float x = std::floor(minWorld.x / 20.f) * 20.f; x <= maxWorld.x; x += 20.f) {
                    for (float z = std::floor(minWorld.z / 20.f) * 20.f; z <= maxWorld.z; z += 20.f) {
                        out.push_back({glm::vec3(x, 8.f, z), 4.f, 3.f, 0.8f});
                    }
                }
            };
        const auto floorMod = [](int value, int modulus) {
            const int result = value % modulus;
            return result < 0 ? result + modulus : result;
        };
        constexpr double DT = 1.0 / 60.0;
        const float maxStep = static_cast<float>(DT) / Panda::OcclusionVolume::FADE_SECONDS + 1.e-4f;
        glm::vec3 sun = glm::normalize(glm::vec3(1.f, 1.f, 0.f));
        glm::vec3 focus(0.f);
        std::array<float, Panda::OCCLUSION_MAX_CHUNKS> previousWeight{};
        std::array<glm::ivec2, Panda::OCCLUSION_MAX_CHUNKS> previousChunk{};
        float worstWeightStep = 0.f;
        bool weightDropped = false; // the weight dropped for a slot whose chunk did not change
        // The volume ramps run on the application clock: the frame is advanced by Application::tick
        // (it also polls the events and submits the Miren frame).
        const auto pump = [&]() {
            application->tick(DT);
            volume.update(settings, focus, sun, 1, collect);
            const Panda::OcclusionVolumeData &data = volume.data();
            for (size_t i = 0; i < data.chunks.size(); i++) {
                const float weight = data.chunks[i].weight;
                if (weight < previousWeight[i] - 1.e-6f && data.chunks[i].chunk == previousChunk[i]) {
                    weightDropped = true;
                }
                worstWeightStep = std::max(worstWeightStep, weight - previousWeight[i]);
                previousWeight[i] = weight;
                previousChunk[i] = data.chunks[i].chunk;
            }
            // A smoke test frame does not wait for vsync — the pause gives the background bakes
            // time (a real frame is ≥ 8 ms).
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        const auto settle = [&](int maxFrames) {
            for (int frame = 0; frame < maxFrames && !volume.ready(); frame++) { pump(); }
            return volume.ready();
        };
        report("window fills", settle(3000));
        for (int frame = 0; frame < 40; frame++) { pump(); } // the ramps reach 1
        report("chunk weights ramp continuously", worstWeightStep <= maxStep && !weightDropped);

        // Focus motion across chunk boundaries: in the settled state everything that lies before
        // the start of the falloff (fadeRadius − 16 cells) is resident with full weight — the
        // guarantee of the window placement plus a margin for the fade-in ramp of a chunk that
        // entered at the edge.
        const float chunkWorld = static_cast<float>(Panda::OcclusionVolume::CHUNK_CELLS) * settings.cellSize;
        bool residentInside = true;
        int settledFrames = 0;
        // 0.2 m per frame: the falloff band (16 cells) is 160 frames, a chunk at the edge has time
        // to bake and fade in (≈ 40 frames) before it enters the zone of full occlusion.
        for (int i = 0; i < 2000; i++) {
            focus.x += 0.2f;
            focus.z += 0.076f;
            pump();
            if (!volume.ready()) { continue; }
            settledFrames++;
            const Panda::OcclusionVolumeData &data = volume.data();
            const int window = static_cast<int>(data.windowChunks);
            const glm::vec2 focusXZ(focus.x, focus.z);
            const glm::ivec2 centerChunk(
                static_cast<int>(std::floor(focus.x / chunkWorld)), static_cast<int>(std::floor(focus.z / chunkWorld))
            );
            for (int cz = -3; cz <= 3; cz++) {
                for (int cx = -3; cx <= 3; cx++) {
                    const glm::ivec2 chunk = centerChunk + glm::ivec2(cx, cz);
                    const glm::vec2 boxMin = glm::vec2(chunk) * chunkWorld;
                    const glm::vec2 nearest = glm::clamp(focusXZ, boxMin, boxMin + chunkWorld);
                    if (glm::distance(nearest, focusXZ) >= data.fadeRadius - 16.f * data.cellSize) { continue; }
                    const Panda::OcclusionChunk &entry =
                        data.chunks[floorMod(chunk.y, window) * window + floorMod(chunk.x, window)];
                    if (entry.chunk != chunk || entry.weight < 0.999f) { residentInside = false; }
                }
            }
        }
        report("focus motion: chunks within fade radius stay resident", residentInside && settledFrames > 100);
        report("focus motion: no weight drops without chunk change", !weightDropped && worstWeightStep <= maxStep);

        // Sun rotation by 30° (> the 15° step): a sun pass — the weights do not change, the
        // cross-fade weight moves continuously and reaches the other channel.
        settle(3000);
        for (int frame = 0; frame < 40; frame++) { pump(); }
        const float blendBefore = volume.data().transmittanceBlend;
        const float blendTarget = blendBefore > 0.5f ? 0.f : 1.f;
        sun = glm::normalize(glm::vec3(std::cos(glm::radians(30.f)), 1.f, std::sin(glm::radians(30.f))));
        float previousBlend = blendBefore;
        float worstBlendStep = 0.f;
        bool blendReversed = false;
        bool weightsHeld = true;
        int frames = 0;
        for (; frames < 3000; frames++) {
            pump();
            const Panda::OcclusionVolumeData &data = volume.data();
            const float blend = data.transmittanceBlend;
            const float delta = (blend - previousBlend) * (blendTarget - blendBefore);
            if (delta < -1.e-6f) { blendReversed = true; }
            worstBlendStep = std::max(worstBlendStep, std::abs(blend - previousBlend));
            previousBlend = blend;
            for (uint32_t i = 0; i < data.windowChunks * data.windowChunks; i++) {
                if (data.chunks[i].weight < 0.999f) { weightsHeld = false; }
            }
            if (volume.ready() && std::abs(blend - blendTarget) < 1.e-6f) { break; }
        }
        report("sun pass: blend reaches the other channel", frames < 3000);
        report("sun pass: blend ramps continuously", worstBlendStep <= maxStep && !blendReversed);
        report("sun pass: chunk weights untouched", weightsHeld);
    }

    // World ↔ JSON through the mapper; the entities of the file by id — a compact JSON of each one
    // (rapidjson, the same one that Rain uses) for comparing the round trip independently of the
    // registry traversal order.
    const auto saveWorldJson = [](const Panda::World &world) {
        std::stringstream stream;
        Rain::JsonEncoder json(true);
        Panda::WorldMapper::save(world, json, stream);
        return stream.str();
    };
    const auto loadWorldJson = [](Panda::World &world, const std::string &text) {
        std::stringstream stream(text);
        Rain::JsonDecoder json;
        return Panda::WorldMapper::load(world, json, stream);
    };
    const auto compactJson = [](const rapidjson::Value &value) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        value.Accept(writer);
        return std::string(buffer.GetString());
    };
    const auto entitiesJson = [&](const std::string &text) {
        std::map<uint32_t, std::string> result;
        rapidjson::Document document;
        document.Parse(text.c_str());
        for (const rapidjson::Value &entity : document["entities"].GetArray()) {
            result[entity["id"].GetUint()] = compactJson(entity);
        }
        return result;
    };
    const auto memberJson = [&](const std::string &text, const char *key) {
        rapidjson::Document document;
        document.Parse(text.c_str());
        return compactJson(document[key]);
    };

    // Static tag (an empty entt component): an entity duplicate, a world copy (Play) and .world
    // carry it over; removing it takes it away.
    std::printf("static tag\n");
    {
        Panda::Entity tagged = world->instantiateEntity();
        tagged.setName("Static");
        tagged.addTag<Panda::IsStaticComponent>();
        report("tag added", tagged.hasComponent<Panda::IsStaticComponent>());
        Panda::Entity clone = world->duplicateEntity(tagged);
        report("duplicate keeps the tag", clone.hasComponent<Panda::IsStaticComponent>());
        auto *copy = new Panda::World();
        *copy = *world;
        report("world copy keeps the tag", copy->getById(tagged.getId()).hasComponent<Panda::IsStaticComponent>());
        delete copy;
        // In the file the tag is a bool on the entity (in old worlds it is false for each one).
        const std::string json = saveWorldJson(*world);
        const std::map<uint32_t, std::string> entities = entitiesJson(json);
        const auto isStaticIn = [&](uint32_t id) {
            const auto it = entities.find(id);
            return it != entities.end() && it->second.find("\"isStatic\":true") != std::string::npos;
        };
        report("world file carries isStatic", isStaticIn(tagged.getId()) && !isStaticIn(cubeEntity.getId()));
        auto *restored = new Panda::World();
        report(
            "world restored from file keeps the tag",
            loadWorldJson(*restored, json) &&
                restored->getById(tagged.getId()).hasComponent<Panda::IsStaticComponent>() &&
                !restored->getById(cubeEntity.getId()).hasComponent<Panda::IsStaticComponent>()
        );
        delete restored;
        tagged.removeComponent<Panda::IsStaticComponent>();
        report("tag removed", !tagged.hasComponent<Panda::IsStaticComponent>());
        world->destroy(clone);
        world->destroy(tagged);
    }

    // Round trip of the world file: world → JSON → world → JSON. Catches a forgotten line in the
    // contribution of a component type (a field is written but not read, or vice versa) on the
    // whole world at once; the comparison is by entities (the registry traversal order of the
    // restored world is different). A component without a contribution (the plugin is not loaded)
    // survives the load and the save as is.
    std::printf("world file round trip\n");
    {
        const std::string first = saveWorldJson(*world);
        auto *restored = new Panda::World();
        report("world loads from json", loadWorldJson(*restored, first));
        const std::string second = saveWorldJson(*restored);
        delete restored;
        const std::map<uint32_t, std::string> firstEntities = entitiesJson(first);
        report("entities survive the round trip", firstEntities == entitiesJson(second) && !firstEntities.empty());
        report(
            "settings survive the round trip",
            memberJson(first, "lighting") == memberJson(second, "lighting") &&
                memberJson(first, "graphics") == memberJson(second, "graphics") &&
                memberJson(first, "wind") == memberJson(second, "wind")
        );
        // Foreign component: plant an unknown key into the first entity of the file.
        rapidjson::Document document;
        document.Parse(first.c_str());
        rapidjson::Value &entity = document["entities"][0];
        const uint32_t foreignId = entity["id"].GetUint();
        rapidjson::Value foreign(rapidjson::kObjectType);
        foreign.AddMember("fuel", 3, document.GetAllocator());
        foreign.AddMember("name", "rocket", document.GetAllocator());
        entity.AddMember("org.example/rocketComponent", foreign, document.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        auto *foreignWorld = new Panda::World();
        const bool foreignLoaded = loadWorldJson(*foreignWorld, buffer.GetString());
        const bool foreignKept = foreignLoaded && foreignWorld->getById(foreignId).hasComponent<Panda::ForeignComponents>();
        const std::string resaved = saveWorldJson(*foreignWorld);
        delete foreignWorld;
        const std::map<uint32_t, std::string> resavedEntities = entitiesJson(resaved);
        const auto kept = resavedEntities.find(foreignId);
        report(
            "foreign component survives load and save",
            foreignKept && kept != resavedEntities.end() &&
                kept->second.find("\"org.example/rocketComponent\":{\"fuel\":3,\"name\":\"rocket\"}") != std::string::npos
        );
#if PANDA_SMOKE_HAS_TERRAIN
        // Contribution settings (the terrain flora distances) — the top-level "flora" key: the
        // value of the world survives the round trip; a foreign top-level key (the plugin is not
        // loaded) — as is.
        Panda::FloraDistances &flora = world->context<Panda::FloraDistances>();
        const float savedGrass = flora.grass;
        flora.grass = 123.f;
        const std::string floraJson = saveWorldJson(*world);
        flora.grass = savedGrass;
        auto *floraWorld = new Panda::World();
        const bool floraLoaded = loadWorldJson(*floraWorld, floraJson);
        const std::string floraResaved = floraLoaded ? saveWorldJson(*floraWorld) : std::string("{}");
        report(
            "plugin world settings survive the round trip",
            floraLoaded && floraWorld->context<Panda::FloraDistances>().grass == 123.f &&
                memberJson(floraJson, "flora").find("\"grass\":123") != std::string::npos &&
                memberJson(floraJson, "flora") == memberJson(floraResaved, "flora") &&
                memberJson(floraJson, "terrain") == memberJson(floraResaved, "terrain")
        );
        delete floraWorld;
#endif
        rapidjson::Document settingsDocument;
        settingsDocument.Parse(first.c_str());
        rapidjson::Value weather(rapidjson::kObjectType);
        weather.AddMember("rain", 0.5, settingsDocument.GetAllocator());
        settingsDocument.AddMember("org.example/weather", weather, settingsDocument.GetAllocator());
        rapidjson::StringBuffer settingsBuffer;
        rapidjson::Writer<rapidjson::StringBuffer> settingsWriter(settingsBuffer);
        settingsDocument.Accept(settingsWriter);
        auto *settingsWorld = new Panda::World();
        const bool settingsLoaded = loadWorldJson(*settingsWorld, settingsBuffer.GetString());
        const std::string settingsResaved = settingsLoaded ? saveWorldJson(*settingsWorld) : std::string("{}");
        delete settingsWorld;
        report(
            "foreign world settings survive load and save",
            settingsLoaded && settingsResaved.find("org.example/weather") != std::string::npos &&
                memberJson(settingsResaved, "org.example/weather") == "{\"rain\":0.5}"
        );
        // A section missing from the file loads as the defaults (the registry resets every
        // section before reading), and clear() brings the settings back to the defaults and drops
        // the foreign sections: a new world does not inherit the previous one.
        rapidjson::Document withoutWind;
        withoutWind.Parse(first.c_str());
        withoutWind.RemoveMember("wind");
        rapidjson::StringBuffer withoutWindBuffer;
        rapidjson::Writer<rapidjson::StringBuffer> withoutWindWriter(withoutWindBuffer);
        withoutWind.Accept(withoutWindWriter);
        auto *defaultsWorld = new Panda::World();
        defaultsWorld->getWindSettings().strength = 0.9f;
        const bool defaultsLoaded = loadWorldJson(*defaultsWorld, withoutWindBuffer.GetString());
        report(
            "a missing settings section loads as the defaults",
            defaultsLoaded && defaultsWorld->getWindSettings().strength == Panda::SceneWindSettings().strength
        );
        const bool foreignBeforeClear = loadWorldJson(*defaultsWorld, settingsBuffer.GetString()) &&
                                        defaultsWorld->findContext<Panda::ForeignSettings>() != nullptr;
        defaultsWorld->getLightingSettings().ambientIntensity = 0.5f;
        defaultsWorld->clear();
        report(
            "clear resets the settings and drops foreign sections",
            foreignBeforeClear && defaultsWorld->findContext<Panda::ForeignSettings>() == nullptr &&
                defaultsWorld->getLightingSettings().ambientIntensity == Panda::WorldLightingSettings().ambientIntensity
        );
        delete defaultsWorld;
    }

    // The shadow of the cube (x ∈ [-1..1], y ∈ [0.5..2.5]) from the sun at +x at 45° lies at
    // x ∈ [-3.5..-1.5].
    const auto check = [&](const char *name, bool ok, float shadowed, float lit, float far) {
        std::printf("  [%s] %-34s shadow=%6.1f lit=%6.1f far=%6.1f\n", ok ? " OK " : "FAIL", name, shadowed, lit, far);
        if (!ok) { failures++; }
    };

    std::printf("shadows enabled\n");
    renderAndRead();
    float shadowed = sampleGround(pixels, viewProj, -2.5f);
    float lit = sampleGround(pixels, viewProj, 4.f);
    float far = sampleGround(pixels, viewProj, -5.5f);
    check("ground under cube shadow is dark", shadowed < lit * 0.6f && lit > 40.f, shadowed, lit, far);
    check("ground past the shadow is lit", far > lit * 0.85f, shadowed, lit, far);

    std::printf("shadows disabled\n");
    world->getGraphicsSettings().shadows.enabled = false;
    renderAndRead();
    shadowed = sampleGround(pixels, viewProj, -2.5f);
    lit = sampleGround(pixels, viewProj, 4.f);
    far = sampleGround(pixels, viewProj, -5.5f);
    check("no shadow without shadows", shadowed > lit * 0.85f && lit > 40.f, shadowed, lit, far);

    std::printf("shadows enabled, 1 cascade of 512\n");
    world->getGraphicsSettings().shadows.enabled = true;
    world->getGraphicsSettings().shadows.cascadeCount = 1;
    world->getGraphicsSettings().shadows.tileSize = 512;
    renderAndRead();
    shadowed = sampleGround(pixels, viewProj, -2.5f);
    lit = sampleGround(pixels, viewProj, 4.f);
    far = sampleGround(pixels, viewProj, -5.5f);
    check("single cascade shadows the ground", shadowed < lit * 0.6f && lit > 40.f, shadowed, lit, far);

    // The caster has left all cascades (the depth passes are empty): the atlas must be cleared —
    // otherwise a ghost shadow remains from the previous frames.
    std::printf("caster moved away (atlas must clear)\n");
    world->getGraphicsSettings().shadows.cascadeCount = 3;
    world->getGraphicsSettings().shadows.tileSize = 1024;
    cubeEntity.getTransform().setPosition({400.f, 1.5f, 0.f});
    renderAndRead();
    shadowed = sampleGround(pixels, viewProj, -2.5f);
    lit = sampleGround(pixels, viewProj, 4.f);
    far = sampleGround(pixels, viewProj, -5.5f);
    check("no ghost shadow without casters", shadowed > lit * 0.85f && lit > 40.f, shadowed, lit, far);
    cubeEntity.getTransform().setPosition({0.f, 1.5f, 0.f});

    // Ortho camera (ZO: on Metal/D3D the NO ortho cuts off the near half of the volume): the
    // cascade slices are built from the 8 corners of the frustum — the shadow is in the same
    // places.
    std::printf("orthographic camera\n");
    const glm::mat4 orthoViewProj = glm::orthoZO(-7.f, 7.f, -7.f, 7.f, 0.1f, 100.f) * view;
    viewProj = orthoViewProj;
    skyViewProj = orthoViewProj;
    renderAndRead();
    shadowed = sampleGround(pixels, viewProj, -2.5f);
    lit = sampleGround(pixels, viewProj, 4.f);
    far = sampleGround(pixels, viewProj, -5.5f);
    check("ortho camera: shadow under cube", shadowed < lit * 0.6f && lit > 40.f, shadowed, lit, far);
    check("ortho camera: lit past the shadow", far > lit * 0.85f, shadowed, lit, far);

    // Stability under camera motion: the snap to a texel keeps the shadow map grid fixed in the
    // world — the shadow edge (x ≈ -3.5, the top of the cube) must not drift. A coarse tile (256)
    // makes a grid mismatch noticeable (~0.1 m per texel), the camera is low (a pixel is ~1.5 cm).
    // First a rotation of the view direction (the cascade slices travel after the camera).
    std::printf("camera look-around (shadow edge stability)\n");
    world->getGraphicsSettings().shadows.tileSize = 256;
    const glm::mat4 lowProjection = perspectiveForBackend(glm::radians(60.f), 1.f, 0.1f, 100.f);
    const glm::vec3 lookCamera(2.f, 8.f, 6.f);
    cameraPosition = lookCamera;
    float edgeMin = 1e9f;
    float edgeMax = -1e9f;
    const glm::vec3 targets[] = {
        {-5.f, 0.f, -2.f}, {-3.f, 0.f, 0.f}, {-1.f, 0.f, 2.f}, {-3.f, 0.f, 3.f}, {-6.f, 0.f, 0.f}, {-3.f, 1.f, -1.f}
    };
    for (const glm::vec3 &target : targets) {
        viewProj = lowProjection * glm::lookAt(lookCamera, target, glm::vec3(0.f, 1.f, 0.f));
        skyViewProj = viewProj;
        renderAndRead();
        const float dark = sampleWorld(pixels, viewProj, glm::vec3(-2.5f, 0.f, 0.f));
        const float bright = sampleWorld(pixels, viewProj, glm::vec3(-4.5f, 0.f, 0.f));
        const float edge = shadowEdgeX(pixels, viewProj, -4.4f, -2.6f, (dark + bright) * 0.5f);
        std::printf(
            "  target (%.0f %.0f %.0f): edge x = %.3f (dark %.0f, lit %.0f)\n",
            target.x,
            target.y,
            target.z,
            edge,
            dark,
            bright
        );
        edgeMin = std::min(edgeMin, edge);
        edgeMax = std::max(edgeMax, edge);
    }
    check("shadow edge stable under look-around", edgeMax - edgeMin < 0.02f, edgeMin, edgeMax, edgeMax - edgeMin);

#if PANDA_SMOKE_HAS_TERRAIN
    // The receiver is the TERRAIN (LOD grid, terrain.frag), the camera moves EVERY FRAME without
    // pauses (like a flight in the editor): the edge of the cube shadow on the ground must stand
    // still. The plane is removed. Only with the Terrain module (the build without modules skips it).
    std::printf("terrain receiver, continuous camera motion (shadow edge stability)\n");
    world->getGraphicsSettings().shadows.tileSize = 512;
    Panda::Entity groundEntity = world->findByTag("Ground");
    if (groundEntity.isValid()) { world->destroy(groundEntity); }
    {
        // 96×96 m, a 1 m cell, gentle hills ±0.4 m; the center is at the world origin.
        Panda::TerrainData2D data = Panda::TerrainData2D::makeFlat(96, 96, 1.f);
        for (uint32_t z = 0; z <= 96; z++) {
            for (uint32_t x = 0; x <= 96; x++) {
                const float height =
                    0.4f * std::sin(static_cast<float>(x) * 0.31f) * std::cos(static_cast<float>(z) * 0.23f);
                data.setHeightAt(x, z, height);
            }
        }
        Panda::AssetRef<Panda::Terrain2DAsset> terrain =
            Panda::GameContext::getAssetHandler()->createRuntimeAsset<Panda::Terrain2DAsset>(data);
        Panda::Entity terrainEntity = world->instantiateEntity();
        terrainEntity.setName("Terrain");
        terrainEntity.getTransform().setPosition({-48.f, 0.f, -48.f});
        auto &terrainComponent = terrainEntity.addComponent<Panda::Terrain2DComponent>();
        terrainComponent.terrain = terrain.asBaseAsset();
        terrainComponent.physicsEnabled = false;
    }
    cubeEntity.getTransform().setPosition({0.f, 1.5f, 0.f});
    // The ground height on the measurement line is known analytically (x + 48, z + 48 in local
    // space).
    const auto groundHeight = [](float worldX) {
        return 0.4f * std::sin((worldX + 48.f) * 0.31f) * std::cos(48.f * 0.23f);
    };
    // The edge along x at y = h(x): projection of the points with the real surface height. No
    // crossing — no edge: the terrain is not on screen yet, or there is no shadow on it.
    const auto terrainEdgeX = [&](const std::vector<uint8_t> &frame, const glm::mat4 &vp) -> std::optional<float> {
        const auto brightnessAt = [&](float x) { return sampleWorld(frame, vp, glm::vec3(x, groundHeight(x), 0.f)); };
        const float dark = brightnessAt(-2.5f);
        const float bright = brightnessAt(-4.5f);
        const float mid = (dark + bright) * 0.5f;
        const float x0 = -4.4f;
        const float step = (-2.6f - x0) / 200.f;
        float previous = brightnessAt(x0);
        for (int i = 1; i <= 200; i++) {
            const float x = x0 + step * static_cast<float>(i);
            const float current = brightnessAt(x);
            if ((previous - mid) * (current - mid) <= 0.f && previous != current) {
                return x - step + step * (mid - previous) / (current - previous);
            }
            previous = current;
        }
        return std::nullopt;
    };
    // The LOD grid builds in a background wave plus a per-frame budget: wait until the receiver is
    // on screen (the shadow edge is found on the terrain), with a cap. A slow machine (the CI
    // runner) needs more than any fixed number of frames.
    cameraPosition = lookCamera;
    viewProj = lowProjection * glm::lookAt(lookCamera, glm::vec3(-3.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    skyViewProj = viewProj;
    int warmupRounds = 0;
    bool terrainOnScreen = false;
    while (!terrainOnScreen && warmupRounds < 200) {
        renderAndRead();
        warmupRounds++;
        terrainOnScreen = terrainEdgeX(pixels, viewProj).has_value();
    }
    check("terrain receiver built and on screen", terrainOnScreen, static_cast<float>(warmupRounds), 0.f, 0.f);
    constexpr int MOTION_FRAMES = 12;
    std::vector<std::vector<uint8_t>> motionFrames(MOTION_FRAMES, std::vector<uint8_t>(TARGET_SIZE * TARGET_SIZE * 4));
    std::vector<glm::mat4> motionViewProj(MOTION_FRAMES);
    uint32_t lastReady = 0;
    // A projection like that of the editor camera: near 0.1, far 20000, a wide aspect. The inverse
    // matrix of such a perspective is ill-conditioned in float: the far corners float by percents —
    // if the cascades are built from them, the sphere radius breathes and the shadow edges jitter
    // in motion (the 1 cm threshold catches it: with the error it was ~1.5 cm on the cube near the
    // cascade center, at the cascade edge — up to 2 texels).
    const glm::mat4 editorLikeProjection = perspectiveForBackend(glm::radians(60.f), 1.78f, 0.1f, 20000.f);
    for (int frame = 0; frame < MOTION_FRAMES; frame++) {
        // ~4 cm per frame diagonally — centimeter fractions of a texel every frame.
        const glm::vec3 movedCamera = lookCamera + glm::vec3(0.037f, 0.011f, -0.023f) * static_cast<float>(frame);
        cameraPosition = movedCamera;
        viewProj = editorLikeProjection * glm::lookAt(movedCamera, glm::vec3(-3.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
        skyViewProj = viewProj;
        motionViewProj[frame] = viewProj;
        world->update(1.0 / 60.0, {.viewProj = viewProj, .skyViewProj = skyViewProj, .cameraPosition = cameraPosition});
        lastReady =
            Miren::readFrameBuffer(sceneFrameBuffer, 0, 0, 0, TARGET_SIZE, TARGET_SIZE, motionFrames[frame].data());
        Fern::pollEvents();
        Miren::frame();
    }
    while (Miren::getFrameNumber() <= lastReady) {
        world->update(1.0 / 60.0, {.viewProj = viewProj, .skyViewProj = skyViewProj, .cameraPosition = cameraPosition});
        Fern::pollEvents();
        Miren::frame();
    }
    edgeMin = 1e9f;
    edgeMax = -1e9f;
    bool edgeInEveryFrame = true;
    for (int frame = 0; frame < MOTION_FRAMES; frame++) {
        const std::optional<float> edge = terrainEdgeX(motionFrames[frame], motionViewProj[frame]);
        if (!edge) {
            std::printf("  frame %2d: no shadow edge on the terrain\n", frame);
            edgeInEveryFrame = false;
            continue;
        }
        std::printf("  frame %2d: edge x = %.3f\n", frame, *edge);
        edgeMin = std::min(edgeMin, *edge);
        edgeMax = std::max(edgeMax, *edge);
    }
    check(
        "terrain: edge stable under continuous motion",
        edgeInEveryFrame && edgeMax - edgeMin < 0.01f,
        edgeMin,
        edgeMax,
        edgeMax - edgeMin
    );
    world->getGraphicsSettings().shadows.tileSize = 1024;
#endif

    for (int frame = 0; frame < 2; frame++) {
        Fern::pollEvents();
        Miren::frame();
    }
    delete world;
    Miren::deleteFrameBuffer(sceneFrameBuffer);
    Miren::deleteTexture(sceneColor);
    Miren::deleteTexture(sceneEmissive);
    Miren::deleteTexture(sceneDepth);
    Panda::StaticResources::deinitStaticResources();
    assetHandler->cleanup();
    Panda::GameContext::setAssetHandler(nullptr);
    delete assetHandler;
    Fern::pollEvents();
    Miren::frame();
    delete application;
    Miren::terminate();
    Fern::disposeWindow(window);
    Fern::terminate();

    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

// Engine smoke test. `--write-shader-includes <default-shaders/include>` — write
// the generated HLSL UBO blocks (ShaderInterface) and exit; otherwise — the tests: freshness
// of the generated includes in Resources, temporal stability of the occlusion volume (the CPU
// logic of the window) and cascaded sun shadows (CSM) end to end. A World with a plane,
// a cube above it and a sun at the side is rendered in the usual way (WorldView::render, without
// physics and scripts); the HDR frame of the view is read back, and the cube shadow on the plane
// is checked by pixels along the middle row (the camera looks straight down, screen +x = world +x
// — the row order on readback differs between backends, the columns are the same). Without
// shadows the same points are equally bright.

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
#include <Panda/Assets/Base/CookedMesh.hpp>
#include <Panda/Renderer/OcclusionVolume.hpp>
#include <Panda/Runtime/AssetPack.hpp>
#include <Panda/Runtime/GameFiles.hpp>
#include <Panda/Renderer/WorldView.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderInterface.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/Serialization/ComponentDtos.hpp>
#include <Panda/Serialization/ReferenceCollector.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Panda/Prefabs/PrefabAsset.hpp>
#include <Panda/Prefabs/PrefabComponent.hpp>
#include <Panda/Prefabs/Prefabs.hpp>
#include <Rain/Coders/BinaryDecoder.hpp>
#include <Rain/Coders/BinaryEncoder.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include <Panda/Packages/PackageLoader.hpp>
#if PANDA_SMOKE_HAS_PHYSICS
#include <Panda/Physics/Physics3D.hpp>
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

#include <fstream>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/packing.hpp>

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

    // The prefabs of the checks: the text of the file of each, by the id of its asset.
    std::unordered_map<Panda::AssetId, std::string> prefabs;

    bool hasAsset(Panda::AssetId id) const override {
        return m_loadedAssets.contains(id) || prefabs.contains(id);
    }
    void reload(Panda::AssetId id) {
        loadInternal(id, true);
    }

protected:
    Panda::Asset *loadInternal(Panda::AssetId id, bool forcedReload = false) override {
        auto it = m_loadedAssets.find(id);
        if (it != m_loadedAssets.end() && !forcedReload) { return it->second.asset; }
#ifdef PND_EDITOR
        const auto prefab = prefabs.find(id);
        if (prefab != prefabs.end()) {
            if (it != m_loadedAssets.end()) {
                F_DELETE(m_allocator, it->second.asset);
                m_loadedAssets.erase(it);
                bumpResolveGeneration();
            }
            auto *asset = F_NEW(m_allocator, Panda::PrefabAsset)();
            m_loadedAssets[id] = {asset};
            Panda::Prefabs::loadTemplate(id, prefab->second, *asset);
            return asset;
        }
#endif
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

    // The world is not simulated here: a frame of it is drawn through a view of its own, into
    // the targets of the view — the same HDR frame the game has.
    auto *worldView = new Panda::WorldView();
    worldView->resize(TARGET_SIZE, TARGET_SIZE);
    const auto drawWorld = [&] {
        Panda::ViewCamera camera;
        camera.viewProj = viewProj;
        camera.skyViewProj = skyViewProj;
        camera.position = cameraPosition;
        worldView->render(*world, camera);
    };
    // The readback of the frame that was just drawn: the scene color as it is (RGBA16F, linear
    // light). The checks compare the brightness a display without a curve would show — 8 bits,
    // clamped at 1.0.
    using RawFrame = std::vector<uint16_t>;
    const auto readScene = [&](RawFrame &raw) {
        raw.assign(TARGET_SIZE * TARGET_SIZE * 4, 0);
        return Miren::readTexture(worldView->postprocessingInputs(*world).color, raw.data());
    };
    const auto resolve = [](const RawFrame &raw, std::vector<uint8_t> &out) {
        for (size_t i = 0; i < raw.size(); i++) {
            out[i] = static_cast<uint8_t>(glm::clamp(glm::unpackHalf1x16(raw[i]), 0.f, 1.f) * 255.f + 0.5f);
        }
    };

    std::vector<uint8_t> pixels(TARGET_SIZE * TARGET_SIZE * 4);
    RawFrame rawPixels;
    auto renderAndRead = [&]() {
        for (int frame = 0; frame < 3; frame++) {
            drawWorld();
            Fern::pollEvents();
            Miren::frame();
        }
        const uint32_t ready = readScene(rawPixels);
        while (Miren::getFrameNumber() <= ready) {
            drawWorld();
            Fern::pollEvents();
            Miren::frame();
        }
        resolve(rawPixels, pixels);
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

    // The pack of a game (AssetPack) and the files a loader opens through GameFiles: what was
    // written is what is read, an entry is found by its path, a path outside the pack is a file.
    std::printf("asset pack\n");
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "panda-smoke-pack";
        std::error_code error;
        std::filesystem::remove_all(folder, error);
        std::filesystem::create_directories(folder, error);
        const std::filesystem::path loose = folder / "loose.txt";
        {
            std::ofstream file(loose, std::ios::binary);
            file << "a file on disk";
        }
        std::string binary(4099, '\0'); // not a multiple of the alignment, with zero bytes inside
        for (size_t i = 0; i < binary.size(); i++) { binary[i] = static_cast<char>(i * 7 + 3); }
        Panda::AssetPack::Writer writer;
        writer.add("Worlds/main.pworld", binary);
        writer.addFile("Texts/loose.txt", loose);
        writer.add("empty.bin", std::string());
        std::string writeError;
        const std::filesystem::path packFile = folder / Panda::AssetPack::FILE_NAME;
        report("pack is written", writer.write(packFile, writeError));

        auto pack = std::make_unique<Panda::AssetPack>();
        report("pack opens", pack->open(packFile) && pack->entries().size() == 3);
        const Panda::AssetPack::Entry *world = pack->find("Worlds/main.pworld");
        bool sameBytes = false;
        if (world != nullptr && world->size == binary.size() && world->offset % 16 == 0) {
            Foundation::Memory block = pack->read(*world);
            sameBytes = block.data != nullptr && std::memcmp(block.data, binary.data(), binary.size()) == 0;
            block.release();
        }
        report("an entry reads back byte for byte", sameBytes);
        report("an unknown path is not an entry", pack->find("Worlds/other.pworld") == nullptr);

        Panda::GameFiles::mount(folder, std::move(pack));
        size_t size = 0;
        Foundation::Memory text = Panda::GameFiles::read(folder / "Texts" / "loose.txt", size, true);
        report(
            "a file copied into the pack is read through GameFiles",
            text.data != nullptr && size == 14 && std::string(static_cast<const char *>(text.data)) == "a file on disk"
        );
        text.release();
        std::unique_ptr<std::istream> stream = Panda::GameFiles::open(folder / "Worlds" / "main.pworld");
        bool streamed = false;
        if (stream) {
            stream->seekg(0, std::ios::end);
            const std::streamoff length = stream->tellg();
            stream->seekg(4000, std::ios::beg);
            char tail[99];
            stream->read(tail, sizeof(tail));
            streamed = length == 4099 && stream->gcount() == 99 && std::memcmp(tail, binary.data() + 4000, 99) == 0;
        }
        report("an entry is a stream that seeks", streamed);
        report(
            "the pack answers for its folder, the disk for the rest",
            Panda::GameFiles::exists(folder / "empty.bin") && !Panda::GameFiles::exists(folder / "missing.bin") &&
                Panda::GameFiles::exists(loose) && !Panda::GameFiles::isPacked(loose)
        );
        Panda::GameFiles::unmount();
        report("unmounted: the entries are not files", !Panda::GameFiles::exists(folder / "empty.bin"));

        // A cooked model: the arrays come back as they went.
        std::vector<Panda::ImportedMeshData> meshes(2);
        meshes[0].primitives.resize(2);
        meshes[0].primitives[0].vertices.resize(5);
        meshes[0].primitives[0].vertices[3].pos = glm::vec3(1.f, 2.f, 3.f);
        meshes[0].primitives[0].vertices[3].uv = glm::vec2(0.25f, 0.75f);
        meshes[0].primitives[0].indices = {0, 1, 2, 2, 3, 4};
        meshes[1].primitives.resize(1);
        meshes[1].primitives[0].vertices.resize(3);
        meshes[1].primitives[0].indices = {2, 1, 0};
        std::stringstream cooked(std::ios::in | std::ios::out | std::ios::binary);
        Panda::CookedMesh::write(cooked, meshes);
        std::vector<Panda::ImportedMeshData> back;
        const bool read = Panda::CookedMesh::read(cooked, back);
        report(
            "a cooked mesh reads back",
            read && back.size() == 2 && back[0].primitives.size() == 2 && back[0].primitives[0].vertices.size() == 5 &&
                back[0].primitives[0].vertices[3].pos == glm::vec3(1.f, 2.f, 3.f) &&
                back[0].primitives[0].vertices[3].uv == glm::vec2(0.25f, 0.75f) &&
                back[0].primitives[0].indices == meshes[0].primitives[0].indices && back[0].primitives[1].vertices.empty() &&
                back[1].primitives[0].indices == meshes[1].primitives[0].indices
        );
        std::stringstream garbage("not a mesh at all, just some text that is long enough");
        report("a file that is not a cooked mesh is refused", !Panda::CookedMesh::read(garbage, back) && back.empty());
        std::filesystem::remove_all(folder, error);
    }

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

    // A list field of a script (a std::vector in the class): an array in the world file, the
    // same list back from the file and from the binary world of a game, and every id of it seen
    // by the reference collector. The last one is what lets the cook of a game find the assets
    // and the worlds a script chooses among, with no list kept by hand (context/export.md).
    std::printf("script list fields\n");
    {
        const auto field = [](const char *name, Panda::ScriptFieldType type, Panda::ScriptFieldValue value) {
            Panda::ScriptFieldDto dto;
            dto.name = name;
            dto.fieldId = 7;
            dto.type = type;
            dto.value = std::move(value);
            return dto;
        };
        const std::vector<Panda::ScriptFieldDto> fields = {
            field("levels", Panda::ScriptFieldType::WORLD, std::vector<Panda::UUID>{Panda::UUID(101), Panda::UUID(102)}),
            field("delays", Panda::ScriptFieldType::FLOAT, std::vector<float>{0.5f, 1.5f}),
            field("counts", Panda::ScriptFieldType::INTEGER, std::vector<int32_t>{}),
            field("speed", Panda::ScriptFieldType::FLOAT, 2.f),
            field("texture", Panda::ScriptFieldType::TEXTURE, Panda::UUID(103)),
            field("clip", Panda::ScriptFieldType::ASSET, Panda::UUID(104)),
            field("music", Panda::ScriptFieldType::ASSET, std::vector<Panda::UUID>{Panda::UUID(105), Panda::UUID(106)}),
        };
        const Panda::ScriptListComponentDto written = {{}, {Panda::ExternalScriptDto("Menu", fields)}};
        const auto same = [&fields](const Panda::ScriptListComponentDto &read) {
            if (read.scripts.size() != 1 || read.scripts[0].scriptFields.size() != fields.size()) { return false; }
            for (size_t i = 0; i < fields.size(); i++) {
                const Panda::ScriptFieldDto &back = read.scripts[0].scriptFields[i];
                if (back.name != fields[i].name || back.type != fields[i].type) { return false; }
                if (Panda::isScriptFieldList(back.value) != Panda::isScriptFieldList(fields[i].value)) { return false; }
                const Panda::ScriptFieldItems a = Panda::scriptFieldItems(back.value);
                const Panda::ScriptFieldItems b = Panda::scriptFieldItems(fields[i].value);
                if (a.count != b.count || std::memcmp(a.data, b.data, a.count * sizeof(uint32_t)) != 0) { return false; }
            }
            return true;
        };

        std::stringstream text;
        Rain::JsonEncoder jsonEncoder(true);
        static_cast<Rain::Encoder &>(jsonEncoder).encode(text, written);
        Panda::ScriptListComponentDto fromText;
        Rain::JsonDecoder jsonDecoder;
        const bool textRead = static_cast<Rain::Decoder &>(jsonDecoder).decode(text, fromText);
        report("a list field survives the world file", textRead && same(fromText));

        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        Rain::BinaryEncoder binaryEncoder;
        static_cast<Rain::Encoder &>(binaryEncoder).encode(binary, written);
        Panda::ScriptListComponentDto fromBinary;
        Rain::BinaryDecoder binaryDecoder;
        const bool binaryRead = static_cast<Rain::Decoder &>(binaryDecoder).decode(binary, fromBinary);
        report("a list field survives the binary world of a game", binaryRead && same(fromBinary));

        Panda::ReferenceCollector references;
        std::ostringstream sink;
        static_cast<Rain::Encoder &>(references).encode(sink, written);
        report(
            "every id of a list is a reference the cook sees",
            references.ids().count(Panda::UUID(101)) == 1 && references.ids().count(Panda::UUID(102)) == 1 &&
                references.ids().count(Panda::UUID(103)) == 1
        );
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
        // The order of a world file depends on the world alone (World::orderedChildren), not on
        // how the entities lie in memory: a world read and written again is the same text.
        report("a world saved twice is the same file", first == second);
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

#ifdef PND_EDITOR
    // Prefabs (context/prefabs.md): an instance is an entry in a file and entities in a world;
    // it follows its prefab except in what it differs in; what differs is found when the file
    // is written. None of this is seen in a picture.
    std::printf("prefabs\n");
    {
        using Panda::UUID;
        const auto entity = [](const char *name, uint32_t id, uint32_t parent, const char *children, float y, const char *more) {
            char text[2048];
            std::snprintf(
                text, sizeof(text),
                "{\"id\":%u,\"tagComponent\":{\"tag\":\"%s\"},\"relationshipComponent\":{\"parent\":%u,\"children\":[%s]},"
                "\"transformComponent\":{\"position\":{\"x\":0,\"y\":%g,\"z\":0},\"rotation\":{\"x\":0,\"y\":0,\"z\":0,\"w\":1},"
                "\"scale\":{\"x\":1,\"y\":1,\"z\":1}}%s}",
                id, name, parent, children, y, more
            );
            return std::string(text);
        };
        const auto light = [](float intensity, float radius) {
            char text[512];
            std::snprintf(
                text, sizeof(text),
                ",\"scriptListComponent\":{\"scripts\":[]},\"pointLightComponent\":{\"color\":{\"x\":1,\"y\":1,\"z\":1,\"w\":1},"
                "\"intensity\":%g,\"radius\":%g,\"isEnabled\":true,\"coneAngle\":0,\"coneSoftness\":0.25,\"castShadows\":false}",
                intensity, radius
            );
            return std::string(text);
        };
        const std::string noScripts = ",\"scriptListComponent\":{\"scripts\":[]}";
        // A lamp: a root whose script names the bulb under it, the bulb with a light, a shade.
        const std::string blink = ",\"scriptListComponent\":{\"scripts\":[{\"name\":\"Blink\",\"scriptFields\":["
                                  "{\"name\":\"bulb\",\"fieldId\":11,\"type\":2,\"value\":2},"
                                  "{\"name\":\"speed\",\"fieldId\":12,\"type\":1,\"value\":2}]}]}";
        const auto lamp = [&](float intensity, float radius, bool shade, bool cord) {
            std::string text = "{\"entities\":[" + entity("Lamp", 1, 0, shade ? "2,3" : cord ? "2,4" : "2", 0.f, blink.c_str()) + "," +
                               entity("Bulb", 2, 1, "", 2.f, light(intensity, radius).c_str());
            if (shade) { text += "," + entity("Shade", 3, 1, "", 2.5f, noScripts.c_str()); }
            if (cord) { text += "," + entity("Cord", 4, 1, "", 1.f, noScripts.c_str()); }
            return text + "]}";
        };
        const Panda::AssetId LAMP(9001), POST(9002), LOOP(9003);
        assetHandler->prefabs[LAMP] = lamp(10.f, 5.f, true, false);
        // A post: a lamp nested in it, its bulb wider there.
        assetHandler->prefabs[POST] =
            "{\"entities\":[" + entity("Post", 60, 0, "50", 0.f, noScripts.c_str()) +
            ",{\"id\":50,\"prefab\":9001,\"relationshipComponent\":{\"parent\":60,\"children\":[]},"
            "\"transformComponent\":{\"position\":{\"x\":0,\"y\":3,\"z\":0},\"rotation\":{\"x\":0,\"y\":0,\"z\":0,\"w\":1}},"
            "\"overrides\":[{\"entity\":2,\"pointLightComponent\":{\"radius\":9}}]}]}";
        // A prefab that holds itself.
        assetHandler->prefabs[LOOP] =
            "{\"entities\":[" + entity("Loop", 70, 0, "71", 0.f, noScripts.c_str()) +
            ",{\"id\":71,\"prefab\":9003,\"relationshipComponent\":{\"parent\":70,\"children\":[]}}]}";
        const std::function<void(Panda::AssetId)> reload = [assetHandler](Panda::AssetId id) { assetHandler->reload(id); };

        const std::string place = "\"transformComponent\":{\"position\":{\"x\":5,\"y\":0,\"z\":0},\"rotation\":{\"x\":0,\"y\":0,\"z\":0,\"w\":1}}";
        const std::string file =
            "{\"entities\":[{\"id\":100,\"prefab\":9001,\"relationshipComponent\":{\"parent\":0,\"children\":[101]}," + place +
            ",\"overrides\":[{\"entity\":2,\"pointLightComponent\":{\"intensity\":20}}]}," + entity("Sticker", 101, 100, "", 0.f, noScripts.c_str()) +
            ",{\"id\":200,\"prefab\":9001,\"relationshipComponent\":{\"parent\":0,\"children\":[]}," + place + "}," +
            "{\"id\":300,\"prefab\":9002,\"relationshipComponent\":{\"parent\":0,\"children\":[]}," + place +
            ",\"overrides\":[{\"entity\":[50,2],\"pointLightComponent\":{\"intensity\":33}}]}," +
            "{\"id\":400,\"prefab\":9999,\"relationshipComponent\":{\"parent\":0,\"children\":[]}," + place +
            ",\"overrides\":[{\"entity\":7,\"tagComponent\":{\"tag\":\"Kept\"}}]}]}";

        auto *instances = new Panda::World();
        report("a world with instances loads", Panda::Prefabs::load(*instances, file));
        const auto intensityOf = [](Panda::Entity entity) {
            return entity.isValid() && entity.hasComponent<Panda::PointLightComponent>() ? entity.getComponent<Panda::PointLightComponent>().intensity : -1.f;
        };
        const auto radiusOf = [](Panda::Entity entity) {
            return entity.isValid() && entity.hasComponent<Panda::PointLightComponent>() ? entity.getComponent<Panda::PointLightComponent>().radius : -1.f;
        };
        const auto bulbFieldOf = [](Panda::Entity entity) {
            if (!entity.isValid()) { return UUID(0); }
            auto &scripts = entity.getComponent<Panda::ScriptListComponent>().scripts;
            if (scripts.empty() || !scripts[0].hasField("bulb")) { return UUID(0); }
            const UUID *value = std::get_if<UUID>(&scripts[0].getField("bulb").value);
            return value != nullptr ? *value : UUID(0);
        };
        const UUID bulb100 = Panda::derivedEntityId(UUID(100), UUID(2));
        const UUID bulb200 = Panda::derivedEntityId(UUID(200), UUID(2));
        const UUID bulb300 = Panda::derivedEntityId(UUID(300), Panda::derivedEntityId(UUID(50), UUID(2)));
        report(
            "the entities of an instance are entities of the world, with ids derived from its root",
            instances->getById(bulb100).isValid() && instances->getById(bulb200).isValid() && instances->getById(UUID(100)).getName() == "Lamp" &&
                instances->getById(bulb100).getParent() == instances->getById(UUID(100))
        );
        report("an instance has what it overrides, another one what the prefab has",
               intensityOf(instances->getById(bulb100)) == 20.f && intensityOf(instances->getById(bulb200)) == 10.f);
        report("a reference inside a prefab names the entity of the instance",
               bulbFieldOf(instances->getById(UUID(100))) == bulb100 && bulbFieldOf(instances->getById(UUID(200))) == bulb200);
        report("an entity added under an instance stands under it",
               instances->getById(UUID(101)).isValid() && instances->getById(UUID(101)).getParent() == instances->getById(UUID(100)));
        report(
            "a prefab nested in a prefab: what the outer one overrides, and what the world overrides by the path",
            radiusOf(instances->getById(bulb300)) == 9.f && intensityOf(instances->getById(bulb300)) == 33.f
        );
        report("an instance of a prefab that is not there is one entity", instances->getById(UUID(400)).isValid());

        // The file again: every instance is one entry that holds what differs and nothing else.
        const std::string saved = Panda::Prefabs::save(*instances);
        const std::map<uint32_t, std::string> entries = entitiesJson(saved);
        const auto entryOf = [&entries](uint32_t id) {
            const auto found = entries.find(id);
            return found != entries.end() ? found->second : std::string();
        };
        report("a file holds an entry per instance, not its entities", entries.size() == 5 && entries.contains(100) && entries.contains(101));
        report(
            "an entry holds the name and the place of the instance, what it differs in and nothing else",
            entryOf(100) ==
                "{\"id\":100,\"prefab\":9001,\"relationshipComponent\":{\"parent\":0,\"children\":[101]},"
                "\"tagComponent\":{\"tag\":\"Lamp\"},"
                "\"transformComponent\":{\"position\":{\"x\":5.0,\"y\":0.0,\"z\":0.0},\"rotation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0}},"
                "\"overrides\":[{\"entity\":2,\"pointLightComponent\":{\"intensity\":20.0}}]}"
        );
        report("an entity of a nested prefab is named by the ids on the way down",
               entryOf(300).find("\"overrides\":[{\"entity\":[50,2],\"pointLightComponent\":{\"intensity\":33.0}}]") != std::string::npos);
        report("the entry of a missing prefab is written back as it was read",
               entryOf(400).find("\"prefab\":9999") != std::string::npos &&
                   entryOf(400).find("\"overrides\":[{\"entity\":7,\"tagComponent\":{\"tag\":\"Kept\"}}]") != std::string::npos);
        auto *again = new Panda::World();
        const bool reloaded = Panda::Prefabs::load(*again, saved);
        report("a world with instances saved twice is the same file", reloaded && Panda::Prefabs::save(*again) == saved);
        delete again;

        // The prefab changes: brighter, wider, the shade gone, a cord come. Every instance
        // follows, except in what it differs in.
        assetHandler->prefabs[LAMP] = lamp(15.f, 7.f, false, true);
        const UUID shade200 = Panda::derivedEntityId(UUID(200), UUID(3));
        const bool shadeBefore = instances->getById(shade200).isValid();
        Panda::Prefabs::prefabChanged(LAMP, {instances}, reload);
        report(
            "a changed prefab reaches its instances, an override stays",
            intensityOf(instances->getById(bulb100)) == 20.f && radiusOf(instances->getById(bulb100)) == 7.f &&
                intensityOf(instances->getById(bulb200)) == 15.f && radiusOf(instances->getById(bulb200)) == 7.f
        );
        report("an entity the prefab lost leaves its instances, one it gained comes",
               shadeBefore && !instances->getById(shade200).isValid() && instances->getById(Panda::derivedEntityId(UUID(200), UUID(4))).isValid());
        report("the prefab that nests it follows too",
               radiusOf(instances->getById(bulb300)) == 9.f && intensityOf(instances->getById(bulb300)) == 33.f &&
                   instances->getById(Panda::derivedEntityId(UUID(300), Panda::derivedEntityId(UUID(50), UUID(4)))).isValid());
        report("a prefab that changed leaves the world as saved as it was", !instances->isChanged());

        // What differs, as the editor shows it; Revert and Apply.
        instances->getById(bulb200).getComponent<Panda::PointLightComponent>().radius = 3.f;
        Panda::Prefabs::Difference difference = Panda::Prefabs::differenceOf(instances->getById(bulb200));
        report("what an entity differs in is named by its component and its field",
               difference.isPart && difference.differs("pointLightComponent", "radius") && !difference.differs("pointLightComponent", "intensity") &&
                   !difference.differs("transformComponent", ""));
        Panda::Prefabs::Scope radius{instances->getById(bulb200), "pointLightComponent", "radius"};
        Panda::Prefabs::setEntry(instances->getById(UUID(200)), Panda::Prefabs::entryWithout(instances->getById(UUID(200)), radius));
        report("Revert puts the value of the prefab back",
               radiusOf(instances->getById(bulb200)) == 7.f && !Panda::Prefabs::differenceOf(instances->getById(bulb200)).any());
        // A field of a script: the difference is named by the script and the field, and taking
        // it back leaves the entity the scripts of the prefab — the list is read again, not joined.
        {
            Panda::Entity lamp200 = instances->getById(UUID(200));
            auto &scripts = lamp200.getComponent<Panda::ScriptListComponent>().scripts;
            if (!scripts.empty() && scripts[0].hasField("speed")) { scripts[0].getField("speed").value = 9.f; }
            const bool named = Panda::Prefabs::differenceOf(lamp200).differs("scriptListComponent", "scripts/Blink/scriptFields/speed");
            Panda::Prefabs::Scope speed{lamp200, "scriptListComponent", "scripts/Blink/scriptFields/speed"};
            Panda::Prefabs::setEntry(lamp200, Panda::Prefabs::entryWithout(lamp200, speed));
            auto &after = lamp200.getComponent<Panda::ScriptListComponent>().scripts;
            const float *value = after.size() == 1 && after[0].hasField("speed") ? std::get_if<float>(&after[0].getField("speed").value) : nullptr;
            report("a script field taken back leaves the scripts of the prefab, once",
                   named && value != nullptr && *value == 2.f && bulbFieldOf(lamp200) == bulb200);
        }
        instances->getById(UUID(100)).setName("Hall Lamp"); // what an instance is called is its own, like where it stands
        const Panda::Prefabs::Applied applied = Panda::Prefabs::apply(instances->getById(UUID(100)), {}, assetHandler->prefabs[LAMP]);
        report("Apply writes the difference into the prefab file", applied.any() && applied.file.find("\"intensity\": 20.0") != std::string::npos);
        assetHandler->prefabs[LAMP] = applied.file;
        Panda::Prefabs::prefabChanged(LAMP, {instances}, reload);
        report("after Apply the other instances have the value and the applied one differs in nothing",
               intensityOf(instances->getById(bulb200)) == 20.f && Panda::Prefabs::differenceCount(instances->getById(UUID(100))) == 0);
        report("the name of an instance is not applied, and not taken from it by the prefab",
               applied.file.find("Hall Lamp") == std::string::npos && instances->getById(UUID(100)).getName() == "Hall Lamp" &&
                   instances->getById(UUID(200)).getName() == "Lamp");
        report("the sticker added under the instance became an entity of the prefab",
               applied.file.find("\"tag\": \"Sticker\"") != std::string::npos &&
                   instances->getById(Panda::derivedEntityId(UUID(200), UUID(101))).isValid());

        // Copies: a new instance, a duplicate of one — each points at its own entities.
        Panda::Entity made = instances->instantiate(Panda::AssetRef<Panda::PrefabAsset>(LAMP));
        const UUID madeBulb = made.isValid() ? Panda::derivedEntityId(made.getId(), UUID(2)) : UUID(0);
        report("a new instance: its entities by derived ids, its references its own",
               made.isValid() && instances->getById(madeBulb).isValid() && bulbFieldOf(made) == madeBulb);
        Panda::Entity copy = instances->duplicateEntity(instances->getById(UUID(100)));
        const UUID copyBulb = copy.isValid() ? Panda::derivedEntityId(copy.getId(), UUID(2)) : UUID(0);
        report("a duplicate of an instance is an instance of its own",
               copy.isValid() && instances->getById(copyBulb).isValid() && bulbFieldOf(copy) == copyBulb &&
                   Panda::Prefabs::instanceRoot(instances->getById(copyBulb)) == copy);

        // A prefab that holds itself is refused, not followed forever.
        auto *loop = new Panda::World();
        const bool loopLoaded = Panda::Prefabs::load(
            *loop, "{\"entities\":[{\"id\":500,\"prefab\":9003,\"relationshipComponent\":{\"parent\":0,\"children\":[]}}]}"
        );
        report("a prefab that holds itself is refused", loopLoaded && loop->getById(UUID(500)).isValid());
        delete loop;

        // The patch of an object: a list of named things is compared by name.
        rapidjson::Document base, value, patch;
        base.Parse("{\"scripts\":[{\"name\":\"A\",\"speed\":1,\"at\":{\"x\":1,\"y\":2}},{\"name\":\"B\",\"speed\":2}],\"list\":[1,2]}");
        value.Parse("{\"scripts\":[{\"name\":\"A\",\"speed\":5,\"at\":{\"x\":1,\"y\":3}},{\"name\":\"C\",\"speed\":3}],\"list\":[1,2]}");
        const bool differs = Panda::Prefabs::diff(base, value, patch, patch.GetAllocator());
        rapidjson::StringBuffer patchText;
        rapidjson::Writer<rapidjson::StringBuffer> patchWriter(patchText);
        patch.Accept(patchWriter);
        report("a list of named things is compared by name, a vector as one value",
               differs && std::string(patchText.GetString()) ==
                              "{\"scripts\":{\"A\":{\"speed\":5,\"at\":{\"x\":1,\"y\":3}},\"C\":{\"speed\":3},\"B\":null}}");
        Panda::Prefabs::merge(base, patch, base.GetAllocator());
        report("a patch merged into its base gives the value", base == value);

        // A tree of a terrain is a copy of a prefab that no entity stands for: the colliders of
        // the prefab are shapes of the body of the terrain, where the tree stands, at its turn
        // and its size. Nothing of it is seen in a picture.
        {
            const Panda::AssetId TREE(9004);
            const std::string trunk = noScripts + ",\"capsuleCollider3dComponent\":{\"isSensor\":false,"
                                                  "\"offset\":{\"x\":0,\"y\":0,\"z\":0},\"radius\":0.5,\"halfHeight\":1.5,"
                                                  "\"density\":1,\"friction\":0.5,\"restitution\":0}";
            // The trunk stands 2 m above the root of the prefab.
            assetHandler->prefabs[TREE] = "{\"entities\":[" + entity("Tree", 1, 0, "2", 0.f, noScripts.c_str()) + "," +
                                          entity("Trunk", 2, 1, "", 2.f, trunk.c_str()) + "]}";
            Panda::FloraSettings flora;
            Panda::TreeLayer layer;
            layer.id = 1;
            layer.prefab = Panda::AssetRef<Panda::Asset>(TREE);
            flora.trees.push_back(layer);
            Panda::TerrainTrees trees;
            Panda::TreeInstance tree;
            tree.x = 10.f;
            tree.z = 12.f;
            tree.yaw = 0.7f;
            tree.scale = 2.f;
            tree.layer = 1;
            trees.add(tree);
            auto *forest = new Panda::World();
            Panda::Entity ground = forest->instantiateEntity();
            ground.getTransform().setPosition({100.f, 5.f, 100.f});
            auto &groundTerrain = ground.addComponent<Panda::Terrain2DComponent>();
            groundTerrain.terrain = Panda::GameContext::getAssetHandler()
                                        ->createRuntimeAsset<Panda::Terrain2DAsset>(Panda::TerrainData2D::makeFlat(32, 32, 1.f), flora, trees)
                                        .asBaseAsset();
            groundTerrain.physicsEnabled = true;
            forest->startRunning();
            Panda::Physics3D *physics = forest->findContext<Panda::Physics3D>();
            Panda::Entity touched;
            const auto fall = [&](float x, float z) {
                Panda::RaycastHit3D hit;
                if (physics == nullptr || !physics->raycast(Panda::Vec3(x, 100.f, z), Panda::Vec3(0.f, -1.f, 0.f), 200.f, Panda::Entity(), hit)) {
                    return -1000.f;
                }
                touched = hit.entity;
                return hit.point.y;
            };
            // The ground is at 5. The tree is twice its size: the trunk 4 m above the ground, a
            // capsule of 3 m to each side and a radius of 1 — its top is 8 m above the ground.
            const float aside = fall(120.f, 120.f);
            const float atTree = fall(110.f, 112.f);
            report("the trees of a prefab layer collide where they stand",
                   std::abs(atTree - 13.f) < 0.05f && std::abs(aside - 5.f) < 0.05f && touched == ground);
            forest->finishRunning();
            delete forest;
        }

        delete instances;
        Panda::Prefabs::clear();
    }
#endif

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
    std::vector<RawFrame> motionRaw(MOTION_FRAMES);
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
        drawWorld();
        lastReady = readScene(motionRaw[frame]);
        Fern::pollEvents();
        Miren::frame();
    }
    while (Miren::getFrameNumber() <= lastReady) {
        drawWorld();
        Fern::pollEvents();
        Miren::frame();
    }
    for (int frame = 0; frame < MOTION_FRAMES; frame++) { resolve(motionRaw[frame], motionFrames[frame]); }
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
    delete worldView;
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

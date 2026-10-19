// Miren smoke test. On the active backend it checks:
// 1) a depth-only pass into DEPTH32F and reading through a comparison sampler (SampleCmp) — the
//    foundation of the shadow map (CSM): an FBO with depth only and a depth-only PSO without color
//    output; a comparison sampler (Metal: depth2d + sample_compare, GL: sampler2DShadow);
//    CLAMP_TO_BORDER (beyond the map edge — light), MIREN_STATE_CULL_FRONT,
//    MIREN_STATE_NO_COLOR_WRITE, setDepthBias, setViewClearDepth. Scene: the light and the camera
//    look straight down; a square occluder hangs above the receiver plane. The shadow map covers
//    x,z ∈ [-1,1], the receiver is wider ([-2,2]) — beyond the map edge the border kicks in. The
//    result is read from a 64×64 RGBA8 target and verified by regions (all of them are vertically
//    symmetric — the row order on readback differs between backends).
// 2) a 3D texture (TextureCreate::m_depth): creation with data, the slice layout and the axis
//    order, trilinear filtering along depth (Texture3D + SamplerState), updateTextureRegion3D
//    and a full re-upload via updateTexture. A sample at the point from the UBO fills the whole
//    target.
//
// Shaders are compiled at startup the same way as user shaders in the editor:
// dxc → SPIR-V → spirv-cross (MSL/GLSL); D3D11 compiles HLSL itself.

#include <Fern/Fern.hpp>
#include <Foundation/Foundation.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Miren/Miren.hpp>
#include <MirenTools.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int TARGET_SIZE = 64;
constexpr int SHADOW_SIZE = 256;
constexpr Miren::ViewId SHADOW_VIEW = 2; // views execute in descending id order: shadow before scene
constexpr Miren::ViewId MAIN_VIEW = 1;
constexpr Miren::ViewId WINDOW_VIEW = 0;

struct Mesh {
    Miren::VertexBufferHandle vertexBuffer;
    Miren::IndexBufferHandle indexBuffer;
};

Mesh createQuad(Miren::VertexLayoutHandle layout, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    const float vertices[12] = {a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, d.x, d.y, d.z};
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    Mesh mesh;
    mesh.vertexBuffer =
        Miren::createVertexBuffer(Foundation::Memory::copying(vertices, sizeof(vertices)), sizeof(vertices), layout);
    mesh.indexBuffer = Miren::createIndexBuffer(
        Foundation::Memory::copying(indices, sizeof(indices)), Miren::BufferElementType::UnsignedInt, 6
    );
    return mesh;
}

void deleteMesh(Mesh &mesh) {
    Miren::deleteVertexBuffer(mesh.vertexBuffer);
    Miren::deleteIndexBuffer(mesh.indexBuffer);
}

// Cooking for the active backend; the path to the ready file (for D3D11 — the source itself).
path_t cookShader(const path_t &sourceDir, const path_t &outDir, const char *name, MirenTools::ShaderType type) {
    const Foundation::GraphicsAPI api = Foundation::ACTIVE_GRAPHICS_API;
    const path_t source = sourceDir / (std::string(name) + ".hlsl");
    if (api == Foundation::GraphicsAPI::D3D11) { return source; }
    const path_t spv = outDir / (std::string(name) + ".spv");
    if (!MirenTools::compileHlslShaderToSpv(source, spv, type)) { return {}; }
    if (api == Foundation::GraphicsAPI::Metal) {
        const path_t msl = outDir / (std::string(name) + ".msl");
        if (!MirenTools::compileSpvShaderToMsl(spv, msl, type)) { return {}; }
        return msl;
    }
    const path_t glsl = outDir / (std::string(name) + ".glsl");
    const MirenTools::GlslTarget target =
        api == Foundation::GraphicsAPI::OpenGLES ? MirenTools::GlslTarget::OpenGLES : MirenTools::GlslTarget::OpenGL;
    if (!MirenTools::compileSpvShaderToGlsl(spv, glsl, type, target)) { return {}; }
    return glsl;
}

Foundation::Memory readTextFile(const path_t &path, uint32_t &outSize) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    outSize = static_cast<uint32_t>(text.size());
    Foundation::Memory memory = Foundation::Memory::alloc(outSize + 1);
    std::memcpy(memory.data, text.data(), outSize);
    static_cast<char *>(memory.data)[outSize] = 0;
    return memory;
}

Miren::ProgramHandle
loadProgram(const path_t &sourceDir, const path_t &outDir, const char *vertex, const char *fragment) {
    const path_t vertexPath = cookShader(sourceDir, outDir, vertex, MirenTools::VERTEX);
    const path_t fragmentPath = cookShader(sourceDir, outDir, fragment, MirenTools::FRAGMENT);
    if (vertexPath.empty() || fragmentPath.empty()) {
        std::printf("FAIL: shader cook %s / %s\n", vertex, fragment);
        return MIREN_INVALID_HANDLE;
    }
    Miren::ProgramCreate create;
    create.m_vertexBinary = readTextFile(vertexPath, create.m_vertexBinarySize);
    create.m_fragmentBinary = readTextFile(fragmentPath, create.m_fragmentBinarySize);
    return Miren::createProgram(create);
}

// Light NDC → (u, v, depth) of the shadow map. Row v=0 is the bottom on GL (v = y*0.5+0.5) and
// the top on D3D/Metal (v = 0.5-y*0.5); on GL depth is written to the buffer as z*0.5+0.5.
glm::mat4 shadowTextureMatrix(const glm::mat4 &lightViewProj) {
    const bool bottomLeft = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
    const bool homogeneousDepth = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth;
    glm::mat4 bias(1.f);
    bias[0][0] = 0.5f;
    bias[3][0] = 0.5f;
    bias[1][1] = bottomLeft ? 0.5f : -0.5f;
    bias[3][1] = 0.5f;
    bias[2][2] = homogeneousDepth ? 0.5f : 1.f;
    bias[3][2] = homogeneousDepth ? 0.5f : 0.f;
    return bias * lightViewProj;
}

// Orthographic projection for the backend's clip range: GL — [-1,1], Metal/D3D — [0,1] (ZO is
// mandatory, otherwise Metal clips away the near half of the volume).
glm::mat4 topDownOrtho(float halfSize, float zNear, float zFar) {
    if (Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth) {
        return glm::ortho(-halfSize, halfSize, -halfSize, halfSize, zNear, zFar);
    }
    return glm::orthoZO(-halfSize, halfSize, -halfSize, halfSize, zNear, zFar);
}

struct Variant {
    const char *name;
    bool cullFront; // occluder is wound with its "back" to the light: survives only with CULL_FRONT
    float depthBias; // constant depth bias of the occluder
    float shadowClearDepth;
    bool expectCenterLit; // region under the occluder
    bool expectInsideLit; // inside the map, outside the occluder
};

const Variant VARIANTS[] = {
    {"baseline (front cull, no bias)", true, 0.f, 1.f, false, true},
    {"clear depth 0 (all inside map shadowed)", true, 0.f, 0.f, false, false},
    {"large depth bias (occluder pushed behind receiver)", true, 1.e7f, 1.f, true, true},
    {"back cull (occluder culled away)", false, 0.f, 1.f, true, true},
};

struct Region {
    const char *name;
    int x0, x1, y0, y1;
};

// World → pixel: x = -2 + (i + 0.5) / 16. All regions are row-symmetric.
const Region CENTER = {"under occluder", 26, 38, 26, 38};         // |x|,|z| < 0.35
const Region INSIDE = {"inside map, unshadowed", 17, 22, 26, 38}; // x ∈ [-0.9,-0.6]
const Region OUTSIDE = {"outside map (border)", 2, 10, 26, 38};   // x < -1.3
const Region MASKED = {"NO_COLOR_WRITE quad", 54, 62, 26, 38};    // x ∈ [1.4,1.9]

float meanChannel(const std::vector<uint8_t> &pixels, const Region &region, int channel) {
    float sum = 0.f;
    int count = 0;
    for (int y = region.y0; y < region.y1; y++) {
        for (int x = region.x0; x < region.x1; x++) {
            sum += pixels[(y * TARGET_SIZE + x) * 4 + channel];
            count++;
        }
    }
    return count > 0 ? sum / count : 0.f;
}

bool check(const char *variant, const Region &region, float value, bool expectLit, int &failures) {
    const bool lit = value >= 250.f;
    const bool dark = value <= 5.f;
    const bool ok = expectLit ? lit : dark;
    std::printf(
        "  [%s] %-26s R=%6.1f expected %s\n", ok ? " OK " : "FAIL", region.name, value, expectLit ? "lit" : "dark"
    );
    if (!ok) { failures++; }
    return ok;
}

} // namespace

int main(int argc, const char **argv) {
    // slangc for cooking the test shaders — the one the project is built with (Vendor/tools).
    MirenTools::setSlangcExecutable(MIREN_SMOKE_SLANGC);
    Fern::initialize();
    Fern::Window *window =
        Fern::createWindow("Miren Smoke", Fern::Rect(0, 0, 320, 320), Fern::WindowState::WindowStateNormal);
    Miren::initialize(window->getDrawingContext());

    const path_t shaderDir = MIREN_SMOKE_SHADER_DIR;
    const path_t cookDir = std::filesystem::temp_directory_path() / "MirenSmoke";
    std::filesystem::create_directories(cookDir);
    Miren::ProgramHandle depthProgram = loadProgram(shaderDir, cookDir, "shadow_depth.vert", "shadow_depth.frag");
    Miren::ProgramHandle receiverProgram = loadProgram(shaderDir, cookDir, "receiver.vert", "receiver.frag");
    Miren::ProgramHandle flatProgram = loadProgram(shaderDir, cookDir, "shadow_depth.vert", "flat.frag");
    Miren::ProgramHandle volumeProgram = loadProgram(shaderDir, cookDir, "shadow_depth.vert", "volume.frag");

    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3();
    Miren::VertexLayoutHandle layout = Miren::createVertexLayout(layoutData);
    // Vertex order: from the camera above (right = +x, up = -z) — clockwise, i.e. back-face.
    Mesh occluder =
        createQuad(layout, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f});
    Mesh receiver = createQuad(layout, {-2.f, 0.f, -2.f}, {2.f, 0.f, -2.f}, {2.f, 0.f, 2.f}, {-2.f, 0.f, 2.f});
    Mesh maskedQuad = createQuad(layout, {1.25f, 1.f, -2.f}, {2.f, 1.f, -2.f}, {2.f, 1.f, 2.f}, {1.25f, 1.f, 2.f});

    // Shadow map: depth only, comparison sampler, white border.
    Miren::TextureCreate shadowCreate;
    shadowCreate.m_format = Miren::TextureFormat::DEPTH32F;
    shadowCreate.m_width = SHADOW_SIZE;
    shadowCreate.m_height = SHADOW_SIZE;
    shadowCreate.m_minFiltering = Miren::LINEAR;
    shadowCreate.m_magFiltering = Miren::LINEAR;
    shadowCreate.m_wrap = Miren::CLAMP_TO_BORDER;
    shadowCreate.m_compare = Miren::COMPARE_LESS_EQUAL;
    Miren::TextureHandle shadowMap = Miren::createTexture(shadowCreate);
    Miren::FrameBufferAttachment shadowAttachments[1] = {shadowMap};
    Miren::FrameBufferHandle shadowFrameBuffer =
        Miren::createFrameBuffer(Miren::FrameBufferSpecification(shadowAttachments, 1));

    Miren::TextureCreate colorCreate;
    colorCreate.m_format = Miren::TextureFormat::RGBA8;
    colorCreate.m_width = TARGET_SIZE;
    colorCreate.m_height = TARGET_SIZE;
    colorCreate.m_wrap = Miren::CLAMP;
    Miren::TextureHandle colorTarget = Miren::createTexture(colorCreate);
    Miren::FrameBufferAttachment colorAttachments[1] = {colorTarget};
    Miren::FrameBufferHandle mainFrameBuffer =
        Miren::createFrameBuffer(Miren::FrameBufferSpecification(colorAttachments, 1));

    const glm::mat4 view = glm::lookAt(glm::vec3(0.f, 5.f, 0.f), glm::vec3(0.f), glm::vec3(0.f, 0.f, -1.f));
    const glm::mat4 lightViewProj = topDownOrtho(1.f, 0.1f, 10.f) * view;
    const glm::mat4 cameraViewProj = topDownOrtho(2.f, 0.1f, 10.f) * view;
    struct ReceiverUbo {
        glm::mat4 projViewMtx;
        glm::mat4 shadowMtx;
    } receiverUbo{cameraViewProj, shadowTextureMatrix(lightViewProj)};

    Miren::setViewFrameBuffer(SHADOW_VIEW, shadowFrameBuffer);
    Miren::setViewport(SHADOW_VIEW, Miren::Rect(0, 0, SHADOW_SIZE, SHADOW_SIZE));
    Miren::setViewFrameBuffer(MAIN_VIEW, mainFrameBuffer);
    Miren::setViewport(MAIN_VIEW, Miren::Rect(0, 0, TARGET_SIZE, TARGET_SIZE));
    Miren::setViewClear(MAIN_VIEW, 0xff00ffff);
    Miren::setViewClear(WINDOW_VIEW, 0x202020ff);

    auto encodeFrame = [&](const Variant &variant) {
        Miren::setViewClearDepth(SHADOW_VIEW, true, variant.shadowClearDepth);
        // The occluder — into the shadow map (depth-only).
        Miren::addInputUniformBuffer("UBO_VERT", &lightViewProj, sizeof(lightViewProj));
        Miren::setState(
            MIREN_STATE_DEPTH_TEST | MIREN_STATE_NO_COLOR_WRITE | MIREN_STATE_CULL_FACE |
            (variant.cullFront ? MIREN_STATE_CULL_FRONT : 0)
        );
        Miren::setDepthBias(variant.depthBias, 0.f);
        Miren::setVertexBuffer(occluder.vertexBuffer);
        Miren::setIndexBuffer(occluder.indexBuffer, 0, 6);
        Miren::submit(SHADOW_VIEW, depthProgram);
        // The receiver — into the target and into the window.
        for (Miren::ViewId viewId : {MAIN_VIEW, WINDOW_VIEW}) {
            Miren::addInputUniformBuffer("UBO_VERT", &receiverUbo, sizeof(receiverUbo));
            Miren::addInputTexture("shadowMap", shadowMap);
            Miren::setState(0);
            Miren::setVertexBuffer(receiver.vertexBuffer);
            Miren::setIndexBuffer(receiver.indexBuffer, 0, 6);
            Miren::submit(viewId, receiverProgram);
        }
        // A green square without color write — the target must stay untouched.
        Miren::addInputUniformBuffer("UBO_VERT", &cameraViewProj, sizeof(cameraViewProj));
        Miren::setState(MIREN_STATE_NO_COLOR_WRITE);
        Miren::setVertexBuffer(maskedQuad.vertexBuffer);
        Miren::setIndexBuffer(maskedQuad.indexBuffer, 0, 6);
        Miren::submit(MAIN_VIEW, flatProgram);
    };

    auto pumpFrame = [&]() {
        Fern::pollEvents();
        Miren::frame();
    };

    int failures = 0;
    std::vector<uint8_t> pixels(TARGET_SIZE * TARGET_SIZE * 4);
    for (const Variant &variant : VARIANTS) {
        std::printf("%s\n", variant.name);
        encodeFrame(variant);
        pumpFrame();
        encodeFrame(variant);
        std::fill(pixels.begin(), pixels.end(), 0);
        const uint32_t readyFrame =
            Miren::readFrameBuffer(mainFrameBuffer, 0, 0, 0, TARGET_SIZE, TARGET_SIZE, pixels.data());
        pumpFrame();
        while (Miren::getFrameNumber() <= readyFrame) {
            encodeFrame(variant);
            pumpFrame();
        }
        check(variant.name, CENTER, meanChannel(pixels, CENTER, 0), variant.expectCenterLit, failures);
        check(variant.name, INSIDE, meanChannel(pixels, INSIDE, 0), variant.expectInsideLit, failures);
        check(variant.name, OUTSIDE, meanChannel(pixels, OUTSIDE, 0), true, failures);
        // The square without color write lies beyond the map edge — the receiver under it is lit
        // and white.
        const float maskedR = meanChannel(pixels, MASKED, 0);
        const float maskedG = meanChannel(pixels, MASKED, 1);
        const bool maskedOk = maskedR >= 250.f && maskedG >= 250.f;
        std::printf(
            "  [%s] %-26s R=%6.1f G=%6.1f expected untouched (white)\n",
            maskedOk ? " OK " : "FAIL",
            MASKED.name,
            maskedR,
            maskedG
        );
        if (!maskedOk) { failures++; }
    }

    // ---- 3D texture: creation with data, trilinear sample, box, full re-upload.
    std::printf("volume texture\n");
    {
        constexpr int VOLUME = 4;
        // Texel (x, y, z) = (40 + 50x, 40 + 50y, 40 + 50z, 255): the channel shows which axis is
        // sampled and in which order the slices are laid out (index (z·N + y)·N + x).
        std::vector<uint8_t> voxels(VOLUME * VOLUME * VOLUME * 4);
        for (int z = 0; z < VOLUME; z++) {
            for (int y = 0; y < VOLUME; y++) {
                for (int x = 0; x < VOLUME; x++) {
                    uint8_t *texel = &voxels[((z * VOLUME + y) * VOLUME + x) * 4];
                    texel[0] = static_cast<uint8_t>(40 + 50 * x);
                    texel[1] = static_cast<uint8_t>(40 + 50 * y);
                    texel[2] = static_cast<uint8_t>(40 + 50 * z);
                    texel[3] = 255;
                }
            }
        }
        Miren::TextureCreate volumeCreate;
        volumeCreate.m_format = Miren::TextureFormat::RGBA8;
        volumeCreate.m_width = VOLUME;
        volumeCreate.m_height = VOLUME;
        volumeCreate.m_depth = VOLUME;
        volumeCreate.m_minFiltering = Miren::LINEAR;
        volumeCreate.m_magFiltering = Miren::LINEAR;
        volumeCreate.m_wrap = Miren::CLAMP;
        volumeCreate.m_data = Foundation::Memory::copying(voxels.data(), voxels.size());
        Miren::TextureHandle volume = Miren::createTexture(volumeCreate);
        // Screen quad in clip space (z = 0.5 lies within the depth range of any backend).
        Mesh screenQuad =
            createQuad(layout, {-1.f, -1.f, 0.5f}, {1.f, -1.f, 0.5f}, {1.f, 1.f, 0.5f}, {-1.f, 1.f, 0.5f});
        const glm::mat4 identity(1.f);
        auto encodeVolume = [&](const glm::vec4 &coord) {
            Miren::addInputUniformBuffer("UBO_VERT", &identity, sizeof(identity));
            Miren::addInputUniformBuffer("UBO_FRAG", &coord, sizeof(coord));
            Miren::addInputTexture("volume", volume);
            Miren::setState(0);
            Miren::setVertexBuffer(screenQuad.vertexBuffer);
            Miren::setIndexBuffer(screenQuad.indexBuffer, 0, 6);
            Miren::submit(MAIN_VIEW, volumeProgram);
        };
        auto sampleVolume = [&](const char *name, glm::vec3 coord, int r, int g, int b) {
            const glm::vec4 coord4(coord, 0.f);
            encodeVolume(coord4);
            pumpFrame();
            encodeVolume(coord4);
            std::fill(pixels.begin(), pixels.end(), 0);
            const uint32_t readyFrame =
                Miren::readFrameBuffer(mainFrameBuffer, 0, 0, 0, TARGET_SIZE, TARGET_SIZE, pixels.data());
            pumpFrame();
            while (Miren::getFrameNumber() <= readyFrame) {
                encodeVolume(coord4);
                pumpFrame();
            }
            const float got[3] = {
                meanChannel(pixels, CENTER, 0), meanChannel(pixels, CENTER, 1), meanChannel(pixels, CENTER, 2)
            };
            const bool ok = std::abs(got[0] - static_cast<float>(r)) <= 4.f &&
                            std::abs(got[1] - static_cast<float>(g)) <= 4.f &&
                            std::abs(got[2] - static_cast<float>(b)) <= 4.f;
            std::printf(
                "  [%s] %-30s RGB=(%5.1f,%5.1f,%5.1f) expected (%3d,%3d,%3d)\n",
                ok ? " OK " : "FAIL", name, got[0], got[1], got[2], r, g, b
            );
            if (!ok) { failures++; }
        };
        const float texel = 1.f / VOLUME;
        // Center of texel (1, 2, 3): the exact value — the order of axes and slices.
        sampleVolume("texel (1,2,3) center", glm::vec3(1.5f, 2.5f, 3.5f) * texel, 90, 140, 190);
        // Between slices z = 1 and z = 2 — trilinear filtering along depth gives the average.
        sampleVolume("between slices z=1..2", glm::vec3(0.5f * texel, 0.5f * texel, 0.5f), 40, 40, 115);
        // A 2×2×2 box from corner (2,2,2): only the box changes.
        std::vector<uint8_t> box(2 * 2 * 2 * 4);
        for (size_t i = 0; i < 8; i++) {
            box[i * 4 + 0] = 200;
            box[i * 4 + 1] = 10;
            box[i * 4 + 2] = 10;
            box[i * 4 + 3] = 255;
        }
        Miren::updateTextureRegion3D(volume, 2, 2, 2, 2, 2, 2, Foundation::Memory::copying(box.data(), box.size()));
        sampleVolume("box update: texel (3,3,3)", glm::vec3(3.5f, 3.5f, 3.5f) * texel, 200, 10, 10);
        sampleVolume("box update: texel (1,1,1) kept", glm::vec3(1.5f, 1.5f, 1.5f) * texel, 90, 90, 90);
        // Full re-upload with the inverted pattern.
        for (size_t i = 0; i < voxels.size(); i += 4) {
            voxels[i + 0] = static_cast<uint8_t>(255 - voxels[i + 0]);
            voxels[i + 1] = static_cast<uint8_t>(255 - voxels[i + 1]);
            voxels[i + 2] = static_cast<uint8_t>(255 - voxels[i + 2]);
        }
        Miren::updateTexture(volume, Foundation::Memory::copying(voxels.data(), voxels.size()));
        sampleVolume("full update: texel (0,0,0)", glm::vec3(0.5f, 0.5f, 0.5f) * texel, 215, 215, 215);
        Miren::deleteTexture(volume);
        deleteMesh(screenQuad);
    }

    // A couple of frames so that the render thread finishes its work before resources are deleted.
    encodeFrame(VARIANTS[0]);
    pumpFrame();
    pumpFrame();

    Miren::deleteFrameBuffer(mainFrameBuffer);
    Miren::deleteFrameBuffer(shadowFrameBuffer);
    Miren::deleteTexture(colorTarget);
    Miren::deleteTexture(shadowMap);
    deleteMesh(occluder);
    deleteMesh(receiver);
    deleteMesh(maskedQuad);
    Miren::deleteVertexLayout(layout);
    Miren::deleteProgram(depthProgram);
    Miren::deleteProgram(receiverProgram);
    Miren::deleteProgram(flatProgram);
    Miren::deleteProgram(volumeProgram);
    pumpFrame();

    Miren::terminate();
    Fern::disposeWindow(window);
    Fern::terminate();

    std::printf(
        "%s (%s, %d failure(s))\n",
        failures == 0 ? "PASSED" : "FAILED",
        Foundation::ACTIVE_GRAPHICS_TRAITS.shaderBackendName,
        failures
    );
    return failures == 0 ? 0 : 1;
}

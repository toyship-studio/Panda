#include "Panda/Renderer/WorldView.hpp"

#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/Components/BaseComponents.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Renderer/SceneWind.hpp"

#include <Foundation/Profile.hpp>

#include <array>

namespace Panda {

bool ViewCamera::main(World &world, ViewCamera &camera) {
    Entity cameraEntity = world.findMainCameraEntity();
    if (!cameraEntity.isValid()) { return false; }
    const glm::mat4 cameraWorld = world.getWorldSpaceTransformMatrix(cameraEntity);
    camera.projection = cameraEntity.getComponent<CameraComponent>().camera.getProjection();
    camera.viewProj = camera.projection * glm::inverse(cameraWorld);
    camera.skyViewProj = camera.projection * glm::inverse(cameraEntity.getTransform().getSkyTransform());
    camera.position = glm::vec3(cameraWorld * glm::vec4(0.f, 0.f, 0.f, 1.f));
    return true;
}

WorldView::~WorldView() {
    if (!m_color.isValid()) { return; }
    Miren::deleteFrameBuffer(m_frameBuffer);
    Miren::deleteTexture(m_color);
    Miren::deleteTexture(m_emissive);
    Miren::deleteTexture(m_depth);
}

void WorldView::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
}

void WorldView::ensureTargets() {
    if (m_width == 0 || m_height == 0) { return; }
    if (!m_color.isValid()) {
        // Linear: a pass that reads the frame between its texels (a downsample, the picture of a
        // preview brought down to its size) averages them.
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_width = static_cast<uint16_t>(m_width);
        create.m_height = static_cast<uint16_t>(m_height);
        create.m_format = Miren::TextureFormat::RGBA16F; // HDR: linear light, no clamp at 1.0
        m_color = Miren::createTexture(create);
        m_emissive = Miren::createTexture(create); // MRT: what glows, for the bloom
        create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
        m_depth = Miren::createTexture(create);
        Miren::FrameBufferAttachment attachments[] = {m_color, m_emissive, m_depth};
        m_frameBufferSpecification = Miren::FrameBufferSpecification(attachments, 3);
        m_frameBuffer = Miren::createFrameBuffer(m_frameBufferSpecification);
    } else if (m_targetWidth != m_width || m_targetHeight != m_height) {
        Miren::resizeTexture(m_color, m_width, m_height);
        Miren::resizeTexture(m_emissive, m_width, m_height);
        Miren::resizeTexture(m_depth, m_width, m_height);
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = Miren::createFrameBuffer(m_frameBufferSpecification);
    }
    m_targetWidth = m_width;
    m_targetHeight = m_height;
}

void WorldView::render(World &world, const ViewCamera &camera) {
    ensureTargets();
    if (!m_color.isValid()) { return; } // no size yet
    // The wind of this world for everything its frame draws (the scene blocks, the system
    // material fields, the modules): a world brings its own, nobody sets it from outside.
    SceneWind::apply(world.getWindSettings());
    m_renderer3d.resetStats();
    m_camera = camera;
    // What the world draws is read once, for every pass of the frame.
    m_meshes.collect(world);
    // The light of the frame, fitted to the camera, and the scene blocks every pass reads it from.
    m_renderer3d.setLighting(m_lighting.update(world, camera.viewProj, camera.position));
    // The views are made in the order they are executed: the scene samples the atlases the shadow
    // passes draw. The first view of an atlas clears it, also when nothing casts into it — no
    // ghost shadows after the casters are gone.
    const CascadedShadowMap &shadowMap = m_lighting.shadowMap();
    const LampShadowMap &lampShadows = m_lighting.lampShadowMap();
    std::array<Miren::ViewId, RENDERER_MAX_SHADOW_CASCADES> cascadeViews{};
    std::array<Miren::ViewId, LAMP_SHADOW_MAX_TILES> tileViews{};
    for (uint32_t cascade = 0; cascade < shadowMap.cascadeCount(); cascade++) {
        cascadeViews[cascade] = shadowMap.addCascadeView(cascade);
    }
    for (uint32_t tile = 0; tile < lampShadows.tileCount(); tile++) {
        tileViews[tile] = lampShadows.addTileView(tile);
    }
    m_sceneView = Miren::addView({
        .frameBuffer = m_frameBuffer,
        .viewport = Miren::Rect(0, 0, static_cast<int>(m_width), static_cast<int>(m_height)),
        .clearColor = m_clearColor,
    });
    // The world is walked in the order its systems count on: the scene pass is the first
    // traversal of a frame — a system that follows the camera moves there (the LOD of a terrain,
    // the blocks of its flora), and the depth passes traverse what it prepared: the same meshes as
    // in the scene, or a surface shadows itself at the LOD seams.
    renderPass(world, RenderingMode::DEFAULT, m_sceneView);
    PANDA_PROFILE_SCOPE_N("WorldView::renderShadows");
    // World traversal per cascade: culling by the light frustum, the LOD of the world systems —
    // by the camera.
    for (uint32_t cascade = 0; cascade < shadowMap.cascadeCount(); cascade++) {
        renderShadowPass(
            world, cascadeViews[cascade], shadowMap.cascadeViewProj(cascade), &shadowMap.cascadeCasterPlanes(cascade)
        );
    }
    // The tiles of the lamps that cast shadows: the same traversal through the eye of the lamp.
    for (uint32_t tile = 0; tile < lampShadows.tileCount(); tile++) {
        renderShadowPass(world, tileViews[tile], lampShadows.tileViewProj(tile));
    }
}

void WorldView::renderPass(World &world, RenderingMode mode, Miren::ViewId view) {
    m_renderer2d.begin(mode, view);
    m_renderer3d.begin(mode, view);
    m_renderer2d.setViewProj(m_camera.viewProj);
    m_renderer3d.setViewProj(m_camera.viewProj);
    submitWorld(world, m_camera.viewProj, m_camera.skyViewProj);
    m_renderer2d.end();
    m_renderer3d.end();
}

void WorldView::submitWorld(
    World &world, const glm::mat4 &viewProj, const glm::mat4 &skyViewProj, const std::vector<glm::vec4> *casters
) {
    PANDA_PROFILE_SCOPE_N("WorldView::submitWorld");
    const bool shadowPass = m_renderer3d.getMode() == RenderingMode::SHADOW_DEPTH;
    RenderSystem::Frustum frustum = RenderSystem::Frustum::of(viewProj);
    if (casters != nullptr) { frustum.narrow(*casters); }
    RenderSystem::submitSky(world, m_renderer3d, skyViewProj);
    if (!shadowPass) { RenderSystem::submitSprites(world, m_renderer2d, frustum); }
    // World systems (contributions of the worldSystems registry) — between sprites and meshes, in
    // all modes.
    for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
        if (system.submit != nullptr) { system.submit(world, m_renderer3d, m_camera.position, viewProj); }
    }
    m_meshes.submit(m_renderer3d, frustum, shadowPass, m_camera.position);
    // What the systems blend over the scene: after the opaque world, or it would paint them over.
    if (shadowPass) { return; }
    for (const WorldSystem &system : GameContext::getExtensions().worldSystems.systems()) {
        if (system.submitBlended != nullptr) { system.submitBlended(world, m_renderer3d, m_camera.position, viewProj); }
    }
}

void WorldView::renderShadowPass(
    World &world, Miren::ViewId view, const glm::mat4 &viewProj, const std::vector<glm::vec4> *casters
) {
    m_renderer3d.begin(RenderingMode::SHADOW_DEPTH, view);
    m_renderer3d.setViewProj(viewProj);
    submitWorld(world, viewProj, viewProj, casters);
    m_renderer3d.end();
}

/* ------------------------------ The look of the frame ------------------------------ */

PostprocessingPipeline &WorldView::pipeline() {
    if (!m_pipeline) { m_pipeline = std::make_unique<PostprocessingPipeline>(); }
    return *m_pipeline;
}

PostprocessingFrameInputs WorldView::postprocessingInputs(World &world) const {
    PostprocessingFrameInputs inputs;
    inputs.color = m_color;
    inputs.emissive = m_emissive;
    inputs.depth = m_depth;
    inputs.setView(*this, world);
    return inputs;
}

Miren::TextureHandle
WorldView::develop(AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, float time) {
    if (m_targetWidth == 0) { return Miren::TextureHandle(); } // nothing was rendered
    const Miren::TextureHandle output = pipeline().apply(chain, inputs, m_targetWidth, m_targetHeight, time);
    m_lighting.setScreenOcclusion(m_pipeline->screenOcclusion()); // the next frame of the view shades with it
    return output;
}

Miren::ViewId WorldView::developInto(
    AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, float time,
    const Miren::ViewDescription &output
) {
    // A view that has rendered nothing has nothing to develop: its target is cleared all the same.
    if (m_targetWidth == 0) { return Miren::addView(output); }
    const Miren::ViewId view = pipeline().applyToView(chain, inputs, time, output);
    m_lighting.setScreenOcclusion(m_pipeline->screenOcclusion());
    return view;
}

} // namespace Panda

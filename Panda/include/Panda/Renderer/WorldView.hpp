#pragma once

#include "Panda/GameLogic/Systems/LightingSystem.hpp"
#include "Panda/GameLogic/Systems/RenderSystem.hpp"
#include "Panda/Renderer/PostprocessingPipeline.hpp"
#include "Panda/Renderer/Renderer2D.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Renderer/RenderingMode.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Panda {

class World;

// The camera of a view for one frame: where the world is seen from.
struct ViewCamera {
    glm::mat4 projection = glm::mat4(1.f); // viewProj = projection * view
    glm::mat4 viewProj = glm::mat4(1.f);
    glm::mat4 skyViewProj = glm::mat4(1.f); // the same without the translation: the sky stays around the camera
    glm::vec3 position = glm::vec3(0.f);
    // The main sky of the world is drawn behind the frame. A studio composed over something else
    // (the preview of an asset) is lit by its sky and does not draw it.
    bool drawsSky = true;

    // The main camera of the world as it stands now — asked after the simulation of the frame,
    // the scripts move the camera in it. false — the world has no main camera: the camera is
    // left as it is.
    static bool main(World &world, ViewCamera &camera);
};

// A view of a world: everything that draws it and is not the world itself. A world is entities,
// settings and their simulation; how it looks from a camera — the passes of a frame, their
// targets and what they keep between frames — belongs to the view the camera looks through: the
// HDR targets of the scene, the renderers of the passes, the lighting fitted to the camera (the
// shadow cascades, the tiles of the lamps, the light of the environment, the occlusion volume,
// the ambient occlusion of the last frame), the meshes of the frame, and the pipeline that
// develops the frame for the display (fog, auto exposure, bloom, the chain of the camera, the
// tonemap). The game has one view; the editor has one for its viewports and one for each kind of
// preview. Any world can be drawn through a view, one frame after another: the view refits to
// what it is shown.
//
// A frame of a view is two steps: render — the world through a camera into the targets of the
// view — and develop — the look of the frame, into a texture of the view or into a target of the
// caller (the screen). Each step makes its passes in the Miren frame being built, so any number
// of views draw in one frame.
class WorldView final {
public:
    WorldView() = default;
    ~WorldView();
    WorldView(const WorldView &) = delete;
    WorldView &operator=(const WorldView &) = delete;

    // The size of the frame in pixels: the targets follow it. Before the first size nothing is drawn.
    void resize(uint32_t width, uint32_t height);
    uint32_t width() const {
        return m_width;
    }
    uint32_t height() const {
        return m_height;
    }
    // What the scene is cleared to before the world is drawn (RGBA): black — a world without a
    // sky is black, as in the game; a preview clears to transparent.
    void setClearColor(uint32_t color) {
        m_clearColor = color;
    }

    // A frame of the world through the camera, as passes of the Miren frame being built: the
    // light is fitted to the camera, then the shadow passes and the scene into the targets of the
    // view — executed in that order, while the world is walked for the scene first (its systems
    // prepare there what the depth passes traverse). The wind of the world is the wind of the frame.
    void render(World &world, const ViewCamera &camera);
    // The Miren view of the scene pass of the frame that was just rendered: who draws into the
    // scene over the world (the grid, the helpers and the tools of the editor) submits there —
    // this frame only, a view lives for one.
    Miren::ViewId sceneView() const {
        return m_sceneView;
    }
    // One more pass over the same frame in another mode into another view (the editor: picking).
    void renderPass(World &world, RenderingMode mode, Miren::ViewId view);
    // Forgets the frame that was drawn. A view holds the assets of the meshes of its last frame
    // until the next one: a view that draws now and then (a thumbnail) lets them go with this.
    void forgetFrame() {
        m_meshes = RenderSystem::MeshList();
    }

    // The frame that was rendered as the pipeline takes it: the targets of the view, the camera
    // of the frame, the sun on screen, the fog and the ambient occlusion of the world. The caller
    // adds the look of a camera (PostprocessingFrameInputs::setCamera) and what it overrides.
    PostprocessingFrameInputs postprocessingInputs(World &world) const;
    // Develops the frame — the engine passes, the chain, the tonemap — and answers with the
    // display-ready texture. A view that has rendered nothing yet develops nothing (an invalid
    // texture).
    Miren::TextureHandle
    develop(AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, float time);
    // The same with the last pass drawn into a target of the caller (the screen, the texture of a
    // thumbnail): `output` describes it, and its viewport is the size the frame is developed at.
    // Answers with the view of that pass — the caller may draw into it over the frame.
    Miren::ViewId developInto(
        AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, float time,
        const Miren::ViewDescription &output
    );

    // What the last render left, for the passes that follow it.
    const ViewCamera &camera() const {
        return m_camera;
    }
    // The sun on screen (xy — UV, z — "in frame" 0/1): the flares of the bloom.
    glm::vec4 sunScreen() const {
        return m_lighting.sunScreen();
    }
    // The light of the world as the scene shaders got it: what a pass over the finished frame —
    // the fog — reads it from.
    const SceneBlocks &sceneBlocks() const {
        return m_renderer3d.sceneBlocks();
    }
    // The box around the meshes of the last frame — all of them, or those of the given entities:
    // what a camera frames. false — nothing with bounds.
    bool meshBounds(glm::vec3 &outMin, glm::vec3 &outMax, const std::vector<UUID> *entities = nullptr) const {
        return m_meshes.bounds(outMin, outMax, entities);
    }
    // The renderers of the passes: who adds draws of its own to a pass of the frame (the editor:
    // the outline of the selection), and the statistics of the frame.
    Renderer2D &renderer2D() {
        return m_renderer2d;
    }
    Renderer3D &renderer3D() {
        return m_renderer3d;
    }

private:
    // The world into the pass the renderers have begun: the sky, the sprites, the world systems,
    // the meshes — culled by the frustum of the pass. `casters` — the planes a depth pass knows
    // besides its matrix (the casters of a cascade), or null.
    void submitWorld(
        World &world, const glm::mat4 &viewProj, const glm::mat4 &skyViewProj,
        const std::vector<glm::vec4> *casters = nullptr
    );
    void renderShadowPass(
        World &world, Miren::ViewId view, const glm::mat4 &viewProj, const std::vector<glm::vec4> *casters = nullptr
    );
    void ensureTargets(); // made at the first frame, resized when the size changed
    PostprocessingPipeline &pipeline();

    // The HDR frame of the scene: color and the emissive buffer of the bloom in linear light, depth.
    Miren::TextureHandle m_color, m_emissive, m_depth;
    Miren::FrameBufferHandle m_frameBuffer;
    Miren::FrameBufferSpecification m_frameBufferSpecification;
    uint32_t m_width = 0, m_height = 0;             // asked for
    uint32_t m_targetWidth = 0, m_targetHeight = 0; // of the targets as they are
    uint32_t m_clearColor = 0x000000ff;
    Miren::ViewId m_sceneView = 0; // of the frame that was just rendered

    Renderer2D m_renderer2d;
    Renderer3D m_renderer3d;
    LightingSystem m_lighting; // the lights of the frame, the shadow maps, the occlusion volume
    // The meshes of the frame, read from the world once in render: every pass draws from it.
    RenderSystem::MeshList m_meshes;
    ViewCamera m_camera;
    std::unique_ptr<PostprocessingPipeline> m_pipeline; // made by the first develop
};

} // namespace Panda

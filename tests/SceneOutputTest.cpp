#include "TestRenderDevice.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/renderer/DrawListBuilder.h"
#include "render/renderer/Renderer.h"
#include "runtime/window/Window.h"
#include <gtest/gtest.h>
#include <memory>

namespace {
class FakeSwapchain final : public rhi::ISwapchain {
public:
    rhi::FrameStatus beginFrame() override { ++begins; return status; }
    rhi::FrameStatus endFrame() override { slot = (slot + 1) % 2; return rhi::FrameStatus::Ready; }
    void resize(std::uint32_t w, std::uint32_t h) override { width_ = w; height_ = h; }
    rhi::IGraphicsCommandEncoder& encoder() override { return commands; }
    rhi::TextureHandle currentTexture() const override { return {99, 1}; }
    rhi::TextureViewHandle currentTextureView() const override { return {99, 1}; }
    rhi::ResourceState currentTextureState() const override { return rhi::ResourceState::Undefined; }
    rhi::TextureFormat format() const override { return rhi::TextureFormat::Bgra8Srgb; }
    std::uint32_t width() const override { return width_; }
    std::uint32_t height() const override { return height_; }
    std::uint32_t frameIndex() const override { return slot; }
    std::uint32_t imageCount() const override { return 3; }
    MockEncoder commands;
    std::uint32_t width_{800}, height_{600}, slot{}, begins{};
    rhi::FrameStatus status{rhi::FrameStatus::Ready};
};

class ForwardOnlyPipeline final : public IRenderPipeline {
public:
    bool render(RenderContext& context) override {
        ++calls;
        DrawList list;
        list.clearColor = {0.2F, 0.3F, 0.4F, 1.0F};
        RenderGraph graph;
        ForwardPass pass;
        pass.execute(context, graph, list);
        if (!graph.compile(context.rgTexturePool())) {
            return false;
        }
        graph.execute(context.encoder());
        backbufferWritten = context.backBufferWritten();
        return true;
    }
    int calls{};
    bool backbufferWritten{};
};

class SceneOutputTest : public ::testing::Test {
protected:
    void SetUp() override {
        window = std::make_unique<Window>(800, 600, "SceneOutput GoogleTest");
        ShowWindow(window->nativeHandle(), SW_HIDE);
        (void)window->consumeResize();
        auto deviceOwner = std::make_unique<MockDevice>();
        device = deviceOwner.get();
        auto swapchainOwner = std::make_unique<FakeSwapchain>();
        swapchain = swapchainOwner.get();
        renderer = std::make_unique<Renderer>(*window,
            rhi::Context{std::move(deviceOwner), std::move(swapchainOwner)});
        auto pipelineOwner = std::make_unique<ForwardOnlyPipeline>();
        pipeline = pipelineOwner.get();
        renderer->setPipeline(std::move(pipelineOwner));
    }
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    MockDevice* device{};
    FakeSwapchain* swapchain{};
    ForwardOnlyPipeline* pipeline{};
    RenderScene scene;
};
}

TEST_F(SceneOutputTest, DefaultRuntimeKeepsSwapchainExtentAndPresentFallback) {
    EXPECT_FALSE(renderer->offscreenScene());
    EXPECT_EQ(renderer->sceneWidth(), 800U);
    EXPECT_EQ(renderer->sceneHeight(), 600U);
    EXPECT_FLOAT_EQ(renderer->sceneAspectRatio(), 800.0F / 600.0F);
    renderer->renderFrame(scene);
    ASSERT_EQ(swapchain->commands.renderings.size(), 1U);
    EXPECT_EQ(swapchain->commands.renderings[0].colorAttachments[0].view.index, 99U);
    EXPECT_EQ(swapchain->commands.barriers.back().after, rhi::ResourceState::Present);
    EXPECT_EQ(renderer->currentForwardTarget().colorAttachmentCount(), 0U);
}

TEST_F(SceneOutputTest, EmptySceneClearsSampledImageAndFinishesInShaderRead) {
    renderer->setSceneViewport(320, 180);
    EXPECT_FLOAT_EQ(renderer->sceneAspectRatio(), 320.0F / 180.0F);
    renderer->renderFrame(scene);
    ASSERT_EQ(device->textures.size(), 4U); // two original depth images plus color/depth for slot 0
    EXPECT_EQ(device->textures[2].width, 320U);
    EXPECT_EQ(device->textures[2].height, 180U);
    EXPECT_EQ(device->textures[2].format, rhi::TextureFormat::Bgra8Srgb);
    EXPECT_TRUE(rhi::hasFlag(device->textures[2].usage, rhi::TextureUsage::Sampled));
    EXPECT_TRUE(rhi::hasFlag(device->textures[2].usage, rhi::TextureUsage::ColorAttachment));
    ASSERT_EQ(swapchain->commands.renderings.size(), 2U); // scene and present fallback
    const auto& output = swapchain->commands.renderings[0];
    EXPECT_EQ(output.renderArea.width, 320U);
    EXPECT_EQ(output.colorAttachments[0].loadOp, rhi::LoadOp::Clear);
    EXPECT_FLOAT_EQ(output.colorAttachments[0].clearColor.x, 0.2F);
    EXPECT_FALSE(pipeline->backbufferWritten);
    bool shaderRead = false;
    for (const auto& barrier : swapchain->commands.barriers)
        shaderRead |= barrier.texture.index == 3 && barrier.after == rhi::ResourceState::ShaderRead;
    EXPECT_TRUE(shaderRead);
}

TEST_F(SceneOutputTest, ResizeRetiresOnlyTheAcquiredFrameSlotAfterFenceReuse) {
    renderer->setSceneViewport(320, 180);
    renderer->renderFrame(scene);
    renderer->renderFrame(scene);
    EXPECT_EQ(device->textures.size(), 6U);
    const int destroyedBefore = device->destroyedTextures;
    renderer->renderFrame(scene);
    EXPECT_EQ(device->textures.size(), 6U);
    EXPECT_EQ(device->destroyedTextures, destroyedBefore + 1); // slot 0 initial depth
    renderer->setSceneViewport(640, 360);
    renderer->renderFrame(scene); // slot 1, slot 0 remains untouched
    EXPECT_EQ(device->textures.size(), 8U);
    EXPECT_EQ(device->destroyedTextures, destroyedBefore + 2);
    EXPECT_EQ(renderer->currentForwardTarget().width(), 320U); // next slot 0
    renderer->renderFrame(scene);
    EXPECT_EQ(device->textures.size(), 10U);
    EXPECT_EQ(device->destroyedTextures, destroyedBefore + 2);
    renderer->renderFrame(scene); // slot 1's old target is now fence-safe
    EXPECT_EQ(device->destroyedTextures, destroyedBefore + 4);
    renderer->renderFrame(scene); // slot 0's old target is now fence-safe
    EXPECT_EQ(device->destroyedTextures, destroyedBefore + 6);
}

TEST_F(SceneOutputTest, HiddenViewportSkipsSceneWhileFramesStillPresentAndCanResume) {
    renderer->setSceneViewport(0, 0);
    renderer->renderFrame(scene);
    EXPECT_EQ(pipeline->calls, 0);
    EXPECT_EQ(renderer->frameSerial(), 1U);
    EXPECT_EQ(device->textures.size(), 2U);
    EXPECT_FLOAT_EQ(renderer->sceneAspectRatio(), 1.0F);
    EXPECT_EQ(swapchain->commands.barriers.back().after, rhi::ResourceState::Present);
    renderer->setSceneViewport(300, 200);
    renderer->renderFrame(scene);
    EXPECT_EQ(pipeline->calls, 1);
    renderer->resetSceneViewport();
    renderer->renderFrame(scene);
    EXPECT_FALSE(renderer->offscreenScene());
    EXPECT_EQ(renderer->sceneWidth(), 800U);
}

TEST_F(SceneOutputTest, OutOfDateAcquireDoesNotAllocateOrRenderScene) {
    renderer->setSceneViewport(300, 200);
    swapchain->status = rhi::FrameStatus::OutOfDate;
    renderer->renderFrame(scene);
    EXPECT_EQ(device->textures.size(), 2U);
    EXPECT_EQ(pipeline->calls, 0);
    EXPECT_EQ(renderer->frameSerial(), 0U);
}

TEST_F(SceneOutputTest, MissingCameraProducesNoDrawItemsInOffscreenMode) {
    renderer->setSceneViewport(300, 200);
    scene.submit(RenderObject{});
    RenderContext context(*renderer, scene);
    const DrawList list = DrawListBuilder{}.build(scene, context);
    EXPECT_TRUE(list.groups.empty());
    EXPECT_TRUE(list.objects.empty());
}

TEST_F(SceneOutputTest, CameraTargetOverridesViewportWithoutTouchingFrameTargets) {
    RenderTargetDesc desc;
    desc.width = 256;
    desc.height = 144;
    desc.colorAttachments.push_back({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .additionalUsage = rhi::TextureUsage::Sampled,
    });
    desc.depthAttachment.emplace();
    desc.debugName = "CameraTarget";
    const RenderTargetHandle target = renderer->renderTargetPool().acquire(std::move(desc));
    ASSERT_TRUE(target);

    RenderCamera camera;
    camera.target = target;
    scene.setCamera(camera);
    renderer->renderFrame(scene);

    ASSERT_FALSE(swapchain->commands.renderings.empty());
    EXPECT_EQ(swapchain->commands.renderings.front().renderArea.width, 256U);
    EXPECT_EQ(swapchain->commands.renderings.front().renderArea.height, 144U);
    ASSERT_NE(renderer->renderTarget(target), nullptr);
    EXPECT_EQ(renderer->renderTarget(target)->colorFormat(0), rhi::TextureFormat::Rgba8Unorm);
    EXPECT_EQ(renderer->currentForwardTarget().width(), 800U);
}

// Real-device coverage for the RID-only RHI: the per-type handle pools (create/destroy,
// sampler/view dedup) and the command free-function surface that dispatches through the device
// singleton. These replace the old mock-encoder tests: with commands defined only in the Vulkan
// backend, recording can only be exercised against a live device, so the fixture probes for a
// Vulkan 1.3 runtime (and the validation layer in debug) and skips gracefully when absent.
#include "rhi/api/Command.h"
#include "rhi/api/Device.h"
#include "render/texture/Texture.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanSwapchain.h"
#include "render/render_graph/RenderGraph.h"
#include "render/pipeline/RenderContext.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <numeric>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <vector>

namespace {

using namespace engine;

bool vulkanRuntimeAvailable() {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &application;
    VkInstance probe{VK_NULL_HANDLE};
    if (vkCreateInstance(&info, nullptr, &probe) != VK_SUCCESS)
        return false;
    vkDestroyInstance(probe, nullptr);
#if defined(MINI_DEBUG)
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
#else
    return true;
#endif
}

class HiddenWindow final {
public:
    HiddenWindow() {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kClassName;
        RegisterClassW(&windowClass);
        handle_ = CreateWindowExW(0, kClassName, kClassName, WS_OVERLAPPED, 0, 0, 64, 64,
                                  nullptr, nullptr, instance_, nullptr);
    }
    ~HiddenWindow() {
        if (handle_)
            DestroyWindow(handle_);
        UnregisterClassW(kClassName, instance_);
    }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HINSTANCE instance() const { return instance_; }
    [[nodiscard]] HWND handle() const { return handle_; }

private:
    static constexpr const wchar_t* kClassName = L"MiniRhiDeviceTest";
    HINSTANCE instance_{};
    HWND handle_{};
};

class RhiDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!vulkanRuntimeAvailable()) {
            GTEST_SKIP() << "No Vulkan 1.3 runtime (or validation layer in debug builds)";
        }
        const rhi::SurfaceSource surface{.windowSystem = rhi::WindowSystem::Win32,
                                         .nativeDisplay = window_.instance(),
                                         .nativeWindow = window_.handle()};
        // Constructing the device registers it as the process-wide active device, which is how
        // the rhi:: command free functions resolve their command-buffer RID.
        device_ = std::make_unique<rhi::vulkan::VulkanDevice>(surface, /*enablePipelineCache=*/false);
    }
    void TearDown() override {
        if (device_)
            device_->waitIdle();
        device_.reset();
    }
    [[nodiscard]] rhi::vulkan::VulkanDevice& device() { return *device_; }

    HiddenWindow window_;
    std::unique_ptr<rhi::vulkan::VulkanDevice> device_;
};

TEST_F(RhiDeviceTest, RenderGraphImportsAcquiredSwapchainImageAndPresentsAfterResize) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderScene scene;
    RenderContext renderContext(pool, cache, scene);
    RenderGraph graph;
    const auto backBuffer = graph.importSwapchainTexture(swapchain);
    EXPECT_EQ(backBuffer, graph.importSwapchainTexture(swapchain));
    RgRasterPass pass;
    pass.name = "SwapchainClear";
    pass.renderAreaResolver = [&](const RenderContext&) {
        return rhi::Rect{0, 0, swapchain.width(), swapchain.height()};
    };
    pass.colorAttachments.push_back({backBuffer, rhi::LoadOp::Clear, rhi::StoreOp::Store});
    pass.execute = [&](const RgExecuteContext& execution) {
        EXPECT_EQ(execution.commandBuffer, swapchain.commandBuffer());
        EXPECT_TRUE(execution.renderPass);
        EXPECT_EQ(execution.subpass, 0U);
        EXPECT_EQ(&execution.frame, &renderContext);
        EXPECT_EQ(execution.resources.textureView(backBuffer), swapchain.currentTextureView());
        EXPECT_EQ(execution.renderArea.width, swapchain.width());
    };
    graph.addRasterGroup("Present", {std::move(pass)});
    bool sawPresentedImage = false;
    rhi::RID renderPass;
    for (int round = 0; round < 2; ++round) {
        if (round != 0) {
            d.waitIdle();
            cache.clearFramebuffers();
            swapchain.resize(48, 48);
        }
        for (std::uint32_t iteration = 0; iteration < 2 * swapchain.imageCount() + 1; ++iteration) {
            ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
            pool.beginFrame(swapchain.frameIndex());
            const auto texture = swapchain.currentTexture();
            const auto view = swapchain.currentTextureView();
            const auto before = swapchain.currentTextureState();
            sawPresentedImage |= before == rhi::ResourceState::Present;
            if (!graph.compiled()) graph.compile(renderContext);
            EXPECT_TRUE(graph.resourcesCompatible(renderContext));
            auto frame = graph.prepareFrame(renderContext);
            EXPECT_EQ(frame.textureView(backBuffer), view);
            const auto& begin = frame.groups()[0].beginInfo;
            if (renderPass) EXPECT_EQ(begin.renderPass, renderPass);
            else renderPass = begin.renderPass;
            EXPECT_EQ(begin.renderArea.width, swapchain.width());
            const auto& barriers = frame.groups()[0].preBarriers;
            ASSERT_EQ(barriers.size(), 1U);
            EXPECT_EQ(barriers[0].texture, texture);
            EXPECT_EQ(barriers[0].aspect, rhi::TextureAspect::Color);
            EXPECT_EQ(barriers[0].before, before);
            EXPECT_EQ(barriers[0].after, rhi::ResourceState::ColorAttachment);
            ASSERT_EQ(frame.finalBarriers().size(), 1U);
            EXPECT_EQ(frame.finalBarriers()[0].texture, texture);
            EXPECT_EQ(frame.finalBarriers()[0].after, rhi::ResourceState::Present);
            graph.execute(renderContext, frame, swapchain.commandBuffer());
            ASSERT_EQ(swapchain.endFrame(), rhi::FrameStatus::Ready);
        }
    }
    d.waitIdle();
    EXPECT_EQ(graph.compileCount(), 1U);
    EXPECT_EQ(cache.renderPassCount(), 1U);
    EXPECT_TRUE(sawPresentedImage);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

TEST_F(RhiDeviceTest, ResourceLifecycleAndDedup) {
    auto& d = device();

    const rhi::RID buffer = d.buffer_create({.size = 256,
                                             .usage = rhi::BufferUsage::Vertex,
                                             .memoryUsage = rhi::MemoryUsage::DeviceLocal,
                                             .debugName = "lifecycle-buffer"});
    EXPECT_TRUE(buffer);

    const rhi::TextureDesc textureDesc{.format = rhi::PixelFormat::Rgba8Unorm,
                                       .width = 4,
                                       .height = 4,
                                       .usage = rhi::TextureUsage::Sampled,
                                       .debugName = "lifecycle-texture"};
    const rhi::RID texture = d.texture_create(textureDesc);
    ASSERT_TRUE(texture);
    const rhi::RID viewA = d.texture_default_view(texture);
    const rhi::RID viewB = d.texture_default_view(texture);
    EXPECT_TRUE(viewA);
    EXPECT_EQ(viewA, viewB); // device-level view dedup

    const rhi::SamplerDesc samplerDesc{};
    const rhi::RID samplerA = d.sampler_create(samplerDesc);
    const rhi::RID samplerB = d.sampler_create(samplerDesc);
    EXPECT_TRUE(samplerA);
    EXPECT_EQ(samplerA, samplerB); // identical descriptors share one handle
    rhi::SamplerDesc pointDesc = samplerDesc;
    pointDesc.minFilter = rhi::SamplerFilter::Nearest;
    pointDesc.magFilter = rhi::SamplerFilter::Nearest;
    EXPECT_NE(samplerA, d.sampler_create(pointDesc));

    const rhi::RID command = d.command_buffer_create();
    EXPECT_TRUE(command);
    d.command_buffer_destroy(command);

    d.buffer_destroy(buffer);
    d.texture_destroy(texture); // cascades the deduped default view
}

TEST_F(RhiDeviceTest, CommandRecordAndSubmit) {
    auto& d = device();

    const rhi::TextureDesc textureDesc{.format = rhi::PixelFormat::Rgba8Unorm,
                                       .width = 16,
                                       .height = 16,
                                       .usage = rhi::TextureUsage::ColorAttachment,
                                       .debugName = "record-target"};
    const rhi::RID texture = d.texture_create(textureDesc);
    ASSERT_TRUE(texture);
    const rhi::RID view = d.texture_default_view(texture);
    ASSERT_TRUE(view);

    const rhi::RID cmd = d.command_buffer_create();
    rhi::begin(cmd);
    const rhi::TextureBarrier toAttachment{.texture = texture,
                                           .aspect = rhi::TextureAspect::Color,
                                           .before = rhi::ResourceState::Undefined,
                                           .after = rhi::ResourceState::ColorAttachment};
    rhi::resourceBarriers(cmd, std::span{&toAttachment, 1});
    rhi::beginRendering(cmd,
                        {.renderArea = {.width = 16, .height = 16},
                         .colorAttachments = {{.view = view,
                                               .loadOp = rhi::LoadOp::Clear,
                                               .clearColor = {0.0F, 0.0F, 0.0F, 1.0F}}}});
    rhi::endRendering(cmd);
    rhi::end(cmd);
    d.submit(cmd, rhi::vulkan::SubmitSync{});
    d.waitIdle();

    d.command_buffer_destroy(cmd);
    d.texture_destroy(texture);
    // Reaching this point means recording, submission and teardown ran without a fatal error or
    // a validation-layer abort (the debug messenger fatals on validation errors).
    SUCCEED();
}

std::vector<std::byte> graphShaderBytes(const char* name) {
    const auto path = std::filesystem::path{MINI_TEST_RENDER_GRAPH_SHADER_DIR} / name;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const auto size = stream.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

void configureGraphDraw(rhi::RID cmd, rhi::RID pipeline) {
    rhi::bindPipeline(cmd, pipeline);
    rhi::setViewport(cmd, {0.0F, 0.0F, 16.0F, 16.0F, 0.0F, 1.0F});
    rhi::setScissor(cmd, {0, 0, 16, 16});
    rhi::setCullMode(cmd, rhi::CullMode::None);
    rhi::setFrontFace(cmd, rhi::FrontFace::CounterClockwise);
    rhi::setDepthTestEnable(cmd, false);
    rhi::setDepthWriteEnable(cmd, false);
    rhi::setDepthCompareOp(cmd, rhi::CompareOp::Always);
    rhi::setBlendState(cmd, rhi::BlendMode::Off);
    rhi::setColorWriteMask(cmd, rhi::ColorWriteMask::All);
    rhi::setPrimitiveTopology(cmd, rhi::PrimitiveTopology::TriangleList);
    rhi::setFillMode(cmd, rhi::FillMode::Solid);
}

TEST_F(RhiDeviceTest, RenderGraphSubpassesReadInputAndReuseCaches) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    const auto vertexBytes = graphShaderBytes("render_graph_fullscreen.vert.spv");
    const auto gbufferBytes = graphShaderBytes("render_graph_gbuffer.frag.spv");
    const auto lightingBytes = graphShaderBytes("render_graph_lighting.frag.spv");
    ASSERT_FALSE(vertexBytes.empty());
    ASSERT_FALSE(gbufferBytes.empty());
    ASSERT_FALSE(lightingBytes.empty());
    const auto vertex = d.shader_create({rhi::ShaderStage::Vertex, vertexBytes, "graph-vertex"});
    const auto gbufferShader = d.shader_create({rhi::ShaderStage::Fragment, gbufferBytes, "graph-gbuffer"});
    const auto lightingShader = d.shader_create({rhi::ShaderStage::Fragment, lightingBytes, "graph-lighting"});
    const auto emptyLayout = d.pipeline_layout_create({{}, "graph-empty-layout"});
    const rhi::ResourceBindingDesc inputBinding{0, "gbuffer", rhi::ResourceType::InputAttachment};
    const auto inputSetLayout = d.descriptor_set_layout_allocate_rid();
    d.descriptor_set_layout_build(inputSetLayout, {std::span{&inputBinding, 1}, "graph-input-layout"});
    const auto inputLayout = d.pipeline_layout_create({std::span{&inputSetLayout, 1}, "graph-lighting-layout"});
    const auto inputSet = d.descriptor_set_allocate({inputSetLayout, 0x5247494E505554ULL,
                                                    std::span{&inputBinding, 1}, 1});
    const auto output = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm, .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSource,
        .debugName = "graph-output"});
    const auto outputView = d.texture_default_view(output);
    rhi::ResourceState outputState = rhi::ResourceState::Undefined;
    RgTexturePool pool;
    RgRenderPassCache cache;
    pool.beginFrame(0);
    RenderScene scene;
    RenderContext renderContext(pool, cache, scene);
    RenderGraph graph;
    rhi::RID pipelines[2];
    rhi::RID cachedPass, cachedFramebuffer;
    std::vector<std::uint32_t> callbackOrder;
    const auto gbuffer = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment,
        .debugName = "graph-gbuffer"});
    const auto destination = graph.importTexture(output, outputView, outputState,
        rhi::ResourceState::CopySource, rhi::TextureAspect::Color, &outputState);
    const auto normal = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
        .debugName = "graph-normal"});
    RgRasterPass first;
    first.name = "GBuffer";
    first.renderArea = {0, 0, 16, 16};
    first.colorAttachments.push_back({gbuffer, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
    first.colorAttachments.push_back({normal, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
    first.execute = [&](const RgExecuteContext& context) {
        EXPECT_EQ(context.renderPass, cachedPass);
        callbackOrder.push_back(context.subpass);
        configureGraphDraw(context.commandBuffer, pipelines[0]);
        rhi::draw(context.commandBuffer, {3});
    };
    RgRasterPass second;
    second.name = "Lighting";
    second.renderArea = first.renderArea;
    second.colorAttachments.push_back({destination, rhi::LoadOp::Clear, rhi::StoreOp::Store});
    second.inputAttachments.push_back(gbuffer);
    second.execute = [&](const RgExecuteContext& context) {
        EXPECT_EQ(context.renderPass, cachedPass);
        callbackOrder.push_back(context.subpass);
        configureGraphDraw(context.commandBuffer, pipelines[1]);
        rhi::bindDescriptorSet(context.commandBuffer, 0, inputSet);
        rhi::draw(context.commandBuffer, {3});
    };
    graph.addRasterGroup("Deferred", {std::move(first), std::move(second)});
    graph.compile(renderContext);
    const auto record = [&] {
        auto frame = graph.prepareFrame(renderContext);
        const auto& group = frame.groups()[0];
        if (cachedPass) {
            EXPECT_EQ(group.beginInfo.renderPass, cachedPass);
            EXPECT_EQ(group.beginInfo.framebuffer, cachedFramebuffer);
        } else {
            cachedPass = group.beginInfo.renderPass;
            cachedFramebuffer = group.beginInfo.framebuffer;
            rhi::GraphicsPipelineDesc desc;
            desc.vertexShader = vertex;
            desc.fragmentShader = gbufferShader;
            desc.pipelineLayout = emptyLayout;
            desc.colorFormats = {rhi::PixelFormat::Rgba8Unorm, rhi::PixelFormat::Rgba8Unorm};
            desc.renderPass = cachedPass;
            pipelines[0] = d.pipeline_create(desc);
            desc.fragmentShader = lightingShader;
            desc.pipelineLayout = inputLayout;
            desc.subpass = 1;
            desc.colorFormats.resize(1);
            pipelines[1] = d.pipeline_create(desc);
            EXPECT_NE(pipelines[0], pipelines[1]);
            EXPECT_EQ(d.resolvePipeline(pipelines[1]).subpass, 1U);
        }
        const rhi::DescriptorWrite write{.binding = 0, .type = rhi::ResourceType::InputAttachment,
                                          .textureView = frame.textureView(gbuffer)};
        d.descriptor_set_update(inputSet, std::span{&write, 1});
        return frame;
    };
    VkBuffer readback{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocationInfo{};
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = 16 * 16 * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_AUTO;
    memoryInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ASSERT_EQ(vmaCreateBuffer(d.allocator(), &bufferInfo, &memoryInfo, &readback, &allocation, &allocationInfo), VK_SUCCESS);
    const auto cmd = d.command_buffer_create();
    for (int iteration = 0; iteration < 2; ++iteration) {
        auto frame = record();
        rhi::begin(cmd);
        graph.execute(renderContext, frame, cmd);
        EXPECT_EQ(outputState, rhi::ResourceState::CopySource);
        const auto native = d.command_buffer(cmd).nativeCommandBuffer();
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {16, 16, 1};
        vkCmdCopyImageToBuffer(native, d.resolveTexture(output), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback, 1, &copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback;
        host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                              0, 0, nullptr, 1, &host, 0, nullptr);
        rhi::end(cmd);
        d.submit(cmd, {});
        d.waitIdle();
        ASSERT_EQ(vmaInvalidateAllocation(d.allocator(), allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
        const auto* pixel = static_cast<const std::uint8_t*>(allocationInfo.pMappedData);
        for (std::size_t i = 0; i < 16 * 16; ++i) {
            EXPECT_NEAR(pixel[i * 4], 191, 1);
            EXPECT_NEAR(pixel[i * 4 + 1], 128, 1);
            EXPECT_NEAR(pixel[i * 4 + 2], 64, 1);
            EXPECT_EQ(pixel[i * 4 + 3], 255);
        }
        EXPECT_EQ(graph.compileCount(), 1U);
    }
    EXPECT_EQ(callbackOrder, (std::vector<std::uint32_t>{0, 1, 0, 1}));
    EXPECT_EQ(pool.totalEntryCount(), 2U);
    d.command_buffer_destroy(cmd);
    vmaDestroyBuffer(d.allocator(), readback, allocation);
    for (const auto pipeline : pipelines) d.pipeline_destroy(pipeline);
    d.descriptor_set_destroy(inputSet);
    d.pipeline_layout_destroy(inputLayout);
    d.pipeline_layout_destroy(emptyLayout);
    d.descriptor_set_layout_destroy(inputSetLayout);
    d.shader_destroy(vertex);
    d.shader_destroy(gbufferShader);
    d.shader_destroy(lightingShader);
    cache.invalidateFramebuffers(outputView);
    EXPECT_EQ(cache.framebufferCount(), 0U);
    EXPECT_EQ(cache.renderPassCount(), 1U);
    d.texture_view_destroy(outputView);
    d.texture_destroy(output);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

TEST_F(RhiDeviceTest, RenderGraphDepthReadOnlyAndInputStoresAreSynchronized) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderScene scene;
    RenderContext renderContext(pool, cache, scene);
    RenderGraph graph;
    const auto depth = graph.createTexture({.format = rhi::PixelFormat::Depth32Float,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::InputAttachment |
                 rhi::TextureUsage::Sampled,
        .debugName = "graph-depth-input"});
    const auto output = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
        .debugName = "graph-depth-output"});
    RgRasterPass clear;
    clear.name = "ClearDepth";
    clear.renderArea = {0, 0, 16, 16};
    clear.depthAttachments.push_back({depth, rhi::LoadOp::Clear, rhi::StoreOp::Store, 0.5F});
    clear.execute = [](const RgExecuteContext&) {};
    RgRasterPass read = clear;
    read.name = "ReadOnlyDepth";
    read.depthAttachments[0].loadOp = rhi::LoadOp::Load;
    read.depthAttachments[0].access = rhi::AccessMode::Read;
    RgRasterPass input;
    input.name = "DepthAsInput";
    input.renderArea = clear.renderArea;
    input.colorAttachments.push_back({output, rhi::LoadOp::Clear, rhi::StoreOp::Store});
    input.inputAttachments = {depth};
    input.execute = [](const RgExecuteContext&) {};
    graph.addRasterGroup("DepthGroup", {clear, read, input});
    graph.addGraphicsPass({
        .name = "SampleDepth",
        .renderArea = {0, 0, 16, 16},
        .colorAttachments = {{output, rhi::LoadOp::Clear, rhi::StoreOp::Store}},
        .sampledTextures = {depth},
        .execute = [](const RgExecuteContext&) {},
    });
    graph.compile(renderContext);
    auto frame = graph.prepareFrame(renderContext);
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    graph.execute(renderContext, frame, cmd);
    rhi::end(cmd);
    d.submit(cmd, {});
    d.waitIdle();
    frame.reset();
    graph.reset();
    d.command_buffer_destroy(cmd);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

TEST_F(RhiDeviceTest, RenderObjectCreationDoesNotCacheAtDeviceLevel) {
    auto& d = device();
    rhi::RenderPassDesc desc;
    desc.attachments.push_back({rhi::PixelFormat::Rgba8Unorm, rhi::SampleCount::One,
        rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ResourceState::ColorAttachment,
        rhi::ResourceState::ColorAttachment});
    rhi::SubpassDesc subpass;
    subpass.colors.push_back({0, rhi::ResourceState::ColorAttachment});
    desc.subpasses.push_back(subpass);
    const auto firstPass = d.render_pass_create(desc);
    const auto secondPass = d.render_pass_create(desc);
    EXPECT_NE(firstPass, secondPass);
    EXPECT_NE(d.renderPass(firstPass).handle(), d.renderPass(secondPass).handle());
    const auto texture = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
        .debugName = "raw-framebuffer-target"});
    const auto view = d.texture_default_view(texture);
    const rhi::FramebufferDesc framebufferDesc{firstPass, {view}, 16, 16, 1};
    const auto firstFramebuffer = d.framebuffer_create(framebufferDesc);
    const auto secondFramebuffer = d.framebuffer_create(framebufferDesc);
    EXPECT_NE(firstFramebuffer, secondFramebuffer);
    EXPECT_NE(d.framebuffer(firstFramebuffer).handle(), d.framebuffer(secondFramebuffer).handle());
    d.framebuffer_destroy(firstFramebuffer);
    d.framebuffer_destroy(secondFramebuffer);
    d.render_pass_destroy(firstPass);
    d.render_pass_destroy(secondPass);
    d.texture_destroy(texture);
}

class ExtendedTextureFormatTest : public RhiDeviceTest,
                                  public ::testing::WithParamInterface<TextureFormat> {};

TEST_P(ExtendedTextureFormatTest, CreatesDefaultViewAndClearsNativeAttachment) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    const auto format = GetParam();
    const auto nativeFormat = rhi::vulkan::toVulkan(toRhi(format));
    const bool depth = rhi::isDepthFormat(toRhi(format));
    const auto usage = (depth ? rhi::TextureUsage::DepthStencilAttachment :
                               rhi::TextureUsage::ColorAttachment) | rhi::TextureUsage::Sampled;
    VkImageFormatProperties support{};
    if (vkGetPhysicalDeviceImageFormatProperties(d.physicalDevice(), nativeFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, rhi::vulkan::toVulkan(usage), 0, &support) != VK_SUCCESS)
        GTEST_SKIP() << "Device does not support this format/usage combination";
    auto texture = depth ? Texture::createShadowMap(16, 16, format) : Texture::createColor(16, 16, format);
    ASSERT_TRUE(texture);
    const auto view = d.texture_view_info(texture->defaultView());
    EXPECT_EQ(view.texture, texture->textureHandle());
    EXPECT_EQ(view.format, toRhi(format));
    EXPECT_EQ(view.width, 16U);
    EXPECT_EQ(view.height, 16U);
    EXPECT_EQ(view.mipCount, 1U);
    EXPECT_EQ(view.layerCount, 1U);
    EXPECT_EQ(view.usage, usage);
    const auto cmd = d.command_buffer_create();
    const auto state = depth ? rhi::ResourceState::DepthAttachment : rhi::ResourceState::ColorAttachment;
    const auto aspect = depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    rhi::begin(cmd);
    const rhi::TextureBarrier toAttachment{texture->textureHandle(), aspect,
        rhi::ResourceState::Undefined, state};
    rhi::resourceBarriers(cmd, std::span{&toAttachment, 1});
    rhi::RenderingInfo rendering{.renderArea = {0, 0, 16, 16}};
    if (depth) rendering.depthAttachments.push_back({texture->defaultView(), rhi::LoadOp::Clear,
                                                   rhi::StoreOp::Store, 0.5F});
    else rendering.colorAttachments.push_back({texture->defaultView(), rhi::LoadOp::Clear,
                                               rhi::StoreOp::Store, math::Vec4{0.25F}});
    rhi::beginRendering(cmd, rendering);
    rhi::endRendering(cmd);
    const rhi::TextureBarrier toRead{texture->textureHandle(), aspect, state, rhi::ResourceState::ShaderRead};
    rhi::resourceBarriers(cmd, std::span{&toRead, 1});
    rhi::end(cmd);
    d.submit(cmd, {}); d.waitIdle();
    d.command_buffer_destroy(cmd);
    texture.reset();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

INSTANTIATE_TEST_SUITE_P(NonCompressedFormats, ExtendedTextureFormatTest,
    ::testing::Values(TextureFormat::R8Unorm, TextureFormat::Rg8Unorm,
        TextureFormat::R16Float, TextureFormat::Rg16Float, TextureFormat::Rgba16Float,
        TextureFormat::R32Float, TextureFormat::Rg32Float, TextureFormat::Rgba32Float,
        TextureFormat::Depth16Unorm));

TEST_F(RhiDeviceTest, TextureUploadRejectsMismatchedFormatByteCount) {
    auto& d = device();
    VkImageFormatProperties support{};
    if (vkGetPhysicalDeviceImageFormatProperties(d.physicalDevice(), VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &support) != VK_SUCCESS)
        GTEST_SKIP() << "Device does not support this format/usage combination";
    EXPECT_EXIT({
        const auto image = d.texture_create({.format = rhi::PixelFormat::Rgba16Float,
            .width = 4, .height = 4,
            .usage = rhi::TextureUsage::TransferDestination | rhi::TextureUsage::Sampled});
        const std::vector<std::byte> rgbaBytes(4U * 4U * 4U);
        rhi::TextureUploadRegion upload;
        upload.width = upload.height = 4;
        upload.data = rgbaBytes;
        d.texture_upload(image, std::span{&upload, 1});
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Texture upload region does not match the mip level");
}


// Upload bytes are packed on the CPU, but each GPU buffer copy has an aligned offset.
class TextureUploadFormatTest : public RhiDeviceTest,
                                public ::testing::WithParamInterface<TextureFormat> {};

TEST_P(TextureUploadFormatTest, OddMipUploadAndSubregionUpdateReadBackExactly) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    const auto format = GetParam();
    const auto pixelFormat = toRhi(format);
    const auto info = rhi::formatInfo(pixelFormat);
    const auto usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination |
                       rhi::TextureUsage::TransferSource;
    VkImageFormatProperties support{};
    if (vkGetPhysicalDeviceImageFormatProperties(d.physicalDevice(), rhi::vulkan::toVulkan(pixelFormat),
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, rhi::vulkan::toVulkan(usage), 0, &support) != VK_SUCCESS)
        GTEST_SKIP() << "Device does not support this format/usage combination";
    TextureDesc desc{TextureType::Texture2D, format, 5, 3, 3};
    desc.usage = usage;
    TextureMipLayout packed, padded;
    ASSERT_TRUE(computeTextureLayout(desc, packed));
    ASSERT_TRUE(rhi::computeTextureLayout(pixelFormat, 5, 3, 3, padded,
        std::lcm(std::size_t{4}, static_cast<std::size_t>(info.bytesPerBlock))));
    std::vector<std::byte> pixels(packed.totalSize);
    // Finite, exactly representable float and depth values survive a raw transfer.
    if (info.numericType == rhi::FormatNumericType::Float) {
        const auto componentBytes = info.bytesPerBlock / info.channelCount;
        for (std::size_t offset = 0; offset < pixels.size(); offset += componentBytes) {
            if (componentBytes == 2) {
                const std::uint16_t value = static_cast<std::uint16_t>(0x3400U + (offset % 4U) * 0x200U);
                std::memcpy(pixels.data() + offset, &value, 2);
            } else {
                const float value = 0.25F * static_cast<float>(1 + (offset / 4U) % 4U);
                std::memcpy(pixels.data() + offset, &value, 4);
            }
        }
    } else {
        for (std::size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::byte>((i * 37U + 19U) % 256U);
    }
    auto texture = Texture::create(desc);
    ASSERT_TRUE(texture);
    if (format == TextureFormat::R8Unorm) {
        // Also exercise arbitrary region order at the lower RHI boundary.
        std::vector<rhi::TextureUploadRegion> regions;
        for (auto mip : {2U, 0U, 1U}) {
            rhi::TextureUploadRegion region;
            region.mipLevel = mip;
            region.width = std::max(1U, 5U >> mip);
            region.height = std::max(1U, 3U >> mip);
            region.data = std::span<const std::byte>{pixels}.subspan(packed.offsets[mip], packed.sizes[mip]);
            regions.push_back(region);
        }
        d.texture_upload(texture->textureHandle(), regions);
    } else {
        texture->upload(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size()});
    }
    VkBuffer readback{};
    VmaAllocation allocation{};
    VmaAllocationInfo allocationInfo{};
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    const auto readbackSize = (padded.totalSize + 3U) / 4U * 4U;
    bufferInfo.size = readbackSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_AUTO;
    memoryInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ASSERT_EQ(vmaCreateBuffer(d.allocator(), &bufferInfo, &memoryInfo, &readback, &allocation, &allocationInfo), VK_SUCCESS);
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    const auto aspect = info.depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    rhi::TextureBarrier barrier{texture->textureHandle(), aspect, rhi::ResourceState::ShaderRead,
                                rhi::ResourceState::CopyDestination};
    barrier.mipCount = 3;
    rhi::resourceBarriers(cmd, std::span{&barrier, 1});
    // Replace the second texel of mip 1 with a valid texel from mip 0.
    const auto replacement = std::span<const std::byte>{pixels}.first(info.bytesPerBlock);
    rhi::updateImage(cmd, {.destination = texture->textureHandle(), .mipLevel = 1,
        .offset = {1, 0, 0}, .extent = {1, 1, 1}, .data = replacement});
    std::memcpy(pixels.data() + packed.offsets[1] + info.bytesPerBlock,
                replacement.data(), replacement.size());
    barrier.before = rhi::ResourceState::CopyDestination;
    barrier.after = rhi::ResourceState::CopySource;
    rhi::resourceBarriers(cmd, std::span{&barrier, 1});
    // Use the public RHI image-to-buffer helper, including its depth aspect inference.
    const auto destination = d.buffer_create({readbackSize, rhi::BufferUsage::TransferDestination |
        rhi::BufferUsage::TransferSource, rhi::MemoryUsage::Readback, "upload-roundtrip"});
    for (std::uint32_t mip = 0; mip < 3; ++mip) {
        rhi::BufferImageCopy copy{destination, texture->textureHandle(), padded.offsets[mip],
            0, 0, mip, 0, {}, {std::max(1U, 5U >> mip), std::max(1U, 3U >> mip), 1}};
        rhi::copyImageToBuffer(cmd, copy);
    }
    const auto native = d.command_buffer(cmd).nativeCommandBuffer();
    VkMemoryBarrier transfer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    transfer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &transfer, 0, nullptr, 0, nullptr);
    VkBufferCopy bufferCopy{0, 0, readbackSize};
    vkCmdCopyBuffer(native, d.resolveBuffer(destination), readback, 1, &bufferCopy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 0, nullptr);
    rhi::end(cmd);
    d.submit(cmd, {});
    d.waitIdle();
    ASSERT_EQ(vmaInvalidateAllocation(d.allocator(), allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
    const auto* actual = static_cast<const std::byte*>(allocationInfo.pMappedData);
    for (std::uint32_t mip = 0; mip < 3; ++mip) {
        EXPECT_EQ(std::memcmp(actual + padded.offsets[mip], pixels.data() + packed.offsets[mip],
                              packed.sizes[mip]), 0) << "mip " << mip;
    }
    d.command_buffer_destroy(cmd);
    d.buffer_destroy(destination);
    vmaDestroyBuffer(d.allocator(), readback, allocation);
    texture.reset();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

INSTANTIATE_TEST_SUITE_P(AllFormats, TextureUploadFormatTest,
    ::testing::Values(TextureFormat::Rgba8Unorm, TextureFormat::Rgba8Srgb,
        TextureFormat::Bgra8Unorm, TextureFormat::Bgra8Srgb, TextureFormat::Depth32Float,
        TextureFormat::R8Unorm, TextureFormat::Rg8Unorm, TextureFormat::R16Float,
        TextureFormat::Rg16Float, TextureFormat::Rgba16Float, TextureFormat::R32Float,
        TextureFormat::Rg32Float, TextureFormat::Rgba32Float, TextureFormat::Depth16Unorm));

TEST_F(RhiDeviceTest, ImageUpdateRejectsInvalidRegionsBeforeAllocatingStaging) {
    auto& d = device();
    const auto image = d.texture_create({.format = rhi::PixelFormat::R8Unorm,
        .width = 5, .height = 3, .mipCount = 3,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination});
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    const std::vector<std::byte> bytes(1);
    for (int scenario = 0; scenario < 8; ++scenario) {
        EXPECT_EXIT({
            rhi::ImageUpdate update;
            update.destination = image;
            update.extent.width = update.extent.height = update.extent.depth = 1;
            update.data = bytes;
            switch (scenario) {
            case 0: update.mipLevel = 3; break;
            case 1: update.arrayLayer = 1; break;
            case 2: update.offset.x = 5; break;
            case 3: update.offset.x = UINT32_MAX; break;
            case 4: update.extent.height = 4; break;
            case 5: update.extent.depth = 2; break;
            case 6: update.extent.width = 0; break;
            case 7: update.offset.z = 1; break;
            }
            rhi::updateImage(cmd, update);
        }, ::testing::ExitedWithCode(EXIT_FAILURE), "Invalid image update region");
    }
    EXPECT_EXIT({
        rhi::ImageUpdate update;
        update.destination = image;
        update.extent.width = update.extent.height = 2;
        update.extent.depth = 1;
        update.data = bytes;
        rhi::updateImage(cmd, update);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Image update data does not match the extent");
    d.command_buffer_destroy(cmd);
    d.texture_destroy(image);
}


TEST_F(RhiDeviceTest, TextureUploadRejectsIncompleteDuplicateAndMalformedMipRegions) {
    auto& d = device();
    const auto image = d.texture_create({.format = rhi::PixelFormat::R8Unorm,
        .width = 5, .height = 3, .mipCount = 3,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination});
    const std::vector<std::byte> bytes(18);
    std::vector<rhi::TextureUploadRegion> regions;
    for (std::uint32_t mip = 0; mip < 3; ++mip) {
        rhi::TextureUploadRegion region;
        region.mipLevel = mip;
        region.width = std::max(1U, 5U >> mip);
        region.height = std::max(1U, 3U >> mip);
        region.data = std::span<const std::byte>{bytes}.first(region.width * region.height);
        regions.push_back(region);
    }
    EXPECT_EXIT({
        d.texture_upload(image, std::span{regions}.first(2));
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "complete mip chain");
    for (int scenario = 0; scenario < 5; ++scenario) {
        const char* expectedError = scenario == 3 ? "Texture upload region does not match the mip level" :
                                                  "Invalid Texture upload region";
        EXPECT_EXIT({
            auto invalid = regions;
            switch (scenario) {
            case 0: invalid[2] = invalid[1]; break;
            case 1: invalid[2].mipLevel = UINT32_MAX; break;
            case 2: invalid[2].arrayLayer = 1; break;
            case 3: invalid[2].width = 2; break;
            case 4: invalid[2].data = {}; break;
            }
            d.texture_upload(image, invalid);
        }, ::testing::ExitedWithCode(EXIT_FAILURE), expectedError);
    }
    // Invalid attempts did not submit anything; the fresh image is still uploadable.
    d.texture_upload(image, regions);
    EXPECT_EXIT({
        d.texture_upload(image, regions);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Invalid Texture upload");
    d.texture_destroy(image);
}

} // namespace

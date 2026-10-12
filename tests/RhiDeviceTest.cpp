// Real-device coverage for the RID-only RHI: the per-type handle pools (create/destroy,
// sampler/view dedup) and the command free-function surface that dispatches through the device
// singleton. These replace the old mock-encoder tests: with commands defined only in the Vulkan
// backend, recording can only be exercised against a live device, so the fixture probes for a
// Vulkan 1.3 runtime (and the validation layer in debug) and skips gracefully when absent.
#include "rhi/api/Command.h"
#include "rhi/api/Device.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "render/render_graph/RenderGraph.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstring>
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
    RenderGraph graph;
    rhi::RID pipelines[2];
    rhi::RID cachedPass, cachedFramebuffer;
    std::vector<std::uint32_t> callbackOrder;
    const auto record = [&] {
        const auto gbuffer = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16,
            .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment,
            .debugName = "graph-gbuffer"});
        const auto destination = graph.importTexture({output, outputView, outputState,
            rhi::ResourceState::CopySource, rhi::TextureAspect::Color, &outputState});
        const auto normal = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
            .debugName = "graph-normal"});
        RgRasterPass first;
        first.name = "GBuffer";
        first.rendering.renderArea = {0, 0, 16, 16};
        first.rendering.colorAttachments.push_back({gbuffer, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
        first.rendering.colorAttachments.push_back({normal, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
        first.execute = [&](const RgExecuteContext& context) {
            EXPECT_EQ(context.renderPass, cachedPass);
            callbackOrder.push_back(context.subpass);
            configureGraphDraw(context.commandBuffer, pipelines[0]);
            rhi::draw(context.commandBuffer, {3});
        };
        RgRasterPass second;
        second.name = "Lighting";
        second.rendering.renderArea = first.rendering.renderArea;
        second.rendering.colorAttachments.push_back({destination, rhi::LoadOp::Clear, rhi::StoreOp::Store});
        second.rendering.inputAttachments.push_back(gbuffer);
        second.execute = [&](const RgExecuteContext& context) {
            EXPECT_EQ(context.renderPass, cachedPass);
            callbackOrder.push_back(context.subpass);
            configureGraphDraw(context.commandBuffer, pipelines[1]);
            rhi::bindDescriptorSet(context.commandBuffer, 0, inputSet);
            rhi::draw(context.commandBuffer, {3});
        };
        graph.addRasterGroup("Deferred", {std::move(first), std::move(second)});
        graph.compile(pool, cache);
        const auto& group = graph.compiledGroups()[0];
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
                                          .textureView = graph.resolvedTextureView(gbuffer)};
        d.descriptor_set_update(inputSet, std::span{&write, 1});
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
        record();
        rhi::begin(cmd);
        graph.execute(cmd);
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
        graph.reset();
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
    RenderGraph graph;
    const auto depth = graph.createTexture({.format = rhi::PixelFormat::Depth32Float,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::InputAttachment |
                 rhi::TextureUsage::Sampled,
        .aspect = rhi::TextureAspect::Depth, .debugName = "graph-depth-input"});
    const auto output = graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
        .debugName = "graph-depth-output"});
    RgRasterPass clear;
    clear.name = "ClearDepth";
    clear.rendering.renderArea = {0, 0, 16, 16};
    clear.rendering.depthAttachments.push_back({depth, rhi::LoadOp::Clear, rhi::StoreOp::Store, 0.5F});
    clear.execute = [](const RgExecuteContext&) {};
    RgRasterPass read = clear;
    read.name = "ReadOnlyDepth";
    read.rendering.depthAttachments[0].loadOp = rhi::LoadOp::Load;
    read.rendering.depthAttachments[0].access = rhi::AccessMode::Read;
    RgRasterPass input;
    input.name = "DepthAsInput";
    input.rendering.renderArea = clear.rendering.renderArea;
    input.rendering.colorAttachments.push_back({output, rhi::LoadOp::Clear, rhi::StoreOp::Store});
    input.rendering.inputAttachments = {depth};
    input.execute = [](const RgExecuteContext&) {};
    graph.addRasterGroup("DepthGroup", {clear, read, input});
    graph.addGraphicsPass("SampleDepth",
        RgRenderingInfo{{0, 0, 16, 16}, {{output, rhi::LoadOp::Clear, rhi::StoreOp::Store}}, {}, {}},
        {{depth, rhi::TextureAspect::Depth, rhi::ResourceState::ShaderRead}}, [](rhi::RID) {});
    graph.compile(pool, cache);
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    graph.execute(cmd);
    rhi::end(cmd);
    d.submit(cmd, {});
    d.waitIdle();
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

} // namespace

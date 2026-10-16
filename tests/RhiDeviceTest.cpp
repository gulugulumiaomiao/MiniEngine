#include "TestTextureDesc.h"
// Real-device coverage for the RID-only RHI: the per-type handle pools (create/destroy,
// sampler/view dedup) and the command free-function surface that dispatches through the device
// singleton. These replace the old mock-encoder tests: with commands defined only in the Vulkan
// backend, recording can only be exercised against a live device, so the fixture probes for a
// Vulkan 1.3 runtime (and the validation layer in debug) and skips gracefully when absent.
#include "rhi/api/Command.h"
#include "rhi/api/Device.h"
#include "render/texture/Texture.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "render/shader/ShaderGenerator.h"
#include "asset/format/ShaderAssetFormat.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
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
#include <tuple>
#include <atomic>
#include <cstdlib>
#include <new>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <vector>

// Fault injection is confined to this test executable and disabled outside its guard.
namespace {
std::atomic<std::size_t> failAllocationSize{};
std::atomic<bool> failAnyAllocation{};
class FailNextAllocation final {
public:
    explicit FailNextAllocation(std::size_t size = 0) {
        failAllocationSize = size;
        failAnyAllocation = size == 0;
    }
    ~FailNextAllocation() { failAllocationSize = 0; failAnyAllocation = false; }
};
}
void* operator new(std::size_t size) {
    auto expected = size;
    if (failAnyAllocation.exchange(false) ||
        (size && failAllocationSize.compare_exchange_strong(expected, 0))) {
        failAllocationSize = 0;
        failAnyAllocation = false;
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

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
            pool.collect(d.completedSubmission());
            pool.beginBatch(swapchain.commandBuffer());
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
            pool.onSubmitted(d.lastSubmitted());
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

TEST_F(RhiDeviceTest, GlobalPoolCancellationAndSubmissionCommitPreserveActualGpuState) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
    RgTexturePool pool; RgRenderPassCache cache; RenderScene scene;
    RenderContext context(pool, cache, scene);
    const auto output = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled});
    const auto outputView = d.texture_default_view(output);
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    RenderGraph graph;
    const TextureDesc transientDesc = test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16);
    const auto transient = graph.createTexture(transientDesc);
    const auto external = graph.importTexture("Output", [&](RenderContext&) {
        return RgTextureBinding{output, outputView, state, rhi::AccessMode::Auto, &state};
    }, rhi::ResourceState::ShaderRead);
    bool throwDuringPass = true;
    graph.addGraphicsPass({.name = "Clear", .renderArea = {0, 0, 16, 16},
        .colorAttachments = {{transient, rhi::LoadOp::Clear, rhi::StoreOp::Store},
                             {external, rhi::LoadOp::Clear, rhi::StoreOp::Store}},
        .execute = [&](const RgExecuteContext&) {
            if (throwDuringPass) throw std::runtime_error("cancel recorded rendering");
        }});
    const auto backbuffer = graph.importSwapchainTexture(swapchain);
    graph.addGraphicsPass({.name = "Present",
        .renderAreaResolver = [&](const RenderContext&) {
            return rhi::Rect{0, 0, swapchain.width(), swapchain.height()};
        },
        .colorAttachments = {{backbuffer, rhi::LoadOp::Clear, rhi::StoreOp::Store}},
        .execute = [](const RgExecuteContext&) {}});
    rhi::RID firstTexture;
    for (int iteration = 0; iteration < 3; ++iteration) {
        ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
        pool.collect(d.completedSubmission());
        pool.beginBatch(swapchain.commandBuffer());
        if (!graph.compiled()) graph.compile(context);
        auto frame = graph.prepareFrame(context);
        if (!firstTexture) firstTexture = frame.texture(transient);
        EXPECT_EQ(frame.texture(transient), firstTexture);
        EXPECT_EQ(state, rhi::ResourceState::Undefined);
        if (iteration == 0) {
            EXPECT_THROW(graph.execute(context, frame, swapchain.commandBuffer()), std::runtime_error);
            throwDuringPass = false;
        } else {
            graph.execute(context, frame, swapchain.commandBuffer());
            EXPECT_EQ(state, rhi::ResourceState::Undefined);
            EXPECT_EQ(pool.trackedState(&state), rhi::ResourceState::ShaderRead);
        }
        const auto previous = d.lastSubmitted();
        frame.reset();
        if (iteration < 2) {
            EXPECT_EQ(swapchain.cancelFrame(), rhi::FrameStatus::Ready);
            pool.cancelBatch();
            EXPECT_EQ(state, rhi::ResourceState::Undefined);
        } else {
            EXPECT_EQ(swapchain.endFrame(), rhi::FrameStatus::Ready);
            pool.onSubmitted(d.lastSubmitted());
            EXPECT_EQ(state, rhi::ResourceState::ShaderRead);
        }
        EXPECT_EQ(d.lastSubmitted(), previous + 1);
        d.waitIdle();
        EXPECT_EQ(d.completedSubmission(), d.lastSubmitted());
        pool.collect(d.completedSubmission());
    }
    EXPECT_EQ(graph.compileCount(), 1U);
    EXPECT_EQ(pool.totalEntryCount(), 1U);
    auto* reused = pool.acquire(transientDesc, rhi::TextureUsage::ColorAttachment);
    EXPECT_EQ(reused->texture->textureHandle(), firstTexture);
    EXPECT_EQ(reused->lastState, rhi::ResourceState::ColorAttachment);
    pool.release(reused);
    cache.invalidateFramebuffers(outputView);
    d.texture_destroy(output);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

TEST_F(RhiDeviceTest, PreparedGraphsRefreshSharedImportStateBeforeExecution) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    RgTexturePool pool; RgRenderPassCache cache; RenderScene scene;
    RenderContext context(pool, cache, scene);
    const auto texture = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled});
    const auto view = d.texture_default_view(texture);
    auto state = rhi::ResourceState::Undefined;
    RenderGraph first, second, unused;
    const auto resolver = [&](RenderContext&) {
        return RgTextureBinding{texture, view, state, rhi::AccessMode::Auto, &state};
    };
    const auto build = [&](RenderGraph& graph, rhi::ResourceState final, bool traditional) {
        const auto handle = graph.importTexture("Shared", resolver, final);
        RgRasterPass pass{.name = "Color", .renderArea = {0, 0, 16, 16},
            .colorAttachments = {{handle, traditional ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
                                  rhi::StoreOp::Store}},
            .execute = [](const RgExecuteContext&) {}};
        if (traditional) graph.addRasterGroup("ColorGroup", {pass});
        else graph.addGraphicsPass(pass);
        graph.compile(context);
    };
    build(first, rhi::ResourceState::ShaderRead, false);
    build(second, rhi::ResourceState::ColorAttachment, true);
    (void)unused.importTexture("Shared", resolver);
    unused.compile(context);
    const auto cmd = d.command_buffer_create();
    // The second iteration starts in ColorAttachment, reproducing the oldLayout error.
    for (int iteration = 0; iteration < 2; ++iteration) {
        auto a = first.prepareFrame(context), b = second.prepareFrame(context);
        auto c = unused.prepareFrame(context);
        rhi::begin(cmd); pool.beginBatch(cmd);
        first.execute(context, a, cmd);
        unused.execute(context, c, cmd); // Must not restore the snapshot of an unused import.
        EXPECT_EQ(pool.trackedState(&state), rhi::ResourceState::ShaderRead);
        second.execute(context, b, cmd);
        ASSERT_EQ(b.groups()[0].preBarriers.size(), 1U);
        EXPECT_EQ(b.groups()[0].preBarriers[0].before, rhi::ResourceState::ShaderRead);
        rhi::end(cmd);
        if (iteration == 0) EXPECT_EQ(state, rhi::ResourceState::Undefined);
        pool.onSubmitted(d.submit(cmd, {}));
        EXPECT_EQ(state, rhi::ResourceState::ColorAttachment);
        d.waitIdle(); pool.collect(d.completedSubmission());
    }
    {
        auto a = first.prepareFrame(context), b = second.prepareFrame(context);
        rhi::begin(cmd); pool.beginBatch(cmd);
        first.execute(context, a, cmd);
        EXPECT_EQ(pool.trackedState(&state), rhi::ResourceState::ShaderRead);
        d.command_buffer(cmd).discard();
        pool.cancelBatch();
        EXPECT_EQ(state, rhi::ResourceState::ColorAttachment);
    }
    EXPECT_EQ(first.compileCount(), 1U);
    EXPECT_EQ(second.compileCount(), 1U);
    cache.clear(); d.command_buffer_destroy(cmd); d.texture_destroy(texture);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

TEST_F(RhiDeviceTest, BeginFrameAllocationFailureReturnsImageAndKeepsFenceUsable) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
    const auto before = d.lastSubmitted();
    {
        FailNextAllocation failure(sizeof(VkImageMemoryBarrier));
        EXPECT_THROW((void)swapchain.beginFrame(), std::bad_alloc);
    }
    EXPECT_EQ(d.lastSubmitted(), before + 1); // Recovery presents a cleared image.
    for (int frame = 0; frame < 5; ++frame) {
        ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
        (void)swapchain.cancelFrame();
    }
    d.waitIdle();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

TEST_F(RhiDeviceTest, SubmissionTrackingAllocationFailureCanCancelAndReuseFrameFence) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
    bool caught = false;
    // deque grows periodically. Only endFrame's CPU submission tracking allocates here.
    for (int attempt = 0; attempt < 128 && !caught; ++attempt) {
        ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
        const rhi::TextureBarrier toPresent{.texture = swapchain.currentTexture(),
            .before = swapchain.currentTextureState() == rhi::ResourceState::Undefined ?
                rhi::ResourceState::ColorAttachment : swapchain.currentTextureState(),
            .after = rhi::ResourceState::Present};
        rhi::resourceBarriers(swapchain.commandBuffer(), std::span{&toPresent, 1});
        const auto before = d.lastSubmitted();
        try {
            FailNextAllocation failure;
            (void)swapchain.endFrame();
        } catch (const std::bad_alloc&) {
            caught = true;
            EXPECT_EQ(d.lastSubmitted(), before);
            (void)swapchain.cancelFrame();
        }
    }
    ASSERT_TRUE(caught);
    for (int frame = 0; frame < 5; ++frame) {
        ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
        (void)swapchain.cancelFrame();
    }
    d.waitIdle();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

TEST_F(RhiDeviceTest, StagingUploadsRetireOnlyWithTheirOwnSubmission) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
    const auto destination = d.buffer_create({16, rhi::BufferUsage::TransferDestination,
        rhi::MemoryUsage::DeviceLocal, "delayed-upload"});
    const auto image = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 2, .height = 2,
        .usage = rhi::TextureUsage::TransferDestination | rhi::TextureUsage::Sampled});
    const auto allocations = [&] {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(d.allocator(), &statistics);
        return statistics.total.statistics.allocationCount;
    };
    const auto baseline = allocations();
    const auto delayed = d.command_buffer_create();
    const std::vector<std::byte> bytes(16, std::byte{42});
    rhi::begin(delayed);
    rhi::updateBuffer(delayed, {destination, 0, bytes});
    const rhi::TextureBarrier toCopy{.texture = image,
        .before = rhi::ResourceState::Undefined, .after = rhi::ResourceState::CopyDestination};
    rhi::resourceBarriers(delayed, std::span{&toCopy, 1});
    rhi::updateImage(delayed, {.destination = image, .extent = {2, 2, 1}, .data = bytes});
    rhi::end(delayed);
    EXPECT_EQ(allocations(), baseline + 2);
    ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
    (void)swapchain.cancelFrame();
    d.waitIdle();
    d.collectStagingBuffers();
    EXPECT_EQ(allocations(), baseline + 2); // Both delayed copies still own their scratch.
    d.submit(delayed, {}); // Also exercises retirement with a device-owned fence.
    d.waitIdle();
    d.collectStagingBuffers();
    EXPECT_EQ(allocations(), baseline);
    d.command_buffer_destroy(delayed);
    d.texture_destroy(image);
    d.buffer_destroy(destination);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

TEST_F(RhiDeviceTest, StagingDiscardResetAndSwapchainDestructionReleaseScratchSafely) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    const auto destination = d.buffer_create({16, rhi::BufferUsage::TransferDestination,
        rhi::MemoryUsage::DeviceLocal, "cancelled-upload"});
    const auto allocations = [&] {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(d.allocator(), &statistics);
        return statistics.total.statistics.allocationCount;
    };
    const auto baseline = allocations();
    const std::vector<std::byte> bytes(16);
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    rhi::updateBuffer(cmd, {destination, 0, bytes});
    d.command_buffer(cmd).discard();
    EXPECT_EQ(allocations(), baseline);
    rhi::begin(cmd);
    rhi::updateBuffer(cmd, {destination, 0, bytes});
    rhi::end(cmd);
    rhi::begin(cmd); // Replacing an unsubmitted recording also releases its scratch.
    EXPECT_EQ(allocations(), baseline);
    rhi::updateBuffer(cmd, {destination, 0, bytes});
    rhi::end(cmd);
    d.command_buffer_destroy(cmd);
    EXPECT_EQ(allocations(), baseline);
    {
        rhi::vulkan::VulkanSwapchain swapchain(d, {.width = 64, .height = 64, .vsync = false});
        ASSERT_EQ(swapchain.beginFrame(), rhi::FrameStatus::Ready);
        const auto frame = swapchain.commandBuffer();
        rhi::updateBuffer(frame, {destination, 0, bytes});
        const rhi::TextureBarrier toPresent{.texture = swapchain.currentTexture(),
            .before = rhi::ResourceState::ColorAttachment, .after = rhi::ResourceState::Present};
        rhi::resourceBarriers(frame, std::span{&toPresent, 1});
        (void)swapchain.endFrame();
    } // Destroys the borrowed fence before collecting the submitted upload.
    d.collectStagingBuffers();
    EXPECT_EQ(allocations(), baseline);
    d.buffer_destroy(destination);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
}

TEST_F(RhiDeviceTest, TransientBufferSurvivesAllReferencingRecordingsAndSubmissions) {
    auto& d = device();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    const auto destination = d.buffer_create({16, rhi::BufferUsage::TransferDestination,
        rhi::MemoryUsage::DeviceLocal, "shared-transient-copy"});
    const auto allocations = [&] {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(d.allocator(), &statistics);
        return statistics.total.statistics.allocationCount;
    };
    const auto baseline = allocations();
    const auto transient = d.buffer_acquire_transient({16, rhi::BufferUsage::TransferSource,
        rhi::MemoryUsage::Upload, "shared-transient"});
    const std::vector<std::byte> bytes(16);
    d.buffer_upload(transient, bytes);
    const auto first = d.command_buffer_create(), second = d.command_buffer_create();
    for (const auto cmd : {first, second}) {
        rhi::begin(cmd);
        rhi::copyBuffer(cmd, {transient, destination, 0, 0, 16});
        rhi::end(cmd);
    }
    d.submit(first, {});
    d.waitIdle();
    d.collectStagingBuffers();
    EXPECT_EQ(allocations(), baseline + 1);
    d.submit(second, {});
    d.waitIdle();
    d.collectStagingBuffers();
    EXPECT_EQ(allocations(), baseline);
    d.command_buffer_destroy(first);
    d.command_buffer_destroy(second);
    const auto cancelled = d.buffer_acquire_transient({16, rhi::BufferUsage::Vertex,
        rhi::MemoryUsage::Upload, "cancelled-stream"});
    const auto discarded = d.command_buffer_create();
    rhi::begin(discarded);
    rhi::bindVertexBuffer(discarded, 0, cancelled);
    d.command_buffer(discarded).discard();
    EXPECT_EQ(allocations(), baseline);
    d.command_buffer_destroy(discarded);
    d.buffer_destroy(destination);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos)
        << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos)
        << validation.messages.str();
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
    RenderScene scene;
    RenderContext renderContext(pool, cache, scene);
    RenderGraph graph;
    rhi::RID pipelines[2];
    rhi::RID cachedPass, cachedFramebuffer;
    std::vector<std::uint32_t> callbackOrder;
    const auto gbuffer = graph.createTexture(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16, "graph-gbuffer"));
    const auto destination = graph.importTexture("Output", [&](RenderContext&) {
        return RgTextureBinding{output, outputView, outputState, rhi::AccessMode::Auto, &outputState};
    }, rhi::ResourceState::CopySource);
    const auto normal = graph.createTexture(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16, "graph-normal"));
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
        pool.collect(d.completedSubmission());
        pool.beginBatch(cmd);
        auto frame = record();
        rhi::begin(cmd);
        graph.execute(renderContext, frame, cmd);
        EXPECT_EQ(pool.trackedState(&outputState), rhi::ResourceState::CopySource);
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
        pool.onSubmitted(d.submit(cmd, {}));
        EXPECT_EQ(outputState, rhi::ResourceState::CopySource);
        d.waitIdle();
        pool.collect(d.completedSubmission());
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
    const auto depth = graph.createTexture(test::textureDesc(TextureFormat::Depth32Float, 16, 16, "graph-depth-input"));
    const auto output = graph.createTexture(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16, "graph-depth-output"));
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
    pool.beginBatch(cmd);
    graph.execute(renderContext, frame, cmd);
    rhi::end(cmd);
    pool.onSubmitted(d.submit(cmd, {}));
    d.waitIdle();
    pool.collect(d.completedSubmission());
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
    desc.sampler.filterMode = TextureFilterMode::Point;
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
    const auto texture = d.texture_create({.format = pixelFormat, .width = 5, .height = 3,
        .mipCount = 3, .usage = usage});
    ASSERT_TRUE(texture);
    // The raw RHI path explicitly requests TransferSource for this upload/readback test.
    std::vector<rhi::TextureUploadRegion> regions;
    const auto order = format == TextureFormat::R8Unorm ? std::array{2U, 0U, 1U} : std::array{0U, 1U, 2U};
    for (auto mip : order) {
        rhi::TextureUploadRegion region;
        region.mipLevel = mip;
        region.width = std::max(1U, 5U >> mip);
        region.height = std::max(1U, 3U >> mip);
        region.data = std::span<const std::byte>{pixels}.subspan(packed.offsets[mip], packed.sizes[mip]);
        regions.push_back(region);
    }
    d.texture_upload(texture, regions);
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
    rhi::TextureBarrier barrier{texture, aspect, rhi::ResourceState::ShaderRead,
                                rhi::ResourceState::CopyDestination};
    barrier.mipCount = 3;
    rhi::resourceBarriers(cmd, std::span{&barrier, 1});
    // Replace the second texel of mip 1 with a valid texel from mip 0.
    const auto replacement = std::span<const std::byte>{pixels}.first(info.bytesPerBlock);
    rhi::updateImage(cmd, {.destination = texture, .mipLevel = 1,
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
        rhi::BufferImageCopy copy{destination, texture, padded.offsets[mip],
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
    d.texture_destroy(texture);
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

INSTANTIATE_TEST_SUITE_P(AllFormats, TextureUploadFormatTest,
    ::testing::Values(TextureFormat::Rgba8Unorm, TextureFormat::Rgba8Srgb,
        TextureFormat::Bgra8Unorm, TextureFormat::Bgra8Srgb, TextureFormat::Depth32Float,
        TextureFormat::R8Unorm, TextureFormat::Rg8Unorm, TextureFormat::R16Float,
        TextureFormat::Rg16Float, TextureFormat::Rgba16Float, TextureFormat::R32Float,
        TextureFormat::Rg32Float, TextureFormat::Rgba32Float, TextureFormat::Depth16Unorm, TextureFormat::R32Uint));

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


TEST_F(RhiDeviceTest, FormatCapabilitiesMatchNativeFeaturesForEachTiling) {
    auto& d = device();
    for (const auto format : {rhi::PixelFormat::Rgba8Unorm, rhi::PixelFormat::Rgba8Srgb,
         rhi::PixelFormat::Bgra8Unorm, rhi::PixelFormat::Bgra8Srgb, rhi::PixelFormat::Depth32Float,
         rhi::PixelFormat::R8Unorm, rhi::PixelFormat::Rg8Unorm, rhi::PixelFormat::R16Float,
         rhi::PixelFormat::Rg16Float, rhi::PixelFormat::Rgba16Float, rhi::PixelFormat::R32Float,
         rhi::PixelFormat::Rg32Float, rhi::PixelFormat::Rgba32Float, rhi::PixelFormat::Depth16Unorm, rhi::PixelFormat::R32Uint}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(d.physicalDevice(), rhi::vulkan::toVulkan(format), &properties);
        for (auto tiling : {rhi::TextureTiling::Optimal, rhi::TextureTiling::Linear}) {
            const auto caps = d.texture_format_capabilities(format, tiling);
            const auto bits = tiling == rhi::TextureTiling::Optimal ? properties.optimalTilingFeatures :
                                                                    properties.linearTilingFeatures;
            EXPECT_EQ(caps.sampled, (bits & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0);
            EXPECT_EQ(caps.linearFilter, (bits & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0);
            EXPECT_EQ(caps.colorAttachment, (bits & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0);
            EXPECT_EQ(caps.depthStencilAttachment, (bits & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
            EXPECT_EQ(caps.colorAttachmentBlend, (bits & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0);
            EXPECT_EQ(caps.transferSource, (bits & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0);
            EXPECT_EQ(caps.transferDestination, (bits & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0);
            for (auto usage : {rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination,
                               rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment,
                               rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled}) {
                VkImageFormatProperties image{};
                const bool nativeSupport = vkGetPhysicalDeviceImageFormatProperties(d.physicalDevice(),
                    rhi::vulkan::toVulkan(format), VK_IMAGE_TYPE_2D,
                    tiling == rhi::TextureTiling::Optimal ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
                    rhi::vulkan::toVulkan(usage), 0, &image) == VK_SUCCESS;
                for (auto samples : {rhi::SampleCount::One, rhi::SampleCount::Two,
                                     rhi::SampleCount::Four, rhi::SampleCount::Eight}) {
                    rhi::TextureDesc desc{.format = format, .width = 5, .height = 3,
                        .usage = usage, .tiling = tiling, .samples = samples};
                    EXPECT_EQ(d.texture_is_supported(desc), nativeSupport &&
                        image.maxExtent.width >= 5 && image.maxExtent.height >= 3 &&
                        image.maxMipLevels >= 1 && image.maxArrayLayers >= 1 &&
                        (image.sampleCounts & rhi::vulkan::toVulkan(samples)) != 0);
                }
            }
        }
    }
    EXPECT_FALSE(d.texture_format_capabilities(rhi::PixelFormat::Undefined).sampled);
    EXPECT_FALSE(d.texture_format_capabilities(static_cast<rhi::PixelFormat>(999)).linearFilter);
    EXPECT_FALSE(d.texture_format_capabilities(rhi::PixelFormat::R8Unorm,
        static_cast<rhi::TextureTiling>(999)).colorAttachment);
}

TEST_F(RhiDeviceTest, UnsupportedTextureDescriptionsReturnFailureBeforeAllocation) {
    auto& d = device();
    rhi::TextureDesc valid{.format = rhi::PixelFormat::Rgba8Unorm, .width = 4, .height = 4,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination};
    ASSERT_TRUE(d.texture_is_supported(valid));
    for (int scenario = 0; scenario < 16; ++scenario) {
        auto desc = valid;
        switch (scenario) {
        case 0: desc.format = rhi::PixelFormat::Undefined; break;
        case 1: desc.format = static_cast<rhi::PixelFormat>(999); break;
        case 2: desc.width = 0; break;
        case 3: desc.width = UINT32_MAX; break;
        case 4: desc.height = UINT32_MAX; break;
        case 5: desc.depth = 2; break;
        case 6: desc.arrayLayers = 2; break;
        case 7: desc.dimension = rhi::TextureType::TextureCube; break;
        case 8: desc.mipCount = 4; break;
        case 9: desc.mipCount = 2; desc.samples = rhi::SampleCount::Four; break;
        case 10: desc.samples = static_cast<rhi::SampleCount>(999); break;
        case 11: desc.tiling = static_cast<rhi::TextureTiling>(999); break;
        case 12: desc.usage = rhi::TextureUsage::None; break;
        case 13: desc.usage = static_cast<rhi::TextureUsage>(0x80000000U); break;
        case 14: desc.usage = rhi::TextureUsage::DepthStencilAttachment; break;
        case 15: desc.format = rhi::PixelFormat::Depth32Float;
                 desc.usage = rhi::TextureUsage::ColorAttachment; break;
        }
        EXPECT_FALSE(d.texture_is_supported(desc)) << scenario;
        EXPECT_FALSE(d.texture_create(desc)) << scenario;
    }
    // A valid mip chain and explicit filtering still create through the engine API.
    TextureDesc desc{TextureType::Texture2D, TextureFormat::Rgba8Unorm, 4, 4, 3};
    desc.sampler.filterMode = TextureFilterMode::Point;
    EXPECT_TRUE(Texture::create(desc));
    desc.width = UINT32_MAX;
    EXPECT_FALSE(Texture::create(desc));
}


class HdrRenderGraphTest : public RhiDeviceTest,
                           public ::testing::WithParamInterface<std::tuple<bool, TextureFormat>> {};

TEST_P(HdrRenderGraphTest, DrawsFloatAttachmentsAndReusesCompiledGraphAcrossFramesAndResize) {
    auto& d = device();
    const auto [traditional, depthFormat] = GetParam();
    for (const auto format : {TextureFormat::Rgba16Float, TextureFormat::Rgba32Float, depthFormat}) {
        const bool depth = rhi::isDepthFormat(toRhi(format));
        rhi::TextureDesc desc{.format = toRhi(format), .width = 16, .height = 8,
            .usage = (depth ? rhi::TextureUsage::DepthStencilAttachment : rhi::TextureUsage::ColorAttachment) |
                     rhi::TextureUsage::TransferSource};
        if (!d.texture_is_supported(desc) || (!depth &&
            !d.texture_format_capabilities(desc.format).colorAttachmentBlend))
            GTEST_SKIP() << "Device lacks required attachment/transfer/blend capabilities";
    }
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderScene scene;
    RenderContext context(pool, cache, scene);
    RenderGraph graph;
    std::uint32_t width = 16, height = 8;
    const auto extent = [&](const RenderContext&) { return rhi::Rect{0, 0, width, height}; };
    const auto makeTexture = [&](TextureFormat format) {
        TextureDesc desc{TextureType::Texture2D, format, width, height};
        desc.sampler.filterMode = TextureFilterMode::Point;
        return graph.createTexture(desc, extent, rhi::ResourceState::CopySource);
    };
    const RgTextureHandle handles[]{makeTexture(TextureFormat::Rgba16Float),
        makeTexture(TextureFormat::Rgba32Float), makeTexture(depthFormat)};
    rhi::RID pipelines[2];
    RgRasterPass first;
    first.name = "HDR clear and additive draw";
    first.renderAreaResolver = extent;
    first.colorAttachments = {{handles[0], rhi::LoadOp::Clear, rhi::StoreOp::Store, {1.5F, 2, 3, 1}},
                              {handles[1], rhi::LoadOp::Clear, rhi::StoreOp::Store, {1.5F, 2, 3, 1}}};
    first.depthAttachments = {{handles[2], rhi::LoadOp::Clear, rhi::StoreOp::Store, 0.5F}};
    const auto draw = [&](const RgExecuteContext& execution, std::uint32_t index) {
        configureGraphDraw(execution.commandBuffer, pipelines[index]);
        rhi::setViewport(execution.commandBuffer,
            {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
        rhi::setScissor(execution.commandBuffer, {0, 0, width, height});
        rhi::setBlendState(execution.commandBuffer, rhi::BlendMode::Additive);
        rhi::draw(execution.commandBuffer, {3});
    };
    first.execute = [&](const RgExecuteContext& execution) { draw(execution, 0); };
    RgRasterPass second = first;
    second.name = "HDR load and additive draw";
    for (auto& attachment : second.colorAttachments) attachment.loadOp = rhi::LoadOp::Load;
    second.depthAttachments[0].loadOp = rhi::LoadOp::Load;
    second.execute = [&](const RgExecuteContext& execution) { draw(execution, 1); };
    if (traditional) graph.addRasterGroup("HDR subpasses", {first, second});
    else { graph.addGraphicsPass(first); graph.addGraphicsPass(second); }
    graph.compile(context);
    const auto vertex = d.shader_create({rhi::ShaderStage::Vertex,
        graphShaderBytes("render_graph_fullscreen.vert.spv"), "HDR vertex"});
    const auto fragment = d.shader_create({rhi::ShaderStage::Fragment,
        graphShaderBytes("render_graph_gbuffer.frag.spv"), "HDR fragment"});
    const auto layout = d.pipeline_layout_create({{}, "HDR layout"});
    rhi::RID firstTextures[3], firstFramebuffer, renderPass;
    const auto cmd = d.command_buffer_create();
    for (int iteration = 0; iteration < 4; ++iteration) {
        width = iteration == 2 ? 9U : 16U;
        height = iteration == 2 ? 7U : 8U;
        pool.collect(d.completedSubmission());
        auto frame = graph.prepareFrame(context);
        if (iteration == 0) {
            for (int i = 0; i < 3; ++i) firstTextures[i] = frame.texture(handles[i]);
            firstFramebuffer = frame.groups()[0].beginInfo.framebuffer;
            renderPass = frame.groups()[0].beginInfo.renderPass;
            for (std::uint32_t i = 0; i < 2; ++i) {
                rhi::GraphicsPipelineDesc desc;
                desc.vertexShader = vertex; desc.fragmentShader = fragment; desc.pipelineLayout = layout;
                desc.colorFormats = {rhi::PixelFormat::Rgba16Float, rhi::PixelFormat::Rgba32Float};
                desc.depthFormat = toRhi(depthFormat);
                desc.renderPass = renderPass; desc.subpass = traditional ? i : 0;
                pipelines[i] = d.pipeline_create(desc);
                ASSERT_TRUE(pipelines[i]);
            }
        } else if (iteration != 2) {
            for (int i = 0; i < 3; ++i) EXPECT_EQ(frame.texture(handles[i]), firstTextures[i]);
            EXPECT_EQ(frame.groups()[0].beginInfo.framebuffer, firstFramebuffer);
        } else if (traditional) {
            EXPECT_NE(frame.groups()[0].beginInfo.framebuffer, firstFramebuffer);
        }
        EXPECT_EQ(frame.groups()[0].beginInfo.renderPass, renderPass);
        std::size_t offsets[3], sizes[3], total = 0;
        const rhi::PixelFormat formats[]{rhi::PixelFormat::Rgba16Float, rhi::PixelFormat::Rgba32Float, toRhi(depthFormat)};
        for (int i = 0; i < 3; ++i) {
            offsets[i] = (total + 15U) / 16U * 16U;
            sizes[i] = static_cast<std::size_t>(width) * height * rhi::formatInfo(formats[i]).bytesPerBlock;
            total = offsets[i] + sizes[i];
        }
        VkBuffer readback{};
        VmaAllocation allocation{};
        VmaAllocationInfo allocationInfo{};
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = total; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo memoryInfo{};
        memoryInfo.usage = VMA_MEMORY_USAGE_AUTO;
        memoryInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ASSERT_EQ(vmaCreateBuffer(d.allocator(), &bufferInfo, &memoryInfo,
            &readback, &allocation, &allocationInfo), VK_SUCCESS);
        rhi::begin(cmd);
        pool.beginBatch(cmd);
        graph.execute(context, frame, cmd);
        const auto native = d.command_buffer(cmd).nativeCommandBuffer();
        for (int i = 0; i < 3; ++i) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = offsets[i];
            copy.imageSubresource = {i == 2 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(native, d.resolveTexture(frame.texture(handles[i])),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        }
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback; host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &host, 0, nullptr);
        rhi::end(cmd);
        pool.onSubmitted(d.submit(cmd, {}));
        d.waitIdle();
        pool.collect(d.completedSubmission());
        ASSERT_EQ(vmaInvalidateAllocation(d.allocator(), allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
        const auto* bytes = static_cast<const std::byte*>(allocationInfo.pMappedData);
        const std::uint16_t expectedHalf[]{0x4000, 0x4200, 0x4480, 0x4200}; // 2, 3, 4.5, 3
        const float expectedFloat[]{1.5F, 2, 5, 3};
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
            std::uint16_t half[4]; float full[4];
            std::memcpy(half, bytes + offsets[0] + pixel * 8, 8);
            std::memcpy(full, bytes + offsets[1] + pixel * 16, 16);
            for (int channel = 0; channel < 4; ++channel) {
                EXPECT_EQ(half[channel], expectedHalf[channel]);
                EXPECT_FLOAT_EQ(full[channel], expectedFloat[channel]);
            }
            if (depthFormat == TextureFormat::Depth16Unorm) {
                std::uint16_t depth; std::memcpy(&depth, bytes + offsets[2] + pixel * 2, 2);
                EXPECT_NEAR(depth, 32768, 1);
            } else {
                float depth; std::memcpy(&depth, bytes + offsets[2] + pixel * 4, 4);
                EXPECT_FLOAT_EQ(depth, 0.5F);
            }
        }
        frame.reset();
        vmaDestroyBuffer(d.allocator(), readback, allocation);
        EXPECT_EQ(graph.compileCount(), 1U);
    }
    EXPECT_EQ(pool.totalEntryCount(), 6U);
    EXPECT_EQ(cache.renderPassCount(), traditional ? 1U : 0U);
    EXPECT_EQ(cache.framebufferCount(), traditional ? 2U : 0U);
    graph.reset();
    d.command_buffer_destroy(cmd);
    for (auto pipeline : pipelines) d.pipeline_destroy(pipeline);
    d.pipeline_layout_destroy(layout); d.shader_destroy(vertex); d.shader_destroy(fragment);
    cache.clear();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

INSTANTIATE_TEST_SUITE_P(RenderingModesAndDepthFormats, HdrRenderGraphTest,
    ::testing::Combine(::testing::Bool(), ::testing::Values(TextureFormat::Depth16Unorm, TextureFormat::Depth32Float)));


TEST_F(RhiDeviceTest, UnsupportedAttachmentSamplesAndTransferOnlyViewsReturnFailure) {
    auto& d = device();
    rhi::RenderPassDesc pass;
    pass.attachments.push_back({rhi::PixelFormat::Rgba16Float, static_cast<rhi::SampleCount>(999),
        rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ResourceState::ColorAttachment,
        rhi::ResourceState::ColorAttachment});
    rhi::SubpassDesc subpass;
    subpass.colors.push_back({0, rhi::ResourceState::ColorAttachment});
    pass.subpasses.push_back(subpass);
    EXPECT_FALSE(d.render_pass_create(pass));
    const auto vertex = d.shader_create({rhi::ShaderStage::Vertex,
        graphShaderBytes("render_graph_fullscreen.vert.spv"), "unsupported-sample vertex"});
    const auto fragment = d.shader_create({rhi::ShaderStage::Fragment,
        graphShaderBytes("render_graph_gbuffer.frag.spv"), "unsupported-sample fragment"});
    const auto layout = d.pipeline_layout_create({{}, "unsupported-sample layout"});
    rhi::GraphicsPipelineDesc pipeline;
    pipeline.vertexShader = vertex; pipeline.fragmentShader = fragment; pipeline.pipelineLayout = layout;
    pipeline.colorFormats = {rhi::PixelFormat::Rgba16Float, rhi::PixelFormat::Rgba32Float};
    pipeline.samples = static_cast<rhi::SampleCount>(999);
    EXPECT_FALSE(d.pipeline_create(pipeline));
    pipeline.colorFormats.clear(); pipeline.depthFormat = rhi::PixelFormat::Depth16Unorm;
    EXPECT_FALSE(d.pipeline_create(pipeline));
    const auto transfer = d.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 4, .height = 4, .usage = rhi::TextureUsage::TransferDestination});
    ASSERT_TRUE(transfer);
    EXPECT_FALSE(d.texture_default_view(transfer));
    d.texture_destroy(transfer);
    TextureDesc desc{TextureType::Texture2D, TextureFormat::Rgba8Unorm, 4, 4};
    EXPECT_TRUE(Texture::create(desc));
    d.pipeline_layout_destroy(layout); d.shader_destroy(vertex); d.shader_destroy(fragment);
}


TEST(UintShaderTest, SourceAssetSerializationGeneratorAndSpirvReflectionPreserveUintOutput) {
    const auto asset = format::parseShaderAsset(VirtualPath{"assets://id.shader.json"}, R"({
        "$schemaVersion":1, "name":"Tests/Id", "subShaders":[{"passes":[{
        "name":"Forward", "lightMode":"Forward", "program":{"vertex":"id.vert","frag":"id.frag"},
        "fragmentOutputs":[{"name":"objectId","type":"Uint","location":0}]}]}]})");
    ASSERT_TRUE(asset);
    BinaryWriter writer;
    ASSERT_TRUE(asset->transfer(writer));
    BinaryReader reader(writer.bytes());
    const auto decoded = makeRef<ShaderAsset>();
    ASSERT_TRUE(decoded->transfer(reader));
    const auto runtime = decoded->buildRuntimeInfo();
    ASSERT_TRUE(runtime);
    Shader shader{*runtime};
    const auto& pass = shader.defaultSubShader().requirePass(ShaderPassType::Forward);
    EXPECT_EQ(pass.fragmentOutputs()[0].type, ShaderValueType::Uint);
    const auto generated = ShaderGenerator{}.generateStage(shader, pass, ShaderStage::Fragment,
        "void FragmentMain(MiniVaryings inputValue, out MiniFragmentOutput outputValue) { outputValue.objectId = 0xf1234567u; }");
    ASSERT_TRUE(generated);
    EXPECT_NE(generated->find("out uint _MiniOut_objectId"), std::string::npos);
    struct ShaderFiles {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("MiniEngineUintShaderTest-" + std::to_string(GetCurrentProcessId()));
        ~ShaderFiles() {
            (void)FILE_SYSTEM.unmount("assets"); (void)FILE_SYSTEM.unmount("shader-cache");
            std::error_code error; std::filesystem::remove_all(root, error);
        }
    } files;
    std::filesystem::create_directories(files.root / "assets");
    ASSERT_TRUE(FILE_SYSTEM.mountDirectory("assets", files.root / "assets", false));
    ASSERT_TRUE(FILE_SYSTEM.mountDirectory("shader-cache", files.root / "cache", false));
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://id.vert"},
        "void VertexMain(MiniVertexInput inputValue, out MiniVaryings outputValue) { gl_Position = vec4(0,0,0,1); }"));
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://id.frag"},
        "void FragmentMain(MiniVaryings inputValue, out MiniFragmentOutput outputValue) { outputValue.objectId = 0xf1234567u; }"));
    ShaderCompilePipeline pipeline;
    const auto program = pipeline.getOrCreate(shader, pass);
    ASSERT_TRUE(program);
    const auto& reflected = pipeline.resolveProgram(program).layout.fragmentOutputs;
    ASSERT_EQ(reflected.size(), 1U);
    EXPECT_EQ(reflected[0].type, ShaderValueType::Uint);
    EXPECT_EQ(reflected[0].location, 0U);
    auto invalid = *runtime;
    invalid.subShaders[0].passes[0].vertexInput.push_back({"id", "", ShaderValueType::Uint, 0});
    EXPECT_FALSE(validateShaderCreateInfo(invalid));
}

TEST_F(RhiDeviceTest, IntegerPipelineRejectsFloatOutputAndFloatPipelineRejectsUintOutput) {
    auto& d = device();
    const auto vertex = d.shader_create({rhi::ShaderStage::Vertex,
        graphShaderBytes("render_graph_fullscreen.vert.spv"), "ID vertex"});
    const auto uintFragment = d.shader_create({rhi::ShaderStage::Fragment,
        graphShaderBytes("render_graph_uint.frag.spv"), "ID fragment"});
    const auto floatFragment = d.shader_create({rhi::ShaderStage::Fragment,
        graphShaderBytes("render_graph_gbuffer.frag.spv"), "float fragment"});
    const auto layout = d.pipeline_layout_create({{}, "ID layout"});
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertex; desc.fragmentShader = floatFragment; desc.pipelineLayout = layout;
    desc.colorFormats = {rhi::PixelFormat::R32Uint};
    EXPECT_FALSE(d.pipeline_create(desc));
    desc.fragmentShader = uintFragment;
    desc.colorFormats = {rhi::PixelFormat::Rgba8Unorm};
    EXPECT_FALSE(d.pipeline_create(desc));
    desc.colorFormats = {rhi::PixelFormat::R32Uint};
    desc.fragmentEntry = "missing";
    EXPECT_FALSE(d.pipeline_create(desc));
    desc.fragmentEntry = "main";
    const auto valid = d.pipeline_create(desc);
    ASSERT_TRUE(valid);
    d.pipeline_destroy(valid);
    d.pipeline_layout_destroy(layout);
    d.shader_destroy(vertex); d.shader_destroy(uintFragment); d.shader_destroy(floatFragment);
}

TEST_F(RhiDeviceTest, ClearValueNumericTypeMustMatchTheAttachmentWhenClearing) {
    auto& d = device();
    const auto integer = Texture::createColor(16, 16, TextureFormat::R32Uint);
    const auto floating = Texture::createColor(16, 16, TextureFormat::Rgba16Float);
    ASSERT_TRUE(integer); ASSERT_TRUE(floating);
    const auto cmd = d.command_buffer_create();
    rhi::begin(cmd);
    rhi::RenderingInfo rendering{.renderArea = {0, 0, 16, 16}};
    rendering.colorAttachments.push_back({integer->defaultView(), rhi::LoadOp::Clear, rhi::StoreOp::Store});
    EXPECT_EXIT({
        rhi::beginRendering(cmd, rendering);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Float clear value requires");
    rendering.colorAttachments[0].view = floating->defaultView();
    rendering.colorAttachments[0].clearColor = rhi::ColorClearValue::uint32(0xffffffffU);
    EXPECT_EXIT({
        rhi::beginRendering(cmd, rendering);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Unsigned clear value requires");
    rhi::RenderPassDesc pass;
    pass.attachments.push_back({rhi::PixelFormat::R32Uint, rhi::SampleCount::One,
        rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ResourceState::ColorAttachment, rhi::ResourceState::ColorAttachment});
    rhi::SubpassDesc subpass;
    subpass.colors.push_back({0, rhi::ResourceState::ColorAttachment});
    pass.subpasses.push_back(subpass);
    const auto nativePass = d.render_pass_create(pass);
    const auto framebuffer = d.framebuffer_create({nativePass, {integer->defaultView()}, 16, 16, 1});
    rhi::RenderPassBeginInfo begin{nativePass, framebuffer, {0, 0, 16, 16}, {{}}};
    EXPECT_EXIT({
        rhi::beginRenderPass(cmd, begin);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Float clear value requires");
    d.command_buffer_destroy(cmd);
    d.framebuffer_destroy(framebuffer); d.render_pass_destroy(nativePass);
}

class UintRenderGraphTest : public RhiDeviceTest, public ::testing::WithParamInterface<bool> {};

TEST_P(UintRenderGraphTest, TypedClearAndIdDrawReadBackExactBitsAndRejectBlending) {
    auto& d = device();
    const bool traditional = GetParam();
    struct ValidationCapture {
        std::ostringstream messages;
        std::streambuf* previous{std::cerr.rdbuf(messages.rdbuf())};
        ~ValidationCapture() { std::cerr.rdbuf(previous); }
    } validation;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderScene scene;
    RenderContext context(pool, cache, scene);
    RenderGraph graph;
    TextureDesc textureDesc{TextureType::Texture2D, TextureFormat::R32Uint, 16, 16};
    textureDesc.sampler.filterMode = TextureFilterMode::Point;
    const auto output = graph.createTexture(textureDesc, {}, rhi::ResourceState::CopySource);
    std::uint32_t clearId = 0xfedcba98U;
    RgRasterPass clear;
    clear.name = "Clear unsigned IDs";
    clear.renderArea = {0, 0, 16, 16};
    clear.colorAttachments.push_back({output, rhi::LoadOp::Clear, rhi::StoreOp::Store,
        rhi::ColorClearValue::uint32(clearId)});
    clear.colorAttachments[0].clearColorResolver = [&](const RenderContext&) {
        return rhi::ColorClearValue::uint32(clearId);
    };
    clear.execute = [](const RgExecuteContext& execution) {
        rhi::setBlendState(execution.commandBuffer, rhi::BlendMode::Alpha); // Legal until a draw.
    };
    rhi::RID pipeline;
    int iteration = 0;
    RgRasterPass draw = clear;
    draw.name = "Draw unsigned IDs";
    draw.colorAttachments[0].loadOp = rhi::LoadOp::Load;
    draw.execute = [&](const RgExecuteContext& execution) {
        if (iteration == 0) {
            EXPECT_EXIT({
                std::cerr.rdbuf(validation.previous);
                rhi::draw(execution.commandBuffer, {3});
            }, ::testing::ExitedWithCode(EXIT_FAILURE), "Blending is unsupported");
        }
        configureGraphDraw(execution.commandBuffer, pipeline);
        rhi::setScissor(execution.commandBuffer, {8, 0, 8, 16});
        if (iteration == 0) {
            EXPECT_EXIT({
                std::cerr.rdbuf(validation.previous);
                rhi::setBlendState(execution.commandBuffer, rhi::BlendMode::Alpha);
                rhi::draw(execution.commandBuffer, {3});
            }, ::testing::ExitedWithCode(EXIT_FAILURE), "Blending is unsupported");
            EXPECT_EXIT({
                std::cerr.rdbuf(validation.previous);
                rhi::setBlendState(execution.commandBuffer, rhi::BlendMode::Additive);
                rhi::drawIndexed(execution.commandBuffer, {});
            }, ::testing::ExitedWithCode(EXIT_FAILURE), "Blending is unsupported");
        }
        rhi::draw(execution.commandBuffer, {3});
    };
    if (traditional) graph.addRasterGroup("Unsigned ID subpasses", {clear, draw});
    else { graph.addGraphicsPass(clear); graph.addGraphicsPass(draw); }
    graph.compile(context);
    const auto vertex = d.shader_create({rhi::ShaderStage::Vertex,
        graphShaderBytes("render_graph_fullscreen.vert.spv"), "uint graph vertex"});
    const auto fragment = d.shader_create({rhi::ShaderStage::Fragment,
        graphShaderBytes("render_graph_uint.frag.spv"), "uint graph fragment"});
    const auto layout = d.pipeline_layout_create({{}, "uint graph layout"});
    const auto cmd = d.command_buffer_create();
    rhi::RID firstTexture;
    for (; iteration < 2; ++iteration) {
        clearId = iteration ? 0xffffffffU : 0xfedcba98U;
        pool.collect(d.completedSubmission());
        auto frame = graph.prepareFrame(context);
        if (iteration == 0) {
            firstTexture = frame.texture(output);
            rhi::GraphicsPipelineDesc desc;
            desc.vertexShader = vertex; desc.fragmentShader = fragment; desc.pipelineLayout = layout;
            desc.colorFormats = {rhi::PixelFormat::R32Uint};
            desc.renderPass = frame.groups()[0].beginInfo.renderPass;
            desc.subpass = traditional ? 1 : 0;
            pipeline = d.pipeline_create(desc);
            ASSERT_TRUE(pipeline);
        } else EXPECT_EQ(frame.texture(output), firstTexture);
        VkBuffer readback{};
        VmaAllocation allocation{};
        VmaAllocationInfo allocationInfo{};
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = 16 * 16 * sizeof(std::uint32_t);
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo memoryInfo{};
        memoryInfo.usage = VMA_MEMORY_USAGE_AUTO;
        memoryInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ASSERT_EQ(vmaCreateBuffer(d.allocator(), &bufferInfo, &memoryInfo,
            &readback, &allocation, &allocationInfo), VK_SUCCESS);
        rhi::begin(cmd);
        pool.beginBatch(cmd);
        graph.execute(context, frame, cmd);
        const auto native = d.command_buffer(cmd).nativeCommandBuffer();
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {16, 16, 1};
        vkCmdCopyImageToBuffer(native, d.resolveTexture(frame.texture(output)),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback; host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &host, 0, nullptr);
        rhi::end(cmd);
        pool.onSubmitted(d.submit(cmd, {}));
        d.waitIdle();
        pool.collect(d.completedSubmission());
        ASSERT_EQ(vmaInvalidateAllocation(d.allocator(), allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
        const auto* ids = static_cast<const std::uint32_t*>(allocationInfo.pMappedData);
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 16; ++x)
                EXPECT_EQ(ids[y * 16 + x], x < 8 ? clearId : 0xf1234567U);
        frame.reset();
        vmaDestroyBuffer(d.allocator(), readback, allocation);
        EXPECT_EQ(graph.compileCount(), 1U);
    }
    EXPECT_EQ(pool.totalEntryCount(), 1U);
    graph.reset();
    d.command_buffer_destroy(cmd); d.pipeline_destroy(pipeline);
    d.pipeline_layout_destroy(layout); d.shader_destroy(vertex); d.shader_destroy(fragment);
    cache.clear();
    EXPECT_EQ(validation.messages.str().find("Validation Error"), std::string::npos) << validation.messages.str();
    EXPECT_EQ(validation.messages.str().find("SYNC-HAZARD"), std::string::npos) << validation.messages.str();
}

INSTANTIATE_TEST_SUITE_P(RenderingModes, UintRenderGraphTest, ::testing::Bool());

} // namespace

// Real-device coverage for the RID-only RHI: the per-type handle pools (create/destroy,
// sampler/view dedup) and the command free-function surface that dispatches through the device
// singleton. These replace the old mock-encoder tests: with commands defined only in the Vulkan
// backend, recording can only be exercised against a live device, so the fixture probes for a
// Vulkan 1.3 runtime (and the validation layer in debug) and skips gracefully when absent.
#include "rhi/api/Command.h"
#include "rhi/api/Device.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <memory>
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

} // namespace

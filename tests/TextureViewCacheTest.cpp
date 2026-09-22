#include "rhi/vulkan/VulkanFactory.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <memory>
#include <vector>

namespace {

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
    std::uint32_t count{};
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
        handle_ = CreateWindowExW(0,
                                  kClassName,
                                  kClassName,
                                  WS_OVERLAPPED,
                                  0,
                                  0,
                                  64,
                                  64,
                                  nullptr,
                                  nullptr,
                                  instance_,
                                  nullptr);
    }

    ~HiddenWindow() {
        if (handle_)
            DestroyWindow(handle_);
        UnregisterClassW(kClassName, instance_);
    }

    [[nodiscard]] HINSTANCE instance() const { return instance_; }
    [[nodiscard]] HWND handle() const { return handle_; }

private:
    static constexpr const wchar_t* kClassName = L"MiniTextureViewCacheTest";
    HINSTANCE instance_{};
    HWND handle_{};
};

class TextureViewCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!vulkanRuntimeAvailable())
            GTEST_SKIP() << "Vulkan 1.3 runtime or validation layer is unavailable";
        engine::rhi::vulkan::VulkanFactory factory;
        context = factory.createContext({
            .surface = {.windowSystem = engine::rhi::WindowSystem::Win32,
                        .nativeDisplay = window.instance(),
                        .nativeWindow = window.handle()},
            .swapchain = {.width = 64, .height = 64, .vsync = false},
            .enablePipelineCache = false,
        });
    }

    HiddenWindow window;
    engine::rhi::Context context;
};

TEST_F(TextureViewCacheTest, TextureOwnsDefaultAndDescriptorViewCache) {
    auto& device = *context.device;
    const engine::rhi::TextureDesc textureDesc{
        .dimension = engine::rhi::TextureType::Texture2D,
        .format = engine::rhi::PixelFormat::Rgba8Unorm,
        .width = 4,
        .height = 4,
        .depth = 1,
        .arrayLayers = 1,
        .mipCount = 1,
        .usage = engine::rhi::TextureUsage::Sampled,
        .debugName = "TextureViewCacheTest",
    };
    const auto firstTexture = device.createTexture(textureDesc);
    const auto secondTexture = device.createTexture(textureDesc);
    ASSERT_TRUE(firstTexture);
    ASSERT_TRUE(secondTexture);
    EXPECT_TRUE(device.defaultTextureView(firstTexture));

    const engine::rhi::TextureViewDesc swizzled{
        .type = engine::rhi::TextureType::Texture2D,
        .format = engine::rhi::PixelFormat::Rgba8Unorm,
        .baseMip = 0,
        .mipCount = 1,
        .baseLayer = 0,
        .layerCount = 1,
        .swizzle = {.r = engine::rhi::SwizzleComponent::B,
                    .g = engine::rhi::SwizzleComponent::G,
                    .b = engine::rhi::SwizzleComponent::R,
                    .a = engine::rhi::SwizzleComponent::A},
    };
    const auto first = device.createTextureView(firstTexture, swizzled);
    const auto repeated = device.createTextureView(firstTexture, swizzled);
    const auto otherTexture = device.createTextureView(secondTexture, swizzled);
    EXPECT_EQ(first, repeated);
    EXPECT_NE(first, otherTexture);

    device.destroyTexture(firstTexture);
    device.destroyTexture(secondTexture);
}

TEST_F(TextureViewCacheTest, SamplerDescriptorsAreDeviceCached) {
    auto& device = *context.device;
    engine::rhi::SamplerDesc linear;
    const auto first = device.createSampler(linear);
    const auto repeated = device.createSampler(linear);
    engine::rhi::SamplerDesc point = linear;
    point.minFilter = engine::rhi::SamplerFilter::Nearest;
    point.magFilter = engine::rhi::SamplerFilter::Nearest;
    const auto different = device.createSampler(point);
    EXPECT_EQ(first, repeated);
    EXPECT_NE(first, different);
}

} // namespace

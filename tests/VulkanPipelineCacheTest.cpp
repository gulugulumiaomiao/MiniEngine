#include "core/filesystem/FileSystem.h"
#include "core/filesystem/VirtualPath.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <windows.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

namespace {

// Compiled from: #version 450 / void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
constexpr std::array<std::uint32_t, 188> kVertexSpirv = {
    0x07230203, 0x00010000, 0x000D000B, 0x00000015, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0006000F, 0x00000000, 0x00000004, 0x6E69616D, 0x00000000, 0x0000000D, 0x00030003, 0x00000002,
    0x000001C2, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C,
    0x7269645F, 0x69746365, 0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D, 0x00000000, 0x00060005,
    0x0000000B, 0x505F6C67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x0000000B, 0x00000000,
    0x505F6C67, 0x7469736F, 0x006E6F69, 0x00070006, 0x0000000B, 0x00000001, 0x505F6C67, 0x746E696F,
    0x656A6953, 0x00000000, 0x00070006, 0x0000000B, 0x00000002, 0x435F6C67, 0x4470696C, 0x61747369,
    0x0065636E, 0x00070006, 0x0000000B, 0x00000003, 0x435F6C67, 0x446C6C75, 0x61747369, 0x0065636E,
    0x00030005, 0x0000000D, 0x00000000, 0x00030047, 0x0000000B, 0x00000002, 0x00050048, 0x0000000B,
    0x00000000, 0x0000000B, 0x00000000, 0x00050048, 0x0000000B, 0x00000001, 0x0000000B, 0x00000001,
    0x00050048, 0x0000000B, 0x00000002, 0x0000000B, 0x00000003, 0x00050048, 0x0000000B, 0x00000003,
    0x0000000B, 0x00000004, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008,
    0x00000020, 0x00000000, 0x0004002B, 0x00000008, 0x00000009, 0x00000001, 0x0004001C, 0x0000000A,
    0x00000006, 0x00000009, 0x0006001E, 0x0000000B, 0x00000007, 0x00000006, 0x0000000A, 0x0000000A,
    0x00040020, 0x0000000C, 0x00000003, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D, 0x00000003,
    0x00040015, 0x0000000E, 0x00000020, 0x00000001, 0x0004002B, 0x0000000E, 0x0000000F, 0x00000000,
    0x0004002B, 0x00000006, 0x00000010, 0x00000000, 0x0004002B, 0x00000006, 0x00000011, 0x3F800000,
    0x0007002C, 0x00000007, 0x00000012, 0x00000010, 0x00000010, 0x00000010, 0x00000011, 0x00040020,
    0x00000013, 0x00000003, 0x00000007, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200F8, 0x00000005, 0x00050041, 0x00000013, 0x00000014, 0x0000000D, 0x0000000F, 0x0003003E,
    0x00000014, 0x00000012, 0x000100FD, 0x00010038,
};

// Compiled from: #version 450 / layout(location = 0) out vec4 outColor;
//                void main() { outColor = vec4(1.0, 0.0, 1.0, 1.0); }
constexpr std::array<std::uint32_t, 106> kFragmentSpirv = {
    0x07230203, 0x00010000, 0x000D000B, 0x0000000D, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0006000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00030003, 0x00000002, 0x000001C2, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45,
    0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365, 0x00006576, 0x00080004, 0x475F4C47,
    0x4C474F4F, 0x6E695F45, 0x64756C63, 0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004,
    0x6E69616D, 0x00000000, 0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F, 0x00000000, 0x00040047,
    0x00000009, 0x0000001E, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020,
    0x00000008, 0x00000003, 0x00000007, 0x0004003B, 0x00000008, 0x00000009, 0x00000003, 0x0004002B,
    0x00000006, 0x0000000A, 0x3F800000, 0x0004002B, 0x00000006, 0x0000000B, 0x00000000, 0x0007002C,
    0x00000007, 0x0000000C, 0x0000000A, 0x0000000B, 0x0000000A, 0x0000000A, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0003003E, 0x00000009, 0x0000000C,
    0x000100FD, 0x00010038,
};

std::span<const std::byte> spirvBytes(const std::uint32_t* words, std::size_t count) {
    return {reinterpret_cast<const std::byte*>(words), count * sizeof(std::uint32_t)};
}

// The VulkanDevice constructor is fatal without a usable Vulkan 1.3 driver
// (and the validation layer in debug builds), so probe first and skip gracefully.
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

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HINSTANCE instance() const { return instance_; }
    [[nodiscard]] HWND handle() const { return handle_; }

private:
    static constexpr const wchar_t* kClassName = L"MiniPipelineCacheTest";
    HINSTANCE instance_{};
    HWND handle_{};
};

struct Fixture {
    engine::rhi::ShaderHandle vertex;
    engine::rhi::ShaderHandle fragment;
    engine::rhi::BindGroupLayoutHandle layout;
};

engine::rhi::BindGroupLayoutHandle createLayout(engine::rhi::vulkan::VulkanDevice& device,
                                                std::uint32_t bindingCount) {
    std::vector<engine::rhi::BindGroupLayoutEntry> entries(bindingCount);
    for (std::uint32_t index = 0; index < bindingCount; ++index) {
        entries[index].binding = index;
        entries[index].type = engine::rhi::BindingType::UniformBuffer;
        entries[index].visibility =
            engine::rhi::ShaderVisibility::Vertex | engine::rhi::ShaderVisibility::Fragment;
    }
    engine::rhi::BindGroupLayoutDesc desc;
    desc.entries = entries;
    desc.debugName = "TestLayout";
    return device.createBindGroupLayout(desc);
}

Fixture createFixture(engine::rhi::vulkan::VulkanDevice& device) {
    Fixture fixture;
    engine::rhi::ShaderDesc vertexDesc;
    vertexDesc.stage = engine::rhi::ShaderStage::Vertex;
    vertexDesc.bytecode = spirvBytes(kVertexSpirv.data(), kVertexSpirv.size());
    vertexDesc.debugName = "TestVertex";
    fixture.vertex = device.createShader(vertexDesc);
    engine::rhi::ShaderDesc fragmentDesc;
    fragmentDesc.stage = engine::rhi::ShaderStage::Fragment;
    fragmentDesc.bytecode = spirvBytes(kFragmentSpirv.data(), kFragmentSpirv.size());
    fragmentDesc.debugName = "TestFragment";
    fixture.fragment = device.createShader(fragmentDesc);
    fixture.layout = createLayout(device, 1);
    return fixture;
}

void destroyFixture(engine::rhi::vulkan::VulkanDevice& device, const Fixture& fixture) {
    device.destroyBindGroupLayout(fixture.layout);
    device.destroyShader(fixture.vertex);
    device.destroyShader(fixture.fragment);
}

engine::rhi::GraphicsPipelineDesc basePipelineDesc(const Fixture& fixture) {
    engine::rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = fixture.vertex;
    desc.fragmentShader = fixture.fragment;
    desc.bindGroupLayouts = {fixture.layout};
    desc.colorFormats = {engine::rhi::TextureFormat::Rgba8Unorm};
    desc.depthFormat = engine::rhi::TextureFormat::Depth32Float;
    return desc;
}

[[nodiscard]] bool cacheFileHasEntries(const engine::VirtualPath& path) {
    const auto physical = engine::FileSystem::instance().resolvePhysicalPath(path);
    return physical && std::filesystem::is_regular_file(*physical) &&
           std::filesystem::file_size(*physical) > sizeof(VkPipelineCacheHeaderVersionOne);
}

} // namespace

int main() {
    if (!vulkanRuntimeAvailable()) {
        std::puts("VulkanPipelineCacheTest: Vulkan runtime unavailable, skipping");
        return 0;
    }

    HiddenWindow window;
    engine::rhi::SurfaceSource surface;
    surface.windowSystem = engine::rhi::WindowSystem::Win32;
    surface.nativeDisplay = window.instance();
    surface.nativeWindow = window.handle();

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "mini_pipeline_cache_test";
    std::error_code ignored;
    std::filesystem::remove_all(tempDir, ignored);
    std::filesystem::create_directories(tempDir);
    if (!engine::FileSystem::instance().mountDirectory("pipcache", tempDir, false)) {
        return 10;
    }
    const engine::VirtualPath cachePath{"pipcache://cache.bin"};
    (void)engine::FileSystem::instance().removeFile(cachePath);

    // Phase 1: pipelines sharing a bind group layout share the VkPipelineLayout.
    {
        engine::rhi::vulkan::VulkanDevice device{surface, cachePath};
        const Fixture fixture = createFixture(device);
        engine::rhi::GraphicsPipelineDesc desc = basePipelineDesc(fixture);

        const engine::rhi::GraphicsPipelineHandle first = device.createGraphicsPipeline(desc);
        const engine::rhi::GraphicsPipelineHandle second = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(first).layout == VK_NULL_HANDLE ||
            device.resolvePipeline(first).layout != device.resolvePipeline(second).layout) {
            return 1;
        }

        // Destroying a pipeline must not invalidate the shared layout cache.
        device.destroyGraphicsPipeline(first);
        const engine::rhi::GraphicsPipelineHandle third = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(third).layout != device.resolvePipeline(second).layout) {
            return 2;
        }

        // A different bind group layout gets a different pipeline layout.
        const engine::rhi::BindGroupLayoutHandle other = createLayout(device, 2);
        desc.bindGroupLayouts = {other};
        const engine::rhi::GraphicsPipelineHandle fourth = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(fourth).layout == VK_NULL_HANDLE ||
            device.resolvePipeline(fourth).layout == device.resolvePipeline(second).layout) {
            return 3;
        }

        // Destroying a bind group layout invalidates cached pipeline layouts;
        // rebuilding an equivalent layout yields a usable pipeline again.
        device.destroyGraphicsPipeline(second);
        device.destroyGraphicsPipeline(third);
        device.destroyGraphicsPipeline(fourth);
        device.destroyBindGroupLayout(fixture.layout);
        device.destroyBindGroupLayout(other);
        const engine::rhi::BindGroupLayoutHandle recreated = createLayout(device, 1);
        desc.bindGroupLayouts = {recreated};
        const engine::rhi::GraphicsPipelineHandle fifth = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(fifth).layout == VK_NULL_HANDLE ||
            device.resolvePipeline(fifth).pipeline == VK_NULL_HANDLE) {
            return 4;
        }
        device.destroyGraphicsPipeline(fifth);
        device.destroyBindGroupLayout(recreated);
        device.destroyShader(fixture.vertex);
        device.destroyShader(fixture.fragment);
    }
    if (!cacheFileHasEntries(cachePath)) {
        return 5;
    }

    // Phase 2: a new device reloads the persisted cache and still compiles pipelines.
    {
        engine::rhi::vulkan::VulkanDevice device{surface, cachePath};
        const Fixture fixture = createFixture(device);
        engine::rhi::GraphicsPipelineDesc desc = basePipelineDesc(fixture);
        const engine::rhi::GraphicsPipelineHandle pipeline = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(pipeline).layout == VK_NULL_HANDLE ||
            device.resolvePipeline(pipeline).pipeline == VK_NULL_HANDLE) {
            return 6;
        }
        device.destroyGraphicsPipeline(pipeline);
        destroyFixture(device, fixture);
    }
    if (!cacheFileHasEntries(cachePath)) {
        return 7;
    }

    // Phase 3: corrupted cache data falls back to an empty in-memory cache.
    {
        std::vector<std::byte> garbage(128, static_cast<std::byte>(0xAB));
        if (!engine::FileSystem::instance().writeBinaryAtomic(cachePath, garbage)) {
            return 8;
        }
    }
    {
        engine::rhi::vulkan::VulkanDevice device{surface, cachePath};
        const Fixture fixture = createFixture(device);
        engine::rhi::GraphicsPipelineDesc desc = basePipelineDesc(fixture);
        const engine::rhi::GraphicsPipelineHandle pipeline = device.createGraphicsPipeline(desc);
        if (device.resolvePipeline(pipeline).layout == VK_NULL_HANDLE ||
            device.resolvePipeline(pipeline).pipeline == VK_NULL_HANDLE) {
            return 9;
        }
        device.destroyGraphicsPipeline(pipeline);
        destroyFixture(device, fixture);
    }
    if (!cacheFileHasEntries(cachePath)) {
        return 10;
    }

    (void)engine::FileSystem::instance().unmount("pipcache");
    std::filesystem::remove_all(tempDir, ignored);
    return 0;
}

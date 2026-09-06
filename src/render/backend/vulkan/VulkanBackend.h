#pragma once

#include "core/base/BuildConfig.h"
#include "core/math/Math.h"
#include "render/backend/IRenderBackend.h"
#include "render/mesh/Mesh.h"
#include "rhi/vulkan/Buffer.h"
#include "rhi/vulkan/VulkanCommandEncoder.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace engine {

class Window;
class DescriptorAllocator;
class DescriptorSetLayout;
class GpuAllocator;
class GraphicsPipeline;
class PipelineCache;
class CompiledShaderCache;
class ShaderProgramCache;
class RhiShaderCache;
class MaterialGpuCache;
class MeshGpuCache;
class ShaderPass;

class VulkanBackend final : public IRenderBackend,
                            private rhi::vulkan::IVulkanResourceResolver {
public:
    VulkanBackend(Window& window, bool vsync);
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    [[nodiscard]] MeshDrawInfo prepareMesh(MeshHandle handle, Mesh& mesh) override;
    void releaseMesh(MeshHandle handle) override;
    [[nodiscard]] rhi::GraphicsPipelineHandle pipelineForPass(
        const Shader& shader, const ShaderPass& pass,
        const ShaderVariantKey& variant,
        const VertexLayout& vertexLayout) override;
    void renderFrame(const DrawList& drawList) override;
    void waitIdle() override;

private:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxRenderObjects = 1024;

    bool vsync_{true};

    struct QueueFamilies {
        std::optional<std::uint32_t> graphics;
        std::optional<std::uint32_t> present;
        [[nodiscard]] bool complete() const { return graphics.has_value() && present.has_value(); }
    };

    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct FrameContext {
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        VkFence inFlight{VK_NULL_HANDLE};
        std::unique_ptr<Buffer> sceneBuffer;
        std::unique_ptr<Buffer> objectBuffer;
        std::unique_ptr<DescriptorAllocator> descriptorAllocator;
        VkDescriptorSet sceneDescriptor{VK_NULL_HANDLE};
        std::uint32_t descriptorGeneration{1};
    };

    struct BufferSlot {
        std::unique_ptr<Buffer> resource;
        std::uint32_t generation{1};
        bool alive{};
    };

    void createInstance();
#if defined(MINI_DEBUG)
    void createDebugMessenger();
#endif
    void createSurface();
    void selectPhysicalDevice();
    void createDevice();
    void createGpuResources();
    void createSceneDescriptors();
    void createSwapchain();
    void createCommandResources();
    void createSyncObjects();
    void recreateSwapchain();
    void destroySwapchain();
    [[nodiscard]] rhi::BufferHandle createGpuBuffer(VkDeviceSize size,
                                                     VkBufferUsageFlags usage,
                                                     VmaMemoryUsage memoryUsage,
                                                     VmaAllocationCreateFlags flags = 0);
    void destroyGpuBuffer(rhi::BufferHandle handle);
    [[nodiscard]] Buffer& requireBuffer(rhi::BufferHandle handle);
    [[nodiscard]] const Buffer& requireBuffer(rhi::BufferHandle handle) const;
    void uploadBuffer(rhi::BufferHandle source, rhi::BufferHandle destination,
                      VkDeviceSize size);
    void uploadObjectData(FrameContext& frame, std::span<const ObjectDrawData> objects);
    void uploadSceneData(FrameContext& frame, const SceneDrawData& scene);
    void prepareSceneDescriptor(FrameContext& frame);
    void refreshShaderCaches();
    void recordDrawCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex,
                            const DrawList& drawList);

    [[nodiscard]] VkDevice device() const override { return device_; }
    [[nodiscard]] VkBuffer resolveBuffer(rhi::BufferHandle handle) const override;
    [[nodiscard]] VkImage resolveTexture(rhi::TextureHandle handle) const override;
    [[nodiscard]] VkImageView resolveTextureView(rhi::TextureViewHandle handle) const override;
    [[nodiscard]] rhi::vulkan::ResolvedPipeline
    resolvePipeline(rhi::GraphicsPipelineHandle handle) const override;
    [[nodiscard]] VkDescriptorSet resolveBindGroup(rhi::BindGroupHandle handle) const override;

    friend class MeshGpuCache;

    [[nodiscard]] QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] SwapchainSupport querySwapchainSupport(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;

    Window& window_;
    VkInstance instance_{VK_NULL_HANDLE};
#if defined(MINI_DEBUG)
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
#endif
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    std::unique_ptr<GpuAllocator> allocator_;
    std::vector<BufferSlot> buffers_;
    std::unique_ptr<MeshGpuCache> meshGpuCache_;
    std::unique_ptr<DescriptorSetLayout> sceneDescriptorLayout_;
    std::unique_ptr<DescriptorSetLayout> materialDescriptorLayout_;
    std::unique_ptr<CompiledShaderCache> compiledShaderCache_;
    std::unique_ptr<ShaderProgramCache> shaderProgramCache_;
    std::unique_ptr<RhiShaderCache> rhiShaderCache_;
    std::unique_ptr<MaterialGpuCache> materialGpuCache_;
    std::unique_ptr<PipelineCache> pipelineCache_;
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<bool> swapchainImageInitialized_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::uint32_t swapchainGeneration_{1};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::array<FrameContext, kFramesInFlight> frames_{};
    std::uint32_t currentFrame_{};
    std::uint64_t frameSerial_{};
    std::uint64_t lastShaderPollSerial_{std::numeric_limits<std::uint64_t>::max()};
};

} // namespace engine

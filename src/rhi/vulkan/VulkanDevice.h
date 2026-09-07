#pragma once

#include "rhi/api/Device.h"
#include "rhi/vulkan/VulkanCommandEncoder.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

class Buffer;
class GpuAllocator;
class ShaderModule;
class DescriptorAllocator;
class DescriptorSetLayout;

namespace rhi::vulkan {

class VulkanGraphicsPipeline;

class VulkanDevice final : public IDevice, public IVulkanResourceResolver {
    public:
    VulkanDevice(void* nativeInstance, void* nativeWindow);
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(BufferHandle handle) override;
    void uploadBuffer(BufferHandle destination, std::span<const std::byte> data,
                      std::uint64_t offset = 0) override;

    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& desc) override;
    void destroyShader(ShaderHandle handle) override;

    [[nodiscard]] GraphicsPipelineHandle
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void destroyGraphicsPipeline(GraphicsPipelineHandle handle) override;

    [[nodiscard]] BindGroupLayoutHandle
    createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void destroyBindGroupLayout(BindGroupLayoutHandle handle) override;
    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc& desc) override;
    void destroyBindGroup(BindGroupHandle handle) override;

    void waitIdle() override;

    [[nodiscard]] VkInstance instance() const { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const override { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const { return presentQueue_; }
    [[nodiscard]] VkCommandPool commandPool() const { return commandPool_; }
    [[nodiscard]] VmaAllocator allocator() const;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    [[nodiscard]] std::uint32_t presentQueueFamily() const { return presentQueueFamily_; }

    [[nodiscard]] VkBuffer resolveBuffer(BufferHandle handle) const override;
    [[nodiscard]] VkImage resolveTexture(TextureHandle handle) const override;
    [[nodiscard]] VkImageView resolveTextureView(TextureViewHandle handle) const override;
    [[nodiscard]] VkShaderModule resolveShader(ShaderHandle handle) const;
    [[nodiscard]] VkDescriptorSetLayout resolveBindGroupLayout(BindGroupLayoutHandle handle) const;
    [[nodiscard]] ResolvedPipeline resolvePipeline(GraphicsPipelineHandle handle) const override;
    [[nodiscard]] VkDescriptorSet resolveBindGroup(BindGroupHandle handle) const override;

    private:
    struct QueueFamilies {
        std::uint32_t graphics{};
        std::uint32_t present{};
        bool hasGraphics{};
        bool hasPresent{};
        [[nodiscard]] bool complete() const { return hasGraphics && hasPresent; }
    };

    struct BufferSlot {
        std::unique_ptr<::engine::Buffer> resource;
        MemoryUsage memoryUsage{MemoryUsage::DeviceLocal};
        std::uint32_t generation{1};
    };

    struct ShaderSlot {
        std::unique_ptr<::engine::ShaderModule> resource;
        std::uint32_t generation{1};
    };

    struct PipelineSlot {
        std::unique_ptr<VulkanGraphicsPipeline> resource;
        std::uint32_t generation{1};
    };

    struct BindGroupLayoutSlot {
        std::unique_ptr<::engine::DescriptorSetLayout> resource;
        std::uint32_t generation{1};
    };

    struct BindGroupSlot {
        VkDescriptorSet resource{VK_NULL_HANDLE};
        std::uint32_t generation{1};
    };

    [[nodiscard]] ::engine::Buffer& requireBuffer(BufferHandle handle);
    [[nodiscard]] const ::engine::Buffer& requireBuffer(BufferHandle handle) const;
    void createInstance();
    void createDebugMessenger();
    void createSurface(void* nativeInstance, void* nativeWindow);
    [[nodiscard]] QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    void selectPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void clear();

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    std::unique_ptr<::engine::GpuAllocator> allocator_;
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::uint32_t graphicsQueueFamily_{};
    std::uint32_t presentQueueFamily_{};
    std::vector<BufferSlot> buffers_;
    std::vector<ShaderSlot> shaders_;
    std::vector<PipelineSlot> pipelines_;
    std::vector<BindGroupLayoutSlot> bindGroupLayouts_;
    std::vector<BindGroupSlot> bindGroups_;
    std::unique_ptr<::engine::DescriptorAllocator> descriptorAllocator_;
};

} // namespace rhi::vulkan
} // namespace engine

#pragma once

#include "core/base/HandlePool.h"
#include "rhi/RhiFactory.h"
#include "rhi/api/Device.h"
#include "rhi/vulkan/VulkanCommandEncoder.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine::rhi::vulkan {

class VulkanGraphicsPipeline;
class VulkanBuffer;
class VulkanShaderModule;
class VulkanDescriptorAllocator;
class VulkanDescriptorSetLayout;
class VulkanImage;
class VulkanSampler;

struct PipelineLayoutKeyHash final {
    [[nodiscard]] std::size_t operator()(const std::vector<BindGroupLayoutHandle>& key) const
        noexcept {
        std::size_t seed = key.size();
        for (const BindGroupLayoutHandle& handle : key) {
            seed ^= static_cast<std::size_t>(handle.index) << 32U | handle.generation;
            seed *= 0x9E3779B97F4A7C15ULL;
        }
        return seed;
    }
};

using PipelineLayoutKey = std::vector<BindGroupLayoutHandle>;

class VulkanDevice final : public IDevice {
public:
    // An empty pipeline cache path keeps the cache in memory only.
    explicit VulkanDevice(const SurfaceSource& surface,
                          const engine::VirtualPath& pipelineCachePath = {});
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(BufferHandle handle) override;
    void uploadBuffer(BufferHandle destination,
                      std::span<const std::byte> data,
                      std::uint64_t offset = 0) override;

    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc) override;
    void destroyTexture(TextureHandle handle) override;
    void uploadTexture(TextureHandle destination,
                       std::span<const TextureUploadRegion> regions) override;
    [[nodiscard]] TextureViewHandle createTextureView(const TextureViewDesc& desc) override;
    void destroyTextureView(TextureViewHandle handle) override;
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle handle) override;

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
    [[nodiscard]] VkSampler resolveSampler(SamplerHandle handle) const;
    [[nodiscard]] VkShaderModule resolveShader(ShaderHandle handle) const;
    [[nodiscard]] VkDescriptorSetLayout resolveBindGroupLayout(BindGroupLayoutHandle handle) const;
    [[nodiscard]] ResolvedPipeline resolvePipeline(GraphicsPipelineHandle handle) const override;
    [[nodiscard]] VkDescriptorSet resolveBindGroup(BindGroupHandle handle) const override;

    [[nodiscard]] TextureHandle registerExternalTexture(VkImage image);
    void unregisterExternalTexture(TextureHandle handle);
    [[nodiscard]] TextureViewHandle registerExternalTextureView(VkImageView view);
    void unregisterExternalTextureView(TextureViewHandle handle);

private:
    struct QueueFamilies {
        std::uint32_t graphics{};
        std::uint32_t present{};
        bool hasGraphics{};
        bool hasPresent{};
        [[nodiscard]] bool complete() const { return hasGraphics && hasPresent; }
    };

    struct BufferResource {
        std::unique_ptr<VulkanBuffer> resource;
        MemoryUsage memoryUsage{MemoryUsage::DeviceLocal};
    };

    struct ShaderResource {
        std::unique_ptr<VulkanShaderModule> resource;
    };

    struct PipelineResource {
        std::unique_ptr<VulkanGraphicsPipeline> resource;
    };

    struct BindGroupLayoutResource {
        std::unique_ptr<VulkanDescriptorSetLayout> resource;
    };

    struct BindGroupResource {
        VkDescriptorSet resource{VK_NULL_HANDLE};
    };

    struct TextureResource {
        std::unique_ptr<VulkanImage> owned;
        VkImage external{VK_NULL_HANDLE};
        bool uploaded{};

        [[nodiscard]] VkImage handle() const;
    };

    struct TextureViewResource {
        VkImageView resource{VK_NULL_HANDLE};
        bool owned{};
    };

    struct SamplerResource {
        std::unique_ptr<VulkanSampler> resource;
    };

    [[nodiscard]] BufferResource& requireBufferResource(BufferHandle handle);
    [[nodiscard]] const BufferResource& requireBufferResource(BufferHandle handle) const;
    [[nodiscard]] VulkanBuffer& requireBuffer(BufferHandle handle);
    [[nodiscard]] const VulkanBuffer& requireBuffer(BufferHandle handle) const;
    void createInstance();
    void createDebugMessenger();
    void createSurface(const SurfaceSource& surface);
    [[nodiscard]] QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    void selectPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
    void createCommandPool();
    void createPipelineCache(const engine::VirtualPath& path);
    void savePipelineCache();
    [[nodiscard]] VkPipelineLayout acquirePipelineLayout(const PipelineLayoutKey& key);
    void destroyPipelineLayoutsReferencing(BindGroupLayoutHandle handle);
    void clear();

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::uint32_t graphicsQueueFamily_{};
    std::uint32_t presentQueueFamily_{};
    HandlePool<BufferResource, BufferHandle> buffers_;
    HandlePool<ShaderResource, ShaderHandle> shaders_;
    HandlePool<PipelineResource, GraphicsPipelineHandle> pipelines_;
    HandlePool<BindGroupLayoutResource, BindGroupLayoutHandle> bindGroupLayouts_;
    HandlePool<BindGroupResource, BindGroupHandle> bindGroups_;
    HandlePool<TextureResource, TextureHandle> textures_;
    HandlePool<TextureViewResource, TextureViewHandle> textureViews_;
    HandlePool<SamplerResource, SamplerHandle> samplers_;
    std::unique_ptr<VulkanDescriptorAllocator> descriptorAllocator_;
    VkPipelineCache pipelineCache_{VK_NULL_HANDLE};
    engine::VirtualPath pipelineCachePath_;
    std::unordered_map<PipelineLayoutKey, VkPipelineLayout, PipelineLayoutKeyHash> pipelineLayouts_;
};

} // namespace engine::rhi::vulkan

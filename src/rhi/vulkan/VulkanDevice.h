#pragma once

#include "core/base/HandlePool.h"
#include "core/base/KeyedHandleRegistry.h"
#include "rhi/RhiFactory.h"
#include "rhi/api/Device.h"
#include "rhi/api/Sampler.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

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

struct PipelineLayoutKeyHash final {
    [[nodiscard]] std::size_t
    operator()(const std::vector<BindGroupLayoutHandle>& key) const noexcept {
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
    // 禁用时不创建、读取或保存管线缓存。
    explicit VulkanDevice(const SurfaceSource& surface, bool enablePipelineCache = true);
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
    [[nodiscard]] TextureViewHandle createTextureView(TextureHandle texture,
                                                      const TextureViewDesc& desc) override;
    [[nodiscard]] TextureViewHandle defaultTextureView(TextureHandle texture) const override;
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

    [[nodiscard]] std::unique_ptr<ICommandBuffer> createCommandBuffer() override;
    void submitCommand(ICommandBuffer& command, const SubmitSync& sync) override;

    void waitIdle() override;

    [[nodiscard]] VkInstance instance() const override { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const override { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const override { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const override { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const { return presentQueue_; }
    [[nodiscard]] VkCommandPool commandPool() const { return commandPool_; }
    [[nodiscard]] VmaAllocator allocator() const;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const override {
        return graphicsQueueFamily_;
    }
    [[nodiscard]] std::uint32_t presentQueueFamily() const { return presentQueueFamily_; }

    [[nodiscard]] VkBuffer resolveBuffer(BufferHandle handle) const override;
    [[nodiscard]] IRHITexture* resolveTextureResource(TextureHandle handle) override;
    [[nodiscard]] const IRHITexture* resolveTextureResource(TextureHandle handle) const override;
    [[nodiscard]] VkImage resolveTexture(TextureHandle handle) const override;
    // RHI formats are only tracked for device-owned textures; external images
    // (e.g. swapchain) have no format in the handle table.
    [[nodiscard]] PixelFormat textureFormat(TextureHandle handle) const;
    [[nodiscard]] VkImageView resolveTextureView(TextureViewHandle handle) const override;
    [[nodiscard]] VkSampler resolveSampler(SamplerHandle handle) const;
    [[nodiscard]] VkShaderModule resolveShader(ShaderHandle handle) const;
    [[nodiscard]] VkDescriptorSetLayout resolveBindGroupLayout(BindGroupLayoutHandle handle) const;
    [[nodiscard]] ResolvedPipeline resolvePipeline(GraphicsPipelineHandle handle) const override;
    [[nodiscard]] VkDescriptorSet resolveBindGroup(BindGroupHandle handle) const override;

    [[nodiscard]] TextureHandle
    registerExternalTexture(VkImage image, const TextureDesc& desc, VkFormat nativeFormat);
    void unregisterExternalTexture(TextureHandle handle);
    [[nodiscard]] TextureViewHandle registerExternalTextureView(TextureHandle texture,
                                                                VkImageView view,
                                                                const TextureViewDesc& desc);
    void unregisterExternalTextureView(TextureViewHandle handle);

    // Command buffer staging support. update* commands run while a command buffer is
    // being recorded, so scratch buffers must outlive the recording itself. The device
    // owns them: submitCommand tags them with the submitting frame's fence and they are
    // destroyed once that fence has been signaled.
    [[nodiscard]] BufferHandle acquireStagingBuffer(std::uint64_t size);
    void tagPendingStagingBuffers(VkFence fence);
    // Destroys staging buffers whose fence has been signaled. Called at frame boundaries.
    void collectStagingBuffers();

private:
    friend class VulkanTexture;

    [[nodiscard]] TextureViewHandle acquireTextureView(VulkanTexture& texture,
                                                       const TextureViewDesc& desc);

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

    // Samplers are pure value objects, so the registry dedups them by SamplerDesc: identical
    // descriptors share one handle, which keeps VkSampler creation in one place.
    class SamplerRegistry final
        : public KeyedHandleRegistry<VulkanSampler, SamplerHandle, SamplerDesc, SamplerDescHash> {
        [[nodiscard]] SamplerDesc keyOf(const VulkanSampler& resource) const override {
            return resource.desc();
        }
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
    void createPipelineCache();
    void savePipelineCache();
    [[nodiscard]] VkPipelineLayout acquirePipelineLayout(const PipelineLayoutKey& key);
    void destroyPipelineLayoutsReferencing(BindGroupLayoutHandle handle);
    void clear();

    // Scratch buffer created for a command buffer staging upload; retired once the frame
    // that recorded the copy has completed.
    struct StagingBuffer {
        BufferHandle handle;
        VkFence fence{VK_NULL_HANDLE};
    };
    void retireStagingBuffers(std::vector<StagingBuffer>& pending);

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
    float maxSamplerAnisotropy_{1.0F};
    HandlePool<BufferResource, BufferHandle> buffers_;
    std::vector<StagingBuffer> pendingStagingBuffers_;
    HandlePool<ShaderResource, ShaderHandle> shaders_;
    HandlePool<PipelineResource, GraphicsPipelineHandle> pipelines_;
    HandlePool<BindGroupLayoutResource, BindGroupLayoutHandle> bindGroupLayouts_;
    HandlePool<BindGroupResource, BindGroupHandle> bindGroups_;
    HandlePool<VulkanTexture, TextureHandle> textures_;
    HandlePool<VulkanTextureView, TextureViewHandle> textureViews_;
    SamplerRegistry samplers_;
    std::unique_ptr<VulkanDescriptorAllocator> descriptorAllocator_;
    VkPipelineCache pipelineCache_{VK_NULL_HANDLE};
    std::unordered_map<PipelineLayoutKey, VkPipelineLayout, PipelineLayoutKeyHash> pipelineLayouts_;
};

} // namespace engine::rhi::vulkan

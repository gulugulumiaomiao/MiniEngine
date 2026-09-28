#pragma once

#include "core/base/HandlePool.h"
#include "rhi/RhiFactory.h"
#include "rhi/api/Device.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanDescriptorAllocator.h"
#include "rhi/vulkan/VulkanGraphicsPipeline.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShaderModule.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::rhi::vulkan {

// Native pipeline handles resolved for command-buffer binding. Backend-specific, so it lives
// here rather than on the backend-agnostic IDevice interface.
struct ResolvedPipeline {
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

// A pipeline layout is keyed by the ordered set of bind group layout RIDs it references.
using PipelineLayoutKey = std::vector<RID>;

struct PipelineLayoutKeyHash final {
    [[nodiscard]] std::size_t operator()(const std::vector<RID>& key) const noexcept {
        std::size_t seed = key.size();
        for (const RID& handle : key) {
            seed ^= static_cast<std::size_t>(handle.value());
            seed *= 0x9E3779B97F4A7C15ULL;
        }
        return seed;
    }
};

// Synchronization primitives attached to a submission. Null handles disable the corresponding
// sync point; waitStage is the pipeline stage the wait semaphore blocks before. These are
// Vulkan-native handles, so SubmitSync lives in the backend rather than the api headers; only
// the backend submits (the render layer records into a command-buffer RID and never submits).
struct SubmitSync {
    VkSemaphore waitSemaphore{VK_NULL_HANDLE};
    VkSemaphore signalSemaphore{VK_NULL_HANDLE};
    VkFence signalFence{VK_NULL_HANDLE};
    VkPipelineStageFlags waitStage{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
};

// The single concrete RHI device. It owns one HandlePool per object family, storing the concrete
// Vulkan instances directly (by value, never through a pointer or an abstract interface). Every
// family is managed through a consistent <type>_<op> set that resolves its own pool:
//   xxx_create / xxx_destroy / xxx_upload  - the backend-agnostic IDevice API (desc -> RID)
//   xxx_allocate_rid / xxx_release_rid      - the pool primitives (emplace / release an instance)
// RIDs are untyped; the family is implied by the operation, so a RID minted by texture_create is
// only ever passed back to texture_* operations.
class VulkanDevice final : public IDevice {
public:
    // 禁用时不创建、读取或保存管线缓存。
    explicit VulkanDevice(const SurfaceSource& surface, bool enablePipelineCache = true);
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    // ---- IDevice resource lifecycle (RID-based, backend-agnostic) ----
    [[nodiscard]] RID buffer_allocate_rid(const BufferDesc& desc) override;
    void buffer_allocate_memory(RID handle) override;
    void buffer_free_memory(RID handle) override;
    void buffer_release_rid(RID handle) override;
    [[nodiscard]] RID buffer_create(const BufferDesc& desc) override;
    void buffer_destroy(RID handle) override;
    void buffer_upload(RID destination,
                       std::span<const std::byte> data,
                       std::uint64_t offset = 0) override;
    [[nodiscard]] RID buffer_acquire_transient(const BufferDesc& desc) override;

    [[nodiscard]] RID texture_create(const TextureDesc& desc) override;
    void texture_destroy(RID handle) override;
    void texture_upload(RID destination, std::span<const TextureUploadRegion> regions) override;
    [[nodiscard]] RID texture_view_create(RID texture, const TextureViewDesc& desc) override;
    [[nodiscard]] RID texture_default_view(RID texture) override;
    void texture_view_destroy(RID handle) override;
    [[nodiscard]] RID sampler_create(const SamplerDesc& desc) override;
    void sampler_destroy(RID handle) override;

    [[nodiscard]] RID shader_create(const ShaderDesc& desc) override;
    void shader_destroy(RID handle) override;

    [[nodiscard]] RID pipeline_create(const GraphicsPipelineDesc& desc) override;
    void pipeline_destroy(RID handle) override;

    [[nodiscard]] RID bind_group_layout_create(const BindGroupLayoutDesc& desc) override;
    void bind_group_layout_destroy(RID handle) override;
    [[nodiscard]] RID bind_group_create(const BindGroupDesc& desc) override;
    void bind_group_destroy(RID handle) override;

    void waitIdle() override;

    // ---- Backend-native device accessors ----
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const { return presentQueue_; }
    [[nodiscard]] VkCommandPool commandPool() const { return commandPool_; }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    [[nodiscard]] std::uint32_t presentQueueFamily() const { return presentQueueFamily_; }

    // ---- Native handle resolution (backend-specific; used by the command backend) ----
    [[nodiscard]] VkBuffer resolveBuffer(RID handle) const;
    [[nodiscard]] VkImage resolveTexture(RID handle) const;
    // RHI formats are only tracked for device-owned textures; external images
    // (e.g. swapchain) have no format in the handle table.
    [[nodiscard]] PixelFormat textureFormat(RID handle) const;
    [[nodiscard]] VkImageView resolveTextureView(RID handle) const;
    [[nodiscard]] VkSampler resolveSampler(RID handle) const;
    [[nodiscard]] VkShaderModule resolveShader(RID handle) const;
    [[nodiscard]] VkDescriptorSetLayout resolveBindGroupLayout(RID handle) const;
    [[nodiscard]] ResolvedPipeline resolvePipeline(RID handle) const;
    [[nodiscard]] VkDescriptorSet resolveBindGroup(RID handle) const;

    // ---- Command buffers ----
    // Created/registered here, recorded through the rhi:: command free functions (which resolve
    // the RID via command_buffer()), and submitted through submit(). The swapchain registers its
    // pooled per-frame buffers with command_buffer_allocate_rid(owned = false).
    [[nodiscard]] RID command_buffer_create();
    void command_buffer_destroy(RID handle);
    [[nodiscard]] RID command_buffer_allocate_rid(VkCommandBuffer native, bool owned);
    void command_buffer_release_rid(RID handle);
    [[nodiscard]] VulkanCommandBuffer& command_buffer(RID handle);
    void submit(RID command, const SubmitSync& sync);

    // ---- External (swapchain-owned) texture/view registration ----
    [[nodiscard]] RID texture_allocate_rid(VkImage image, const TextureDesc& desc);
    [[nodiscard]] RID
    texture_view_allocate_rid(RID texture, VkImageView view, const TextureViewDesc& desc);

    // ---- Transient buffer support (staging + MeshUsage::Stream) ----
    // Transient buffers must outlive the recording that references them, so the device owns
    // them: buffer_acquire_transient registers each one here, submit tags it with the submitting
    // frame's fence, and it is destroyed once that fence has been signaled. acquireStagingBuffer
    // is the TransferSource specialization used by command-buffer update* commands.
    [[nodiscard]] RID acquireStagingBuffer(std::uint64_t size);
    void tagPendingStagingBuffers(VkFence fence);
    // Destroys transient buffers whose fence has been signaled. Called at frame boundaries.
    void collectStagingBuffers();

private:
    struct QueueFamilies {
        std::uint32_t graphics{};
        std::uint32_t present{};
        bool hasGraphics{};
        bool hasPresent{};
        [[nodiscard]] bool complete() const { return hasGraphics && hasPresent; }
    };

    // Per-family pool primitives. allocate_rid emplaces a concrete instance and returns its RID;
    // release_rid destroys it in place. create/destroy wrap these with validation and native
    // teardown (dedup tables, descriptor frees, view cascades). Bind groups are plain
    // VkDescriptorSet values, so their pool needs no wrapper type. buffer_release_rid is a public
    // IDevice override (the split buffer lifecycle), so it is not repeated here.
    void texture_release_rid(RID handle) { (void)textures_.release(handle); }
    void texture_view_release_rid(RID handle) { (void)textureViews_.release(handle); }
    void sampler_release_rid(RID handle) { (void)samplers_.release(handle); }
    void shader_release_rid(RID handle) { (void)shaders_.release(handle); }
    void pipeline_release_rid(RID handle) { (void)pipelines_.release(handle); }
    void bind_group_layout_release_rid(RID handle) { (void)bindGroupLayouts_.release(handle); }
    void bind_group_release_rid(RID handle) { (void)bindGroups_.release(handle); }

    [[nodiscard]] VulkanBuffer& requireBuffer(RID handle);
    [[nodiscard]] const VulkanBuffer& requireBuffer(RID handle) const;
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
    [[nodiscard]] VkPipelineLayout acquirePipelineLayout(const std::vector<RID>& key);
    void destroyPipelineLayoutsReferencing(RID handle);
    void clear();

    // Scratch buffer created for a command buffer staging upload; retired once the frame
    // that recorded the copy has completed.
    struct StagingBuffer {
        RID handle;
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

    // One pool per object family, storing concrete Vulkan instances directly (by value).
    HandlePool<VulkanBuffer, RID> buffers_;
    HandlePool<VulkanTexture, RID> textures_;
    HandlePool<VulkanTextureView, RID> textureViews_;
    HandlePool<VulkanSampler, RID> samplers_;
    HandlePool<VulkanShaderModule, RID> shaders_;
    HandlePool<VulkanGraphicsPipeline, RID> pipelines_;
    HandlePool<VulkanDescriptorSetLayout, RID> bindGroupLayouts_;
    HandlePool<VkDescriptorSet, RID> bindGroups_;
    HandlePool<VulkanCommandBuffer, RID> commandBuffers_;

    std::vector<StagingBuffer> pendingStagingBuffers_;
    // Samplers are pure value objects, so this map preserves identical-SamplerDesc sharing
    // (one VkSampler per distinct descriptor) on top of the sampler pool.
    std::unordered_map<SamplerDesc, RID, SamplerDescHash> samplerDedup_;
    // Device-level view dedup: texture RID -> (normalized view desc -> view RID).
    std::unordered_map<std::uint64_t,
                       std::unordered_map<TextureViewDesc, RID, TextureViewDescHash>>
        viewDedup_;
    std::unique_ptr<VulkanDescriptorAllocator> descriptorAllocator_;
    VkPipelineCache pipelineCache_{VK_NULL_HANDLE};
    std::unordered_map<PipelineLayoutKey, VkPipelineLayout, PipelineLayoutKeyHash> pipelineLayouts_;
};

} // namespace engine::rhi::vulkan

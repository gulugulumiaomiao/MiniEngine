#pragma once

#include "rhi/api/ResourceDesc.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine::rhi {

// Backend-agnostic device interface and global device singleton. It exposes only RID-based
// create / destroy / upload operations for every GPU object family plus abstract swapchain
// access; it contains no backend-native (e.g. Vulkan) types and no resource interface classes.
// Concrete backends own the per-type handle pools (storing instances directly) and expose
// native-handle resolution (VkImage/VkSampler/VkPipeline/...) as their own non-virtual methods.
//
// Command recording is not part of this interface: it lives in Command.h as free functions that
// take a command-buffer RID, and command-buffer creation/submission is a backend concern.
class IDevice {
public:
    // Global device singleton accessor. A concrete device registers itself as the active
    // instance on construction and clears it on destruction, so exactly one device is live at a
    // time. active() returns nullptr when no device has been constructed, letting callers test
    // availability before creating GPU resources. The command free functions resolve their
    // command-buffer RID through here, and layer-2 Texture/Sampler creation and the builtin
    // texture defaults resolve the device through here too.
    [[nodiscard]] static IDevice* active() { return activeSlot(); }

    // Process-wide monotonic device identity, assigned on construction and never reused. Lets a
    // resource robustly test "is the device I was created on still the active one" without
    // comparing raw pointers, whose addresses a freshly allocated device may recycle.
    [[nodiscard]] std::uint64_t uid() const { return uid_; }

    virtual ~IDevice() {
        if (activeSlot() == this)
            activeSlot() = nullptr;
    }

    // Buffers. Creation is split into three physical steps so a caller can reserve handles
    // before committing GPU memory: buffer_allocate_rid (handle only) -> buffer_allocate_memory
    // (VkBuffer + VMA allocation) -> buffer_upload (data). Destruction is the two-step reverse:
    // buffer_free_memory (release GPU memory, keep the handle) -> buffer_release_rid (recycle the
    // handle). buffer_create / buffer_destroy compose these for the common one-shot case.
    [[nodiscard]] virtual RID buffer_allocate_rid(const BufferDesc& desc) = 0;
    virtual void buffer_allocate_memory(RID handle) = 0;
    virtual void buffer_free_memory(RID handle) = 0;
    virtual void buffer_release_rid(RID handle) = 0;
    [[nodiscard]] virtual RID buffer_create(const BufferDesc& desc) = 0;
    virtual void buffer_destroy(RID handle) = 0;
    virtual void buffer_upload(RID destination,
                               std::span<const std::byte> data,
                               std::uint64_t offset = 0) = 0;
    // Transient (streaming) buffer: a host-visible buffer valid only for the current frame's
    // recording, reclaimed automatically by the backend once the submitting frame's fence
    // signals. Callers never free it; each acquire orphans the previous one. Backs
    // MeshUsage::Stream, whose data is regenerated every frame (orphan/rename semantics).
    [[nodiscard]] virtual RID buffer_acquire_transient(const BufferDesc& desc) = 0;

    // Textures and texture views. Views are deduped and owned at the device level.
    [[nodiscard]] virtual TextureFormatCapabilities texture_format_capabilities(
        PixelFormat, TextureTiling = TextureTiling::Optimal) const { return {}; }
    [[nodiscard]] virtual bool texture_is_supported(const TextureDesc&) const { return false; }
    // Returns an invalid RID for invalid or unsupported descriptions.
    [[nodiscard]] virtual RID texture_create(const TextureDesc& desc) = 0;
    virtual void texture_destroy(RID handle) = 0;
    // First upload of the complete mip chain into a single-sample 2D texture with
    // Sampled | TransferDestination usage. Regions contain tightly packed source
    // bytes; the backend inserts staging padding. Ends in ShaderRead state.
    virtual void texture_upload(RID destination,
                                std::span<const TextureUploadRegion> regions) = 0;
    [[nodiscard]] virtual RID texture_view_create(RID texture, const TextureViewDesc& desc) = 0;
    [[nodiscard]] virtual RID texture_default_view(RID texture) = 0;
    virtual void texture_view_destroy(RID handle) = 0;

    // Samplers (pure value objects; identical descriptors share one handle).
    [[nodiscard]] virtual RID sampler_create(const SamplerDesc& desc) = 0;
    virtual void sampler_destroy(RID handle) = 0;

    // Shader modules. allocate_rid / release_rid are the pool primitives (reserve a RID with a
    // null module, recycle a RID); create / destroy compose them with the VkShaderModule build and
    // teardown for the common one-shot case.
    [[nodiscard]] virtual RID shader_allocate_rid() = 0;
    [[nodiscard]] virtual RID shader_create(const ShaderDesc& desc) = 0;
    virtual void shader_release_rid(RID handle) = 0;
    virtual void shader_destroy(RID handle) = 0;

    // Graphics pipelines.
    [[nodiscard]] virtual RID pipeline_create(const GraphicsPipelineDesc& desc) = 0;
    virtual void pipeline_destroy(RID handle) = 0;

    // Raw render-pass/framebuffer lifecycle. Reuse and invalidation belong to the
    // render-graph layer. Retire GPU work before destroying these objects or their views.
    [[nodiscard]] virtual RID render_pass_create(const RenderPassDesc&) { return {}; }
    virtual void render_pass_destroy(RID) {}
    [[nodiscard]] virtual RID framebuffer_create(const FramebufferDesc&) { return {}; }
    virtual void framebuffer_destroy(RID) {}
    [[nodiscard]] virtual TextureViewInfo texture_view_info(RID) const { return {}; }

    // Descriptor set layouts, pipeline layouts and descriptor sets: the reflection-driven
    // uniform-binding system (engine layer UniformSetLayout / PipelineLayout / UniformSet).
    //
    // Set layouts use the two-step lifecycle (mirroring buffers): allocate_rid reserves a RID with
    // a null VkDescriptorSetLayout, build fills that reserved slot once the bindings are known
    // (UniformSetLayout records bindings between begin() and end()), destroy tears down and
    // recycles. Descriptor sets are pooled: allocate picks a pool bucketed by info.unorderedHash,
    // update writes descriptors, destroy frees the set back to its pool (destroying an emptied pool).
    [[nodiscard]] virtual RID descriptor_set_layout_allocate_rid() = 0;
    virtual void descriptor_set_layout_build(RID handle, const DescriptorSetLayoutDesc& desc) = 0;
    virtual void descriptor_set_layout_destroy(RID handle) = 0;
    [[nodiscard]] virtual RID pipeline_layout_create(const PipelineLayoutDesc& desc) = 0;
    virtual void pipeline_layout_destroy(RID handle) = 0;
    [[nodiscard]] virtual RID descriptor_set_allocate(const DescriptorSetAllocateInfo& info) = 0;
    virtual void descriptor_set_update(RID handle, std::span<const DescriptorWrite> writes) = 0;
    virtual void descriptor_set_destroy(RID handle) = 0;

    virtual void waitIdle() = 0;

protected:
    IDevice() : uid_(nextDeviceUid()) { activeSlot() = this; }

private:
    // Single global slot: a function-local static in an inline function has exactly one instance
    // across all translation units, so the abstract interface needs no separate definition TU.
    [[nodiscard]] static IDevice*& activeSlot() {
        static IDevice* device = nullptr;
        return device;
    }
    [[nodiscard]] static std::uint64_t nextDeviceUid() {
        static std::atomic<std::uint64_t> counter{0};
        return ++counter;
    }

    std::uint64_t uid_{};
};

// Frame acquire/present surface. Its concrete backend implementation internally manages the
// per-frame command buffers, queue submission, fences and semaphores; recording happens into the
// RID returned by commandBuffer(), which is valid between beginFrame() and endFrame().
class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    [[nodiscard]] virtual FrameStatus beginFrame() = 0;
    [[nodiscard]] virtual FrameStatus endFrame() = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    // The frame command buffer; valid between beginFrame() and endFrame(). Recording starts
    // inside beginFrame() and endFrame() seals and submits it to the device.
    [[nodiscard]] virtual RID commandBuffer() = 0;
    [[nodiscard]] virtual RID currentTexture() const = 0;
    [[nodiscard]] virtual RID currentTextureView() const = 0;
    [[nodiscard]] virtual ResourceState currentTextureState() const = 0;
    [[nodiscard]] virtual PixelFormat format() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual std::uint32_t frameIndex() const = 0;
    // Number of swapchain images; adjacent tooling sizes its per-image resources with this.
    [[nodiscard]] virtual std::uint32_t imageCount() const = 0;
};

} // namespace engine::rhi

// Global device singleton accessor macro, mirroring the ENGINE / ASSET_MANAGER style. Expands to
// the active IDevice* (nullptr when no device is live), so call sites use RHI_DEVICE->xxx() and
// null-check on construction/teardown paths. IDevice is abstract, so it cannot use the CRTP
// Singleton<T> base; the register-on-construct active() slot is its singleton form. No class other
// than the Renderer (the device owner) may hold an IDevice member.
#define RHI_DEVICE (::engine::rhi::IDevice::active())

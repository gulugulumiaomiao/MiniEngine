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

    // Buffers.
    [[nodiscard]] virtual RID buffer_create(const BufferDesc& desc) = 0;
    virtual void buffer_destroy(RID handle) = 0;
    virtual void buffer_upload(RID destination,
                               std::span<const std::byte> data,
                               std::uint64_t offset = 0) = 0;

    // Textures and texture views. Views are deduped and owned at the device level.
    [[nodiscard]] virtual RID texture_create(const TextureDesc& desc) = 0;
    virtual void texture_destroy(RID handle) = 0;
    virtual void texture_upload(RID destination,
                                std::span<const TextureUploadRegion> regions) = 0;
    [[nodiscard]] virtual RID texture_view_create(RID texture, const TextureViewDesc& desc) = 0;
    [[nodiscard]] virtual RID texture_default_view(RID texture) = 0;
    virtual void texture_view_destroy(RID handle) = 0;

    // Samplers (pure value objects; identical descriptors share one handle).
    [[nodiscard]] virtual RID sampler_create(const SamplerDesc& desc) = 0;
    virtual void sampler_destroy(RID handle) = 0;

    // Shader modules.
    [[nodiscard]] virtual RID shader_create(const ShaderDesc& desc) = 0;
    virtual void shader_destroy(RID handle) = 0;

    // Graphics pipelines.
    [[nodiscard]] virtual RID pipeline_create(const GraphicsPipelineDesc& desc) = 0;
    virtual void pipeline_destroy(RID handle) = 0;

    // Bind group layouts and bind groups.
    [[nodiscard]] virtual RID bind_group_layout_create(const BindGroupLayoutDesc& desc) = 0;
    virtual void bind_group_layout_destroy(RID handle) = 0;
    [[nodiscard]] virtual RID bind_group_create(const BindGroupDesc& desc) = 0;
    virtual void bind_group_destroy(RID handle) = 0;

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

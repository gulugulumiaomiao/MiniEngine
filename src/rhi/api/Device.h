#pragma once

#include "core/base/HandlePool.h"
#include "rhi/api/BindGroup.h"
#include "rhi/api/Buffer.h"
#include "rhi/api/PipelineDesc.h"
#include "rhi/api/RhiTypes.h"
#include "rhi/api/Sampler.h"
#include "rhi/api/ShaderModule.h"
#include "rhi/api/Texture.h"
#include "rhi/api/TextureView.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace engine::rhi {

class ICommandBuffer;
struct SubmitSync;
class IRHITexture;
class IRHITextureView;
class IRHISampler;

// Backend-agnostic device interface: only handle(RID)-based create/destroy/resolve and
// abstract RHI object access. It contains no backend-native (e.g. Vulkan) types; concrete
// backends expose native-handle resolution (VkImage/VkSampler/VkPipeline/...) as their own
// non-virtual methods.
class IDevice {
public:
    // Global device singleton accessor. A concrete device registers itself as the active
    // instance on construction and clears it on destruction, so exactly one device is live at a
    // time. active() returns nullptr when no device has been constructed, letting callers test
    // availability before creating GPU resources. Layer-2 Texture/Sampler creation and the
    // builtin texture defaults resolve the device through here (there is no texture manager to
    // inject it).
    [[nodiscard]] static IDevice* active() { return activeSlot(); }

    // Process-wide monotonic device identity, assigned on construction and never reused. Lets a
    // resource robustly test "is the device I was created on still the active one" without
    // comparing raw pointers, whose addresses a freshly allocated device may recycle.
    [[nodiscard]] std::uint64_t uid() const { return uid_; }

    virtual ~IDevice() {
        if (activeSlot() == this)
            activeSlot() = nullptr;
    }

    [[nodiscard]] virtual RID createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(RID handle) = 0;
    virtual void uploadBuffer(RID destination,
                              std::span<const std::byte> data,
                              std::uint64_t offset = 0) = 0;

    [[nodiscard]] virtual RID createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(RID handle) = 0;
    virtual void uploadTexture(RID destination,
                               std::span<const TextureUploadRegion> regions) = 0;
    [[nodiscard]] virtual RID createTextureView(RID texture,
                                                              const TextureViewDesc& desc) = 0;
    [[nodiscard]] virtual RID defaultTextureView(RID texture) = 0;
    virtual void destroyTextureView(RID handle) = 0;
    [[nodiscard]] virtual RID createSampler(const SamplerDesc& desc) = 0;
    virtual void destroySampler(RID handle) = 0;

    [[nodiscard]] virtual RID createShader(const ShaderDesc& desc) = 0;
    virtual void destroyShader(RID handle) = 0;

    [[nodiscard]] virtual RID
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void destroyGraphicsPipeline(RID handle) = 0;

    [[nodiscard]] virtual RID
    createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
    virtual void destroyBindGroupLayout(RID handle) = 0;
    [[nodiscard]] virtual RID createBindGroup(const BindGroupDesc& desc) = 0;
    virtual void destroyBindGroup(RID handle) = 0;

    // Command buffers are allocated from the device and submitted through it. A
    // command buffer must be in the Executable state (end() called) at submit time;
    // submitting a buffer that is still recording or was never begun is an error.
    [[nodiscard]] virtual std::unique_ptr<ICommandBuffer> createCommandBuffer() = 0;
    virtual void submitCommand(ICommandBuffer& command, const SubmitSync& sync) = 0;

    // RID registries for backend-created GPU objects. Concrete and shared by every backend: a
    // backend heap-creates its native object (e.g. VulkanTexture) and registers it here, so
    // IDevice is the single RID allocator while holding no backend-native types. The pools are
    // protected so a backend can clear them while its native device is still alive.
    [[nodiscard]] RID registerTexture(std::unique_ptr<IRHITexture> texture) {
        return textures_.insert(std::move(texture));
    }
    [[nodiscard]] virtual IRHITexture* resolveTextureResource(RID handle) {
        const auto* slot = textures_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    [[nodiscard]] virtual const IRHITexture* resolveTextureResource(RID handle) const {
        const auto* slot = textures_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    void releaseTexture(RID handle) { (void)textures_.release(handle); }

    [[nodiscard]] RID registerTextureView(std::unique_ptr<IRHITextureView> view) {
        return textureViews_.insert(std::move(view));
    }
    [[nodiscard]] IRHITextureView* resolveTextureViewResource(RID handle) {
        const auto* slot = textureViews_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    [[nodiscard]] const IRHITextureView* resolveTextureViewResource(RID handle) const {
        const auto* slot = textureViews_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    void releaseTextureView(RID handle) { (void)textureViews_.release(handle); }

    [[nodiscard]] RID registerSampler(std::unique_ptr<IRHISampler> sampler) {
        return samplers_.insert(std::move(sampler));
    }
    [[nodiscard]] IRHISampler* resolveSamplerResource(RID handle) {
        const auto* slot = samplers_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    [[nodiscard]] const IRHISampler* resolveSamplerResource(RID handle) const {
        const auto* slot = samplers_.find(handle);
        return slot ? slot->get() : nullptr;
    }
    void releaseSampler(RID handle) { (void)samplers_.release(handle); }

    virtual void waitIdle() = 0;

protected:
    IDevice() : uid_(nextDeviceUid()) { activeSlot() = this; }

    HandlePool<std::unique_ptr<IRHITexture>, RID> textures_;
    HandlePool<std::unique_ptr<IRHITextureView>, RID> textureViews_;
    HandlePool<std::unique_ptr<IRHISampler>, RID> samplers_;

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

} // namespace engine::rhi

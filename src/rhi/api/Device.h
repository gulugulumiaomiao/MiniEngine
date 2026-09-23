#pragma once

#include "rhi/api/PipelineDesc.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/api/RhiTypes.h"
#include "rhi/api/Sampler.h"
#include "rhi/api/Texture.h"
#include "rhi/api/TextureView.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace engine::rhi {

class ICommandBuffer;
struct SubmitSync;
class IRHITexture;

// Backend-agnostic device interface: only handle(RID)-based create/destroy/resolve and
// abstract RHI object access. It contains no backend-native (e.g. Vulkan) types; concrete
// backends expose native-handle resolution (VkImage/VkSampler/VkPipeline/...) as their own
// non-virtual methods.
class IDevice {
public:
    virtual ~IDevice() = default;

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
    [[nodiscard]] virtual RID defaultTextureView(RID texture) const = 0;
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

    // Backend-agnostic resolution: returns the abstract RHI texture object for a handle.
    [[nodiscard]] virtual IRHITexture* resolveTextureResource(RID handle) = 0;
    [[nodiscard]] virtual const IRHITexture* resolveTextureResource(RID handle) const = 0;

    virtual void waitIdle() = 0;
};

} // namespace engine::rhi

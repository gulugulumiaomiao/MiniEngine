#pragma once

#include "rhi/api/PipelineDesc.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/api/RhiTypes.h"
#include "rhi/api/Sampler.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace engine::rhi {

struct ResolvedPipeline {
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(BufferHandle handle) = 0;
    virtual void uploadBuffer(BufferHandle destination,
                              std::span<const std::byte> data,
                              std::uint64_t offset = 0) = 0;

    [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle handle) = 0;
    virtual void uploadTexture(TextureHandle destination,
                               std::span<const TextureUploadRegion> regions) = 0;
    [[nodiscard]] virtual TextureViewHandle createTextureView(const TextureViewDesc& desc) = 0;
    virtual void destroyTextureView(TextureViewHandle handle) = 0;
    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    virtual void destroySampler(SamplerHandle handle) = 0;

    [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual void destroyShader(ShaderHandle handle) = 0;

    [[nodiscard]] virtual GraphicsPipelineHandle
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void destroyGraphicsPipeline(GraphicsPipelineHandle handle) = 0;

    [[nodiscard]] virtual BindGroupLayoutHandle
    createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
    virtual void destroyBindGroupLayout(BindGroupLayoutHandle handle) = 0;
    [[nodiscard]] virtual BindGroupHandle createBindGroup(const BindGroupDesc& desc) = 0;
    virtual void destroyBindGroup(BindGroupHandle handle) = 0;

    [[nodiscard]] virtual VkDevice device() const = 0;
    [[nodiscard]] virtual VkBuffer resolveBuffer(BufferHandle handle) const = 0;
    [[nodiscard]] virtual VkImage resolveTexture(TextureHandle handle) const = 0;
    [[nodiscard]] virtual VkImageView resolveTextureView(TextureViewHandle handle) const = 0;
    [[nodiscard]] virtual ResolvedPipeline resolvePipeline(GraphicsPipelineHandle handle) const = 0;
    [[nodiscard]] virtual VkDescriptorSet resolveBindGroup(BindGroupHandle handle) const = 0;

    virtual void waitIdle() = 0;
};

} // namespace engine::rhi

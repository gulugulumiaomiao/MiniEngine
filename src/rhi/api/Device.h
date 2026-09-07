#pragma once

#include "rhi/api/PipelineDesc.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/api/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace engine::rhi {

class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(BufferHandle handle) = 0;
    virtual void uploadBuffer(BufferHandle destination,
                              std::span<const std::byte> data,
                              std::uint64_t offset = 0) = 0;

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

    virtual void waitIdle() = 0;
};

} // namespace engine::rhi

#include "render/pipeline/passes/RenderPassUtils.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/renderer/DrawBatcher.h"
#include "render/shader/Shader.h"

namespace engine {
namespace {

rhi::CullMode toRhi(CullMode mode) {
    switch (mode) {
    case CullMode::Off: return rhi::CullMode::None;
    case CullMode::Front: return rhi::CullMode::Front;
    case CullMode::Back: return rhi::CullMode::Back;
    }
    return rhi::CullMode::None;
}

rhi::FrontFace toRhi(FrontFace face) {
    return face == FrontFace::Clockwise ? rhi::FrontFace::Clockwise
                                        : rhi::FrontFace::CounterClockwise;
}

rhi::CompareOp toRhi(DepthCompare compare) {
    switch (compare) {
    case DepthCompare::Never: return rhi::CompareOp::Never;
    case DepthCompare::Less: return rhi::CompareOp::Less;
    case DepthCompare::LessEqual: return rhi::CompareOp::LessEqual;
    case DepthCompare::Equal: return rhi::CompareOp::Equal;
    case DepthCompare::Greater: return rhi::CompareOp::Greater;
    case DepthCompare::GreaterEqual: return rhi::CompareOp::GreaterEqual;
    case DepthCompare::Always: return rhi::CompareOp::Always;
    }
    return rhi::CompareOp::Always;
}

rhi::BlendMode toRhi(BlendMode mode) {
    switch (mode) {
    case BlendMode::Off: return rhi::BlendMode::Off;
    case BlendMode::Alpha: return rhi::BlendMode::Alpha;
    case BlendMode::Additive: return rhi::BlendMode::Additive;
    case BlendMode::PremultipliedAlpha: return rhi::BlendMode::PremultipliedAlpha;
    }
    return rhi::BlendMode::Off;
}

rhi::PrimitiveTopology toRhi(PrimitiveTopology topology) {
    return topology == PrimitiveTopology::TriangleList ? rhi::PrimitiveTopology::TriangleList
                                                       : rhi::PrimitiveTopology::LineList;
}

rhi::FillMode toRhi(FillMode mode) {
    return mode == FillMode::Solid ? rhi::FillMode::Solid : rhi::FillMode::Wireframe;
}

rhi::ColorWriteMask toRhiColorMask(std::string_view mask) {
    rhi::ColorWriteMask result = rhi::ColorWriteMask::None;
    if (mask.find('R') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Red;
    if (mask.find('G') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Green;
    if (mask.find('B') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Blue;
    if (mask.find('A') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Alpha;
    return result;
}

void applyRenderState(rhi::ICommandBuffer& commandBuffer, const RenderStateDesc& state) {
    commandBuffer.setPrimitiveTopology(toRhi(state.topology));
    commandBuffer.setFillMode(toRhi(state.fill));
    commandBuffer.setCullMode(toRhi(state.cull));
    commandBuffer.setFrontFace(toRhi(state.frontFace));
    commandBuffer.setDepthTestEnable(state.depthTest != DepthCompare::Always || state.depthWrite);
    commandBuffer.setDepthWriteEnable(state.depthWrite);
    commandBuffer.setDepthCompareOp(toRhi(state.depthTest));
    commandBuffer.setBlendState(toRhi(state.blend));
    commandBuffer.setColorWriteMask(toRhiColorMask(state.colorMask));
}

} // namespace

void drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::BindGroupHandle globalBindGroup,
                       rhi::ICommandBuffer& commandBuffer) {
    DrawBatcher batcher;
    BatchedDrawList batched = batcher.build(items);
    if (batched.batches.empty()) {
        return;
    }

    // Upload the instance rows this pass needs. Each pass owns a disjoint
    // region of the per-frame instance table; other in-flight frames have
    // their own table buffers.
    const std::uint32_t baseSlot =
        FRAME_GPU_MANAGER.reserveInstanceRegion(frameIndex,
                                                static_cast<std::uint32_t>(
                                                    batched.instanceRows.size()));
    FRAME_GPU_MANAGER.uploadInstanceRegion(frameIndex, baseSlot, batched.instanceRows);

    rhi::GraphicsPipelineHandle boundPipeline;
    rhi::BindGroupHandle boundMaterial;
    rhi::BindGroupHandle boundGlobal;
    const ShaderPass* boundShaderPass = nullptr;
    for (const DrawBatch& batch : batched.batches) {
        if (batch.pipeline != boundPipeline) {
            commandBuffer.bindPipeline(batch.pipeline);
            commandBuffer.bindGroup(0, sceneBindGroup);
            boundPipeline = batch.pipeline;
            boundGlobal = {}; // Pipeline change may require set 2 rebind.
        }
        if (batch.shaderPass != boundShaderPass) {
            applyRenderState(commandBuffer, batch.shaderPass->renderState());
            boundShaderPass = batch.shaderPass;
        }
        if (batch.materialBindGroup != boundMaterial) {
            commandBuffer.bindGroup(1, batch.materialBindGroup);
            boundMaterial = batch.materialBindGroup;
        }
        if (globalBindGroup && globalBindGroup != boundGlobal) {
            commandBuffer.bindGroup(2, globalBindGroup);
            boundGlobal = globalBindGroup;
        }
        for (const DrawItem::VertexBuffer& vertex : batch.vertexBuffers) {
            commandBuffer.bindVertexBuffer(vertex.binding, vertex.buffer);
        }
        commandBuffer.bindIndexBuffer(batch.indexBuffer, 0, batch.indexFormat);
        commandBuffer.drawIndexed({.indexCount = batch.indexCount,
                             .instanceCount = batch.instanceCount,
                             .firstIndex = batch.firstIndex,
                             .vertexOffset = batch.vertexOffset,
                             .firstInstance = baseSlot + batch.firstInstance});
    }
}

} // namespace engine

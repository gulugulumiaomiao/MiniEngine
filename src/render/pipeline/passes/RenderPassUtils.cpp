#include "render/pipeline/passes/RenderPassUtils.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/global_uniform/GlobalUniformGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RgTypes.h"
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

void applyRenderState(rhi::RID commandBuffer, const RenderState& state) {
    rhi::setPrimitiveTopology(commandBuffer, toRhi(state.topology));
    rhi::setFillMode(commandBuffer, toRhi(state.fill));
    rhi::setCullMode(commandBuffer, toRhi(state.cull));
    rhi::setFrontFace(commandBuffer, toRhi(state.frontFace));
    rhi::setDepthTestEnable(commandBuffer,
                            state.depthTest != DepthCompare::Always || state.depthWrite);
    rhi::setDepthWriteEnable(commandBuffer, state.depthWrite);
    rhi::setDepthCompareOp(commandBuffer, toRhi(state.depthTest));
    rhi::setBlendState(commandBuffer, toRhi(state.blend));
    rhi::setColorWriteMask(commandBuffer, toRhiColorMask(state.colorMask));
}

} // namespace

void filterDrawItems(const RenderContext& context, const DrawList& drawList,
                     RenderPhase phase, const DrawFilter& filter, std::vector<DrawItem>& result) {
    result.clear();
    result.reserve(drawList.items.size());
    const auto& objects = context.scene().objects();
    for (const auto& item : drawList.items) {
        if (item.renderPhase != phase) continue;
        const auto index = item.sourceObjectIndex();
        const auto layer = index < objects.size() ? objects[index].layerMask : 0xFFFFFFFFU;
        if (filter.accepts(item, layer)) result.push_back(item);
    }
}

void drawRasterItems(const RgExecuteContext& execution, std::span<const DrawItem> items) {
    const auto area = execution.renderArea;
    const auto cmd = execution.commandBuffer;
    const auto frameIndex = execution.frame.frameIndex();
    rhi::setViewport(cmd, {static_cast<float>(area.x), static_cast<float>(area.y),
        static_cast<float>(area.width), static_cast<float>(area.height), 0.0F, 1.0F});
    rhi::setScissor(cmd, area);
    drawFilteredItems(frameIndex, items, FRAME_GPU_MANAGER.sceneUniformSet(frameIndex),
                      GLOBAL_UNIFORM_GPU_MANAGER.resolve(frameIndex), cmd);
}

void drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       const Ref<UniformSet>& scene,
                       const Ref<UniformSet>& global,
                       rhi::RID commandBuffer) {
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

    const rhi::RID sceneSet = scene ? scene->descriptorSet() : rhi::RID{};
    const rhi::RID globalSet = global ? global->descriptorSet() : rhi::RID{};
    rhi::RID boundPipeline;
    rhi::RID boundMaterial;
    rhi::RID boundGlobal;
    const ShaderPass* boundShaderPass = nullptr;
    for (const DrawBatch& batch : batched.batches) {
        if (batch.pipeline != boundPipeline) {
            rhi::bindPipeline(commandBuffer, batch.pipeline);
            if (sceneSet && batch.programSetCount > 0)
                rhi::bindDescriptorSet(commandBuffer, 0, sceneSet);
            boundPipeline = batch.pipeline;
            boundGlobal = {}; // Pipeline change may require set 2 rebind.
        }
        if (batch.shaderPass != boundShaderPass) {
            applyRenderState(commandBuffer, batch.shaderPass->renderState());
            boundShaderPass = batch.shaderPass;
        }
        const rhi::RID materialSet =
            batch.materialSet ? batch.materialSet->descriptorSet() : rhi::RID{};
        if (materialSet && batch.programSetCount > 1 && materialSet != boundMaterial) {
            rhi::bindDescriptorSet(commandBuffer, 1, materialSet);
            boundMaterial = materialSet;
        }
        if (globalSet && batch.programSetCount > 2 && globalSet != boundGlobal) {
            rhi::bindDescriptorSet(commandBuffer, 2, globalSet);
            boundGlobal = globalSet;
        }
        for (const DrawItem::VertexBuffer& vertex : batch.vertexBuffers) {
            rhi::bindVertexBuffer(commandBuffer, vertex.binding, vertex.buffer);
        }
        rhi::bindIndexBuffer(commandBuffer, batch.indexBuffer, 0, batch.indexFormat);
        rhi::drawIndexed(commandBuffer,
                         {.indexCount = batch.indexCount,
                          .instanceCount = batch.instanceCount,
                          .firstIndex = batch.firstIndex,
                          .vertexOffset = batch.vertexOffset,
                          .firstInstance = baseSlot + batch.firstInstance});
    }
}

} // namespace engine

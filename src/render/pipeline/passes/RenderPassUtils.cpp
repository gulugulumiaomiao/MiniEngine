#include "render/pipeline/passes/RenderPassUtils.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/renderer/DrawBatcher.h"

namespace engine {

void drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::IGraphicsCommandEncoder& encoder) {
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
    for (const DrawBatch& batch : batched.batches) {
        if (batch.pipeline != boundPipeline) {
            encoder.bindPipeline(batch.pipeline);
            encoder.bindGroup(0, sceneBindGroup);
            boundPipeline = batch.pipeline;
        }
        if (batch.materialBindGroup != boundMaterial) {
            encoder.bindGroup(1, batch.materialBindGroup);
            boundMaterial = batch.materialBindGroup;
        }
        for (const DrawItem::VertexBuffer& vertex : batch.vertexBuffers) {
            encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
        }
        encoder.bindIndexBuffer(batch.indexBuffer, 0, batch.indexFormat);
        encoder.drawIndexed({.indexCount = batch.indexCount,
                             .instanceCount = batch.instanceCount,
                             .firstIndex = batch.firstIndex,
                             .vertexOffset = batch.vertexOffset,
                             .firstInstance = baseSlot + batch.firstInstance});
    }
}

} // namespace engine

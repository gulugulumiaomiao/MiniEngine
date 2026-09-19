#include "render/pipeline/passes/RenderPassUtils.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/queue/RenderQueue.h"
#include "render/renderer/DrawBatcher.h"
#include "render/renderer/RenderItems.h"

#include <optional>

namespace engine {

std::vector<DrawItem> collectPassItems(const DrawList& drawList,
                                       RenderPhase phase,
                                       const DrawFilter& filter) {
    std::vector<DrawItem> result;
    for (const auto& [queue, items] : drawList.groups) {
        if (!filter.queueRange.contains(queue)) {
            continue;
        }
        for (const DrawItem& item : items) {
            if (item.renderPhase != phase) {
                continue;
            }
            if (filter.accepts(item, item.layerMask)) {
                result.push_back(item);
            }
        }
    }
    return result;
}

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

    RenderItemList renderItems;
    renderItems.reserve(batched.batches.size());
    for (const DrawBatch& batch : batched.batches) {
        RenderItem item;
        item.pipeline = batch.pipeline;
        item.drawState = batch.drawState;
        item.materialBindGroup = batch.materialBindGroup;
        item.vertexBuffers.reserve(batch.vertexBuffers.size());
        for (const DrawItem::VertexBuffer& vertex : batch.vertexBuffers) {
            item.vertexBuffers.push_back({vertex.binding, vertex.buffer, 0});
        }
        item.indexBuffer = batch.indexBuffer;
        item.indexFormat = batch.indexFormat;
        item.arguments = {.indexCount = batch.indexCount,
                          .instanceCount = batch.instanceCount,
                          .firstIndex = batch.firstIndex,
                          .vertexOffset = batch.vertexOffset,
                          .firstInstance = baseSlot + batch.firstInstance};
        renderItems.push_back(std::move(item));
    }

    rhi::GraphicsPipelineHandle boundPipeline;
    rhi::BindGroupHandle boundMaterial;
    std::optional<rhi::DrawStateDesc> boundDrawState;
    for (const RenderItem& item : renderItems) {
        if (item.pipeline != boundPipeline) {
            encoder.bindPipeline(item.pipeline);
            encoder.bindGroup(0, sceneBindGroup);
            boundPipeline = item.pipeline;
        }
        if (item.materialBindGroup != boundMaterial) {
            encoder.bindGroup(1, item.materialBindGroup);
            boundMaterial = item.materialBindGroup;
        }
        if (!boundDrawState || *boundDrawState != item.drawState) {
            encoder.setDrawState(item.drawState);
            boundDrawState = item.drawState;
        }
        for (const RenderItem::VertexBuffer& vertex : item.vertexBuffers) {
            encoder.bindVertexBuffer(vertex.binding, vertex.buffer, vertex.offset);
        }
        encoder.bindIndexBuffer(item.indexBuffer, item.indexBufferOffset, item.indexFormat);
        encoder.drawIndexed(item.arguments);
    }
}

} // namespace engine

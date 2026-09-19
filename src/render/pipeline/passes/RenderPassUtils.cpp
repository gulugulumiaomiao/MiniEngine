#include "render/pipeline/passes/RenderPassUtils.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/queue/RenderQueue.h"
#include "render/renderer/DrawBatcher.h"
#include "render/renderer/RenderItems.h"
#include "render/scene/RenderScene.h"

#include <iterator>
#include <optional>
#include <string>

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

void sortForwardItems(std::vector<DrawItem>& items, const RenderScene& scene) {
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    opaque.reserve(items.size());
    transparent.reserve(items.size());
    for (DrawItem& item : items) {
        (RenderQueueRange::opaque().contains(item.renderQueue) ? opaque : transparent)
            .push_back(std::move(item));
    }

    DrawSorter sorter;
    sorter.sort(opaque,
                SortingCriteria::RenderQueue | SortingCriteria::Pipeline |
                    SortingCriteria::Material | SortingCriteria::Mesh,
                scene);
    sorter.sort(transparent, SortingCriteria::BackToFront, scene);

    items.clear();
    items.insert(items.end(),
                 std::make_move_iterator(opaque.begin()),
                 std::make_move_iterator(opaque.end()));
    items.insert(items.end(),
                 std::make_move_iterator(transparent.begin()),
                 std::make_move_iterator(transparent.end()));
}

DrawSubmissionStats drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::IGraphicsCommandEncoder& encoder) {
    DrawBatcher batcher;
    BatchedRenderItems batched = batcher.build(items);
    if (batched.items.empty()) {
        return {};
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
    std::optional<rhi::DrawStateDesc> boundDrawState;
    std::optional<int> labeledQueue;
    for (RenderItem& item : batched.items) {
        item.arguments.firstInstance += baseSlot;
        if (!labeledQueue || *labeledQueue != item.renderQueue) {
            if (labeledQueue) {
                encoder.endDebugLabel();
            }
            encoder.beginDebugLabel("RenderQueue " + std::to_string(item.renderQueue),
                                    {0.35F, 0.8F, 0.45F, 1.0F});
            labeledQueue = item.renderQueue;
        }
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
            encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
        }
        encoder.bindIndexBuffer(item.indexBuffer, 0, item.indexFormat);
        encoder.drawIndexed(item.arguments);
    }
    if (labeledQueue) {
        encoder.endDebugLabel();
    }
    return {
        .sourceItems = batched.itemCount,
        .renderItems = batched.items.size(),
        .gpuInstancedDraws = batched.gpuInstancedBatchCount,
    };
}

} // namespace engine

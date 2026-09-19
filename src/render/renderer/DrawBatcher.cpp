#include "render/renderer/DrawBatcher.h"

#include "render/material/Material.h"

#include <utility>

namespace engine {

bool DrawBatcher::sameBatchState(const DrawItem& lhs, const DrawItem& rhs) {
    if (lhs.batchingMode != MaterialBatchingMode::GpuInstancing ||
        rhs.batchingMode != MaterialBatchingMode::GpuInstancing ||
        lhs.renderQueue != rhs.renderQueue || lhs.pipeline != rhs.pipeline ||
        lhs.drawState != rhs.drawState || lhs.materialBindGroup != rhs.materialBindGroup ||
        lhs.indexBuffer != rhs.indexBuffer || lhs.indexFormat != rhs.indexFormat ||
        lhs.vertexBuffers.size() != rhs.vertexBuffers.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.vertexBuffers.size(); ++i) {
        if (!(lhs.vertexBuffers[i] == rhs.vertexBuffers[i]))
            return false;
    }
    const rhi::DrawIndexedArguments& a = lhs.arguments;
    const rhi::DrawIndexedArguments& b = rhs.arguments;
    return a.indexCount == b.indexCount && a.firstIndex == b.firstIndex &&
           a.vertexOffset == b.vertexOffset;
}

BatchedRenderItems DrawBatcher::build(std::span<const DrawItem> items) {
    BatchedRenderItems result;
    result.itemCount = items.size();

    const DrawItem* previous = nullptr;
    for (const DrawItem& item : items) {
        const std::uint32_t instanceSlot = static_cast<std::uint32_t>(result.instanceRows.size());
        result.instanceRows.push_back(item.arguments.firstInstance);

        const bool extendsBatch = previous != nullptr &&
                                  result.items.back().arguments.instanceCount <
                                      limits_.maxGpuInstancesPerDraw &&
                                  sameBatchState(*previous, item);
        if (extendsBatch) {
            ++result.items.back().arguments.instanceCount;
        } else {
            RenderItem renderItem;
            renderItem.renderQueue = item.renderQueue;
            renderItem.pipeline = item.pipeline;
            renderItem.drawState = item.drawState;
            renderItem.materialBindGroup = item.materialBindGroup;
            renderItem.vertexBuffers.reserve(item.vertexBuffers.size());
            for (const DrawItem::VertexBuffer& vertex : item.vertexBuffers) {
                renderItem.vertexBuffers.push_back({vertex.binding, vertex.buffer});
            }
            renderItem.indexBuffer = item.indexBuffer;
            renderItem.indexFormat = item.indexFormat;
            renderItem.arguments = {.indexCount = item.arguments.indexCount,
                                    .instanceCount = 1,
                                    .firstIndex = item.arguments.firstIndex,
                                    .vertexOffset = item.arguments.vertexOffset,
                                    .firstInstance = instanceSlot};
            result.items.push_back(std::move(renderItem));
        }
        previous = &item;
    }
    for (const RenderItem& item : result.items) {
        if (item.arguments.instanceCount > 1) {
            ++result.gpuInstancedBatchCount;
        }
    }
    return result;
}

} // namespace engine

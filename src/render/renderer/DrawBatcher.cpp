#include "render/renderer/DrawBatcher.h"

namespace engine {

bool DrawBatcher::sameBatchState(const DrawItem& lhs, const DrawItem& rhs) {
    if (lhs.pipeline != rhs.pipeline || lhs.materialBindGroup != rhs.materialBindGroup ||
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

BatchedDrawList DrawBatcher::build(std::span<const DrawItem> items) {
    BatchedDrawList result;
    result.itemCount = items.size();

    const DrawItem* previous = nullptr;
    for (const DrawItem& item : items) {
        const std::uint32_t instanceSlot = static_cast<std::uint32_t>(result.instanceRows.size());
        result.instanceRows.push_back(item.arguments.firstInstance);

        const bool extendsBatch = previous != nullptr &&
                                  result.batches.back().instanceCount <
                                      std::numeric_limits<std::uint32_t>::max() &&
                                  sameBatchState(*previous, item);
        if (extendsBatch) {
            ++result.batches.back().instanceCount;
        } else {
            result.batches.push_back({
                .pipeline = item.pipeline,
                .materialBindGroup = item.materialBindGroup,
                .vertexBuffers = item.vertexBuffers,
                .indexBuffer = item.indexBuffer,
                .indexFormat = item.indexFormat,
                .indexCount = item.arguments.indexCount,
                .firstIndex = item.arguments.firstIndex,
                .vertexOffset = item.arguments.vertexOffset,
                .firstInstance = instanceSlot,
                .instanceCount = 1,
            });
        }
        previous = &item;
    }
    return result;
}

} // namespace engine

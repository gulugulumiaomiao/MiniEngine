#pragma once

#include "render/renderer/DrawList.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

// One instanced draw call: N consecutive draw items that share pipeline,
// material bind group, vertex/index buffers and index range collapsed into a
// single drawIndexed invocation.
struct DrawBatch {
    rhi::GraphicsPipelineHandle pipeline;
    rhi::BindGroupHandle materialBindGroup;
    std::vector<DrawItem::VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    rhi::IndexFormat indexFormat{rhi::IndexFormat::UInt32};
    std::uint32_t indexCount{};
    std::uint32_t firstIndex{};
    std::int32_t vertexOffset{};
    std::uint32_t firstInstance{};  // instance slot inside the per-frame instance table
    std::uint32_t instanceCount{};  // number of merged draw items
};

// Result of batching a sorted draw list.
struct BatchedDrawList {
    std::vector<DrawBatch> batches;
    // Object row for every instance slot; batches reference rows through
    // firstInstance..firstInstance+instanceCount-1 into this table.
    std::vector<std::uint32_t> instanceRows;
    std::size_t itemCount{}; // number of source draw items covered by the batches
};

// Merges consecutive draw items with identical GPU state into instanced
// batches. Items are assumed to be sorted beforehand; only adjacent items are
// merged so the caller-controlled ordering (queue, transparency) is preserved.
class DrawBatcher final {
public:
    [[nodiscard]] BatchedDrawList build(std::span<const DrawItem> items);

private:
    [[nodiscard]] static bool sameBatchState(const DrawItem& lhs, const DrawItem& rhs);
};

} // namespace engine

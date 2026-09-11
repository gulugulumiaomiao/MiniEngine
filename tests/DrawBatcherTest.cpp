#include "render/renderer/DrawBatcher.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

engine::DrawItem makeItem(std::uint32_t pipelineIndex,
                          std::uint32_t materialBindGroupIndex,
                          std::uint32_t indexCount,
                          std::uint32_t firstIndex,
                          std::int32_t vertexOffset,
                          std::uint32_t objectRow,
                          std::uint32_t indexBufferIndex = 1,
                          engine::rhi::IndexFormat indexFormat = engine::rhi::IndexFormat::UInt32) {
    engine::DrawItem item;
    item.pipeline = engine::rhi::GraphicsPipelineHandle{pipelineIndex, 1};
    item.materialBindGroup = engine::rhi::BindGroupHandle{materialBindGroupIndex, 1};
    item.indexBuffer = engine::rhi::BufferHandle{indexBufferIndex, 1};
    item.indexFormat = indexFormat;
    item.arguments = {.indexCount = indexCount,
                      .instanceCount = 1,
                      .firstIndex = firstIndex,
                      .vertexOffset = vertexOffset,
                      .firstInstance = objectRow};
    return item;
}

} // namespace

int main() {
    using namespace engine;

    // Consecutive items with identical state merge into a single batch.
    {
        DrawBatcher batcher;
        const std::vector<DrawItem> items{
            makeItem(1, 1, 60, 0, 0, 0),
            makeItem(1, 1, 60, 0, 0, 1),
            makeItem(1, 1, 60, 0, 0, 2),
        };
        const BatchedDrawList batched = batcher.build(items);

        assert(batched.batches.size() == 1);
        assert(batched.batches[0].instanceCount == 3);
        assert(batched.batches[0].firstInstance == 0);
        assert(batched.batches[0].indexCount == 60);
        assert(batched.itemCount == 3);
        assert((batched.instanceRows == std::vector<std::uint32_t>{0, 1, 2}));
    }

    // Different materials / pipelines / index ranges break batches.
    {
        DrawBatcher batcher;
        const std::vector<DrawItem> items{
            makeItem(1, 1, 60, 0, 0, 0),
            makeItem(1, 2, 60, 0, 0, 1),  // material change
            makeItem(2, 2, 60, 0, 0, 2),  // pipeline change
            makeItem(2, 2, 30, 0, 0, 3),  // index count change
            makeItem(2, 2, 30, 60, 0, 4), // first index change
        };
        const BatchedDrawList batched = batcher.build(items);

        assert(batched.batches.size() == 5);
        for (const DrawBatch& batch : batched.batches)
            assert(batch.instanceCount == 1);
        assert((batched.instanceRows == std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
    }

    // A A B A A produces three batches; the interleaved state splits the run.
    {
        DrawBatcher batcher;
        const std::vector<DrawItem> items{
            makeItem(1, 1, 60, 0, 0, 0),
            makeItem(1, 1, 60, 0, 0, 1),
            makeItem(1, 2, 60, 0, 0, 2), // B
            makeItem(1, 1, 60, 0, 0, 3),
            makeItem(1, 1, 60, 0, 0, 4),
        };
        const BatchedDrawList batched = batcher.build(items);

        assert(batched.batches.size() == 3);
        assert(batched.batches[0].instanceCount == 2);
        assert(batched.batches[1].instanceCount == 1);
        assert(batched.batches[2].instanceCount == 2);
        assert(batched.batches[2].firstInstance == 3);
        assert((batched.instanceRows == std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
    }

    // Vertex/index buffer and format changes break batches.
    {
        DrawBatcher batcher;
        std::vector<DrawItem> items{
            makeItem(1, 1, 60, 0, 0, 0),
            makeItem(1, 1, 60, 0, 0, 1, /*indexBufferIndex=*/2), // index buffer change
        };
        items[1].vertexBuffers.push_back({0, rhi::BufferHandle{5, 1}});

        std::vector<DrawItem> formatChange{
            makeItem(1, 1, 60, 0, 0, 0),
            makeItem(1, 1, 60, 0, 0, 1, 1, engine::rhi::IndexFormat::UInt16),
        };
        formatChange[0].vertexBuffers.push_back({0, rhi::BufferHandle{5, 1}});
        formatChange[1].vertexBuffers.push_back({0, rhi::BufferHandle{5, 1}});
        formatChange[1].vertexBuffers[0].buffer = rhi::BufferHandle{9, 1}; // VB change

        const BatchedDrawList batchedA = batcher.build(items);
        assert(batchedA.batches.size() == 2); // size mismatch + index buffer mismatch

        const BatchedDrawList batchedB = batcher.build(formatChange);
        assert(batchedB.batches.size() == 2); // format + vertex buffer mismatch
    }

    // Empty input yields no batches.
    {
        DrawBatcher batcher;
        const BatchedDrawList batched = batcher.build({});
        assert(batched.batches.empty());
        assert(batched.instanceRows.empty());
        assert(batched.itemCount == 0);
    }

    return 0;
}

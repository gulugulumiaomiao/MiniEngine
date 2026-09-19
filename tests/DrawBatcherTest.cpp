#include "render/material/Material.h"
#include "render/renderer/DrawBatcher.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace engine {
namespace {

DrawItem makeItem(std::uint32_t pipelineIndex,
                  std::uint32_t materialBindGroupIndex,
                  std::uint32_t indexCount,
                  std::uint32_t firstIndex,
                  std::int32_t vertexOffset,
                  std::uint32_t objectRow,
                  std::uint32_t indexBufferIndex = 1,
                  rhi::IndexFormat indexFormat = rhi::IndexFormat::UInt32) {
    DrawItem item;
    item.pipeline = {pipelineIndex, 1};
    item.materialBindGroup = {materialBindGroupIndex, 1};
    item.indexBuffer = {indexBufferIndex, 1};
    item.indexFormat = indexFormat;
    item.batchingMode = MaterialBatchingMode::GpuInstancing;
    item.arguments = {.indexCount = indexCount,
                      .instanceCount = 1,
                      .firstIndex = firstIndex,
                      .vertexOffset = vertexOffset,
                      .firstInstance = objectRow};
    return item;
}

TEST(DrawBatcherTest, MergesConsecutiveCompatibleItems) {
    const std::vector items{makeItem(1, 1, 60, 0, 0, 0),
                            makeItem(1, 1, 60, 0, 0, 1),
                            makeItem(1, 1, 60, 0, 0, 2)};
    const BatchedRenderItems batched = DrawBatcher{}.build(items);

    ASSERT_EQ(batched.items.size(), 1U);
    EXPECT_EQ(batched.items[0].arguments.instanceCount, 3U);
    EXPECT_EQ(batched.items[0].arguments.firstInstance, 0U);
    EXPECT_EQ(batched.items[0].arguments.indexCount, 60U);
    EXPECT_EQ(batched.itemCount, 3U);
    EXPECT_EQ(batched.instanceRows, (std::vector<std::uint32_t>{0, 1, 2}));
}

TEST(DrawBatcherTest, StateAndGeometryChangesSplitItems) {
    const std::vector items{makeItem(1, 1, 60, 0, 0, 0),
                            makeItem(1, 2, 60, 0, 0, 1),
                            makeItem(2, 2, 60, 0, 0, 2),
                            makeItem(2, 2, 30, 0, 0, 3),
                            makeItem(2, 2, 30, 60, 0, 4)};
    const BatchedRenderItems batched = DrawBatcher{}.build(items);

    ASSERT_EQ(batched.items.size(), 5U);
    for (const RenderItem& item : batched.items)
        EXPECT_EQ(item.arguments.instanceCount, 1U);
    EXPECT_EQ(batched.instanceRows, (std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
}

TEST(DrawBatcherTest, DoesNotMergeAcrossAnInterleavedState) {
    const std::vector items{makeItem(1, 1, 60, 0, 0, 0),
                            makeItem(1, 1, 60, 0, 0, 1),
                            makeItem(1, 2, 60, 0, 0, 2),
                            makeItem(1, 1, 60, 0, 0, 3),
                            makeItem(1, 1, 60, 0, 0, 4)};
    const BatchedRenderItems batched = DrawBatcher{}.build(items);

    ASSERT_EQ(batched.items.size(), 3U);
    EXPECT_EQ(batched.items[0].arguments.instanceCount, 2U);
    EXPECT_EQ(batched.items[1].arguments.instanceCount, 1U);
    EXPECT_EQ(batched.items[2].arguments.instanceCount, 2U);
    EXPECT_EQ(batched.items[2].arguments.firstInstance, 3U);
}

TEST(DrawBatcherTest, BufferAndIndexFormatChangesSplitItems) {
    std::vector<DrawItem> indexBufferChange{makeItem(1, 1, 60, 0, 0, 0),
                                             makeItem(1, 1, 60, 0, 0, 1, 2)};
    indexBufferChange[1].vertexBuffers.push_back({0, {5, 1}});

    std::vector<DrawItem> formatAndVertexChange{
        makeItem(1, 1, 60, 0, 0, 0),
        makeItem(1, 1, 60, 0, 0, 1, 1, rhi::IndexFormat::UInt16)};
    formatAndVertexChange[0].vertexBuffers.push_back({0, {5, 1}});
    formatAndVertexChange[1].vertexBuffers.push_back({0, {9, 1}});

    EXPECT_EQ(DrawBatcher{}.build(indexBufferChange).items.size(), 2U);
    EXPECT_EQ(DrawBatcher{}.build(formatAndVertexChange).items.size(), 2U);
}

TEST(DrawBatcherTest, EmptyInputProducesNoRenderItems) {
    const BatchedRenderItems batched = DrawBatcher{}.build({});
    EXPECT_TRUE(batched.items.empty());
    EXPECT_TRUE(batched.instanceRows.empty());
    EXPECT_EQ(batched.itemCount, 0U);
}

} // namespace
} // namespace engine

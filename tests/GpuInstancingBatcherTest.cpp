#include "render/material/Material.h"
#include "render/renderer/DrawBatcher.h"

#include <gtest/gtest.h>

#include <vector>

namespace engine {
namespace {

DrawItem compatibleItem(std::uint32_t objectRow, MaterialBatchingMode mode) {
    DrawItem item;
    item.pipeline = {1, 1};
    item.materialBindGroup = {2, 1};
    item.vertexBuffers.push_back({0, {3, 1}});
    item.indexBuffer = {4, 1};
    item.indexFormat = rhi::IndexFormat::UInt32;
    item.arguments = {.indexCount = 36,
                      .instanceCount = 1,
                      .firstIndex = 0,
                      .vertexOffset = 0,
                      .firstInstance = objectRow};
    item.batchingMode = mode;
    return item;
}

TEST(GpuInstancingBatcherTest, RequiresMaterialOptIn) {
    const std::vector items{compatibleItem(0, MaterialBatchingMode::None),
                            compatibleItem(1, MaterialBatchingMode::None)};
    const BatchedRenderItems result = DrawBatcher{}.build(items);
    EXPECT_EQ(result.items.size(), 2U);
    EXPECT_EQ(result.gpuInstancedBatchCount, 0U);
}

TEST(GpuInstancingBatcherTest, PacksInstanceRowsForOneDraw) {
    const std::vector items{compatibleItem(7, MaterialBatchingMode::GpuInstancing),
                            compatibleItem(3, MaterialBatchingMode::GpuInstancing),
                            compatibleItem(9, MaterialBatchingMode::GpuInstancing)};
    const BatchedRenderItems result = DrawBatcher{}.build(items);
    ASSERT_EQ(result.items.size(), 1U);
    EXPECT_EQ(result.items.front().arguments.instanceCount, 3U);
    EXPECT_EQ(result.gpuInstancedBatchCount, 1U);
    EXPECT_EQ(result.instanceRows, (std::vector<std::uint32_t>{7, 3, 9}));
}

TEST(GpuInstancingBatcherTest, SplitsAtConfiguredInstanceLimit) {
    std::vector<DrawItem> items;
    for (std::uint32_t row = 0; row < 5; ++row) {
        items.push_back(compatibleItem(row, MaterialBatchingMode::GpuInstancing));
    }
    const BatchedRenderItems result =
        DrawBatcher{DrawBatcherLimits{.maxGpuInstancesPerDraw = 2}}.build(items);
    ASSERT_EQ(result.items.size(), 3U);
    EXPECT_EQ(result.items[0].arguments.instanceCount, 2U);
    EXPECT_EQ(result.items[1].arguments.instanceCount, 2U);
    EXPECT_EQ(result.items[2].arguments.instanceCount, 1U);
}

} // namespace
} // namespace engine

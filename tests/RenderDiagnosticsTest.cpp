#include "render/material/Material.h"
#include "render/renderer/DrawBatcher.h"
#include "render/renderer/RenderFrameStats.h"

#include <gtest/gtest.h>

#include <vector>

namespace engine {
namespace {

DrawItem queueItem(int queue, std::uint32_t objectRow) {
    DrawItem item;
    item.renderQueue = queue;
    item.pipeline = {1, 1};
    item.materialBindGroup = {2, 1};
    item.vertexBuffers.push_back({0, {3, 1}});
    item.indexBuffer = {4, 1};
    item.arguments = {.indexCount = 3, .instanceCount = 1, .firstInstance = objectRow};
    item.batchingMode = MaterialBatchingMode::GpuInstancing;
    return item;
}

TEST(RenderDiagnosticsTest, AccumulatesPassSubmissionStats) {
    RenderFrameStats stats;
    stats.recordSubmission({.sourceItems = 5, .renderItems = 3, .gpuInstancedDraws = 1});
    stats.recordSubmission({.sourceItems = 2, .renderItems = 2, .gpuInstancedDraws = 0});

    EXPECT_EQ(stats.submittedDrawItems, 7U);
    EXPECT_EQ(stats.renderItems, 5U);
    EXPECT_EQ(stats.drawCalls, 5U);
    EXPECT_EQ(stats.gpuInstancedDraws, 1U);
}

TEST(RenderDiagnosticsTest, KeepsRenderQueuesInSeparateDrawBatches) {
    const std::vector items{queueItem(2000, 0), queueItem(2000, 1), queueItem(2450, 2)};
    const BatchedDrawList batches = DrawBatcher{}.build(items, "Forward");

    ASSERT_EQ(batches.batches.size(), 2U);
    EXPECT_EQ(batches.batches[0].renderQueue, 2000);
    EXPECT_EQ(batches.batches[0].instanceCount, 2U);
    EXPECT_EQ(batches.batches[1].renderQueue, 2450);
    EXPECT_EQ(batches.batches[1].instanceCount, 1U);
}

} // namespace
} // namespace engine

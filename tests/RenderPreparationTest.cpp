#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/queue/RenderQueue.h"
#include "render/renderer/RenderItems.h"

#include <gtest/gtest.h>

namespace engine {
namespace {

DrawItem item(RenderPhase phase, int queue, std::uint32_t layer) {
    DrawItem result;
    result.renderPhase = phase;
    result.renderQueue = queue;
    result.layerMask = layer;
    return result;
}

TEST(RenderPreparationTest, PassCollectionTraversesQueueGroupsInAscendingOrder) {
    DrawList prepared;
    prepared.groups[kRenderQueueTransparent].push_back(
        item(RenderPhase::Forward, kRenderQueueTransparent, 0b10));
    prepared.groups[kRenderQueueGeometry].push_back(
        item(RenderPhase::Forward, kRenderQueueGeometry, 0b10));
    prepared.groups[kRenderQueueGeometry].push_back(
        item(RenderPhase::DepthOnly, kRenderQueueGeometry, 0b10));

    const DrawFilter filter{RenderQueueRange::all(), 0b10};
    const std::vector<DrawItem> result =
        collectPassItems(prepared, RenderPhase::Forward, filter);

    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0].renderQueue, kRenderQueueGeometry);
    EXPECT_EQ(result[1].renderQueue, kRenderQueueTransparent);
}

TEST(RenderPreparationTest, PassCollectionAppliesQueueAndLayerFiltering) {
    DrawList prepared;
    prepared.groups[kRenderQueueGeometry].push_back(
        item(RenderPhase::Forward, kRenderQueueGeometry, 0b01));
    prepared.groups[kRenderQueueAlphaTest].push_back(
        item(RenderPhase::Forward, kRenderQueueAlphaTest, 0b10));
    prepared.groups[kRenderQueueTransparent].push_back(
        item(RenderPhase::Forward, kRenderQueueTransparent, 0b10));

    const DrawFilter filter{RenderQueueRange::opaque(), 0b10};
    const std::vector<DrawItem> result =
        collectPassItems(prepared, RenderPhase::Forward, filter);

    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.front().renderQueue, kRenderQueueAlphaTest);
}

TEST(RenderPreparationTest, SourceAndRenderStagesKeepExplicitGeometryContracts) {
    SourceDrawItemList items;
    SourceDrawItem source;
    source.indexBuffer = {7, 1};
    source.indexRange = {.firstIndex = 12, .indexCount = 36, .vertexOffset = 4};
    items.push_back(source);

    RenderItem render;
    render.indexBuffer = source.indexBuffer;
    render.indexFormat = rhi::IndexFormat::UInt32;
    render.arguments = {.indexCount = source.indexRange.indexCount,
                        .instanceCount = 1,
                        .firstIndex = source.indexRange.firstIndex,
                        .vertexOffset = source.indexRange.vertexOffset};

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(render.indexFormat, rhi::IndexFormat::UInt32);
    EXPECT_EQ(render.arguments.indexCount, 36U);
    EXPECT_EQ(render.arguments.firstIndex, 12U);
}

} // namespace
} // namespace engine

#include "TestRenderDevice.h"
#include "render/material/Material.h"
#include "render/mesh/MeshBuilder.h"
#include "render/mesh/MeshManager.h"
#include "render/queue/RenderQueue.h"
#include "render/renderer/StaticBatcher.h"

#include <gtest/gtest.h>

#include <cstring>

namespace engine {
namespace {

class StaticBatcherTest : public ::testing::Test {
protected:
    void SetUp() override { MESH_MANAGER.clear(); }
    void TearDown() override { MESH_MANAGER.clear(); }
};

DrawItem makeStaticItem(MeshHandle mesh,
                        const SubMesh& range,
                        std::uint32_t objectIndex,
                        const math::Mat44& world) {
    DrawItem item;
    item.mesh = mesh;
    item.pipeline = {1, 1};
    item.material = {2, 1};
    item.materialBindGroup = {3, 1};
    item.renderPhase = RenderPhase::Forward;
    item.renderQueue = kRenderQueueGeometry;
    item.batchingMode = MaterialBatchingMode::Static;
    item.worldMatrix = world;
    item.arguments = {.indexCount = range.indexCount,
                      .instanceCount = 1,
                      .firstIndex = range.firstIndex,
                      .vertexOffset = range.vertexOffset,
                      .firstInstance = objectIndex};
    return item;
}

TEST_F(StaticBatcherTest, BakesGeometryAndReusesVersionedCache) {
    MeshBuildRecipe recipe;
    recipe.name = "StaticPlane";
    recipe.keepCpuCopy = true;
    recipe.parts.push_back({PlaneGeometry{{2.0F, 2.0F}, 1, 1}});
    const MeshHandle mesh = MESH_MANAGER.createRuntime(recipe);
    ASSERT_TRUE(mesh);
    const Mesh* source = MESH_MANAGER.find(mesh);
    ASSERT_NE(source, nullptr);
    ASSERT_EQ(source->desc().subMeshes.size(), 1U);

    const DrawItem first = makeStaticItem(
        mesh, source->desc().subMeshes.front(), 0, math::translation({-2.0F, 0.0F, 0.0F}));
    const DrawItem second = makeStaticItem(
        mesh, source->desc().subMeshes.front(), 1, math::translation({2.0F, 0.0F, 0.0F}));
    const auto makeList = [&] {
        DrawList list;
        list.objects = {{first.worldMatrix}, {second.worldMatrix}};
        list.groups[kRenderQueueGeometry] = {first, second};
        return list;
    };

    MockDevice device;
    StaticBatcher batcher;
    DrawList initial = makeList();
    batcher.process(initial, device);
    ASSERT_EQ(initial.groups[kRenderQueueGeometry].size(), 1U)
        << "source=" << batcher.stats().sourceItems
        << " combined=" << batcher.stats().combinedDraws
        << " misses=" << batcher.stats().cacheMisses
        << " streams=" << source->data().vertexStreams.size()
        << " indices=" << source->data().indices.size()
        << " first=" << source->desc().subMeshes.front().firstIndex
        << " count=" << source->desc().subMeshes.front().indexCount
        << " offset=" << source->desc().subMeshes.front().vertexOffset
        << " vertices=" << source->data().vertexStreams.front().vertexCount;
    const DrawItem& combined = initial.groups[kRenderQueueGeometry].front();
    EXPECT_EQ(combined.arguments.indexCount, 12U);
    EXPECT_EQ(combined.arguments.instanceCount, 1U);
    EXPECT_EQ(combined.batchingMode, MaterialBatchingMode::None);
    EXPECT_EQ(combined.indexFormat, rhi::IndexFormat::UInt32);
    EXPECT_EQ(batcher.stats().cacheMisses, 1U);
    ASSERT_FALSE(combined.vertexBuffers.empty());
    const VertexStream& sourceVertices = source->data().vertexStreams.front();
    const VertexBinding* binding = source->desc().vertexLayout.findBinding(sourceVertices.binding);
    const VertexAttribute* position =
        source->desc().vertexLayout.find({VertexSemanticType::Position, 0});
    ASSERT_NE(binding, nullptr);
    ASSERT_NE(position, nullptr);
    math::Vec3 originalPosition;
    std::memcpy(&originalPosition,
                sourceVertices.bytes.data() + position->offset,
                sizeof(originalPosition));
    const auto& bakedBytes =
        device.bufferContents[combined.vertexBuffers.front().buffer.index - 1];
    math::Vec3 firstPosition;
    math::Vec3 secondPosition;
    std::memcpy(&firstPosition, bakedBytes.data() + position->offset, sizeof(firstPosition));
    std::memcpy(&secondPosition,
                bakedBytes.data() +
                    static_cast<std::size_t>(sourceVertices.vertexCount) * binding->stride +
                    position->offset,
                sizeof(secondPosition));
    EXPECT_NEAR(firstPosition.x, originalPosition.x - 2.0F, 0.0001F);
    EXPECT_NEAR(secondPosition.x, originalPosition.x + 2.0F, 0.0001F);
    const std::size_t buffersAfterFirstBuild = device.buffers.size();

    DrawList cached = makeList();
    batcher.process(cached, device);
    EXPECT_EQ(batcher.stats().cacheHits, 1U);
    EXPECT_EQ(device.buffers.size(), buffersAfterFirstBuild);

    recipe.parts[0].primitive = PlaneGeometry{{4.0F, 4.0F}, 1, 1};
    ASSERT_TRUE(MESH_MANAGER.rebuildRuntime(mesh, recipe));
    DrawList changed = makeList();
    batcher.process(changed, device);
    EXPECT_EQ(batcher.stats().cacheMisses, 1U);
    EXPECT_GT(device.buffers.size(), buffersAfterFirstBuild);
}

TEST_F(StaticBatcherTest, SplitsAtSourceItemLimit) {
    MeshBuildRecipe recipe;
    recipe.name = "LimitedStaticPlane";
    recipe.keepCpuCopy = true;
    recipe.parts.push_back({PlaneGeometry{{1.0F, 1.0F}, 1, 1}});
    const MeshHandle mesh = MESH_MANAGER.createRuntime(recipe);
    ASSERT_TRUE(mesh);
    const SubMesh range = MESH_MANAGER.find(mesh)->desc().subMeshes.front();

    DrawList list;
    for (std::uint32_t index = 0; index < 5; ++index) {
        const math::Mat44 world = math::translation({static_cast<float>(index), 0.0F, 0.0F});
        list.objects.push_back({world});
        list.groups[kRenderQueueGeometry].push_back(makeStaticItem(mesh, range, index, world));
    }
    MockDevice device;
    StaticBatcher batcher{StaticBatcherLimits{.maxSourceItemsPerBatch = 2}};
    batcher.process(list, device);
    EXPECT_EQ(list.groups[kRenderQueueGeometry].size(), 3U);
    EXPECT_EQ(batcher.stats().combinedDraws, 2U);
}

} // namespace
} // namespace engine

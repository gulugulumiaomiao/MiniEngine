#include "TestRenderDevice.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/runtime/MeshAssetResolver.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/mesh/MeshBuilder.h"
#include "render/uniform/PipelineLayout.h"
#include "scene/components/MeshComponent.h"
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace {
using namespace engine;
static_assert(!std::is_same_v<MeshAssetData, MeshCreateInfo>);
static_assert(!std::is_same_v<MeshAssetDesc, MeshCreateInfo>);
static_assert(!std::is_same_v<MeshAssetVertexLayout, VertexLayout>);
static_assert(!std::is_same_v<MeshAssetBounds, MeshBounds>);
static_assert(!std::is_same_v<MeshBuildRecipeAssetData, MeshBuildRecipe>);
static_assert(!std::is_base_of_v<Transferable, Mesh>);
static_assert(!std::is_base_of_v<Transferable, MeshCreateInfo>);
static_assert(!std::is_base_of_v<Transferable, MeshData>);
static_assert(!std::is_base_of_v<Transferable, VertexLayout>);
static_assert(!std::is_base_of_v<Transferable, PlaneGeometry>);

MeshBuildRecipe recipe(std::uint32_t segments = 1) {
    MeshBuildRecipe result;
    result.name = "Tests/DetachedMesh";
    result.parts.emplace_back(PlaneGeometry{{2.0F, 4.0F}, segments, segments});
    result.vertexLayout = PrimitiveVertexLayout::Position;
    result.usage = MeshUsage::Dynamic;
    result.keepCpuCopy = true;
    return result;
}

class MeshIsolationTest : public ::testing::Test {
protected:
    void TearDown() override {
        FRAME_GPU_MANAGER.shutdown();
        PipelineLayout::clearCache();
        UniformSetLayout::clearCache();
    }
    MockDevice device;
};

TEST_F(MeshIsolationTest, RuntimeCloneCopiesCurrentDataWithoutAssetAssociation) {
    auto info = MeshBuilder::build(recipe());
    ASSERT_TRUE(info);
    const auto mesh = Mesh::create(*info);
    ASSERT_TRUE(mesh);
    const auto clone = mesh->clone();
    ASSERT_TRUE(clone);
    EXPECT_NE(mesh->instanceId(), clone->instanceId());
    EXPECT_NE(mesh->indexBuffer(), clone->indexBuffer());
    EXPECT_EQ(mesh->vertexLayout(), clone->vertexLayout());
    EXPECT_EQ(mesh->bounds(), clone->bounds());
    ASSERT_TRUE(clone->clone());
    auto changed = info->data;
    const float changedX = -0.75F;
    std::memcpy(changed.vertexStreams[0].bytes.data(), &changedX, sizeof(changedX));
    ASSERT_TRUE(mesh->upload(changed));
    const auto currentClone = mesh->clone();
    ASSERT_TRUE(currentClone);
    EXPECT_EQ(device.uploadedBuffers.at(currentClone->vertexBuffers()[0].buffer.index()),
              changed.vertexStreams[0].bytes);
    EXPECT_EQ(device.uploadedBuffers.at(clone->vertexBuffers()[0].buffer.index()),
              info->data.vertexStreams[0].bytes);
}

TEST_F(MeshIsolationTest, AssetKeepsUniqueInstanceAndRuntimeSurvivesAssetDestruction) {
    Ref<Mesh> mesh;
    {
        auto asset = buildMeshAsset(recipe());
        ASSERT_TRUE(asset);
        mesh = asset->instantiate();
        ASSERT_TRUE(mesh);
        EXPECT_EQ(mesh->refCount(), 2U);
        EXPECT_EQ(asset->instantiate(), mesh);
        mesh.reset();
        EXPECT_EQ(asset->runtimeInstance()->refCount(), 1U);
        mesh = asset->instantiate();
    }
    EXPECT_EQ(mesh->refCount(), 1U);
    EXPECT_TRUE(mesh->isValid());
    EXPECT_TRUE(mesh->clone());
}

TEST_F(MeshIsolationTest, CpuRetentionControlsRuntimeCloneButNotAssetClone) {
    auto build = recipe();
    build.keepCpuCopy = false;
    auto asset = buildMeshAsset(build);
    ASSERT_TRUE(asset);
    auto mesh = asset->instantiate();
    ASSERT_TRUE(mesh);
    EXPECT_FALSE(mesh->keepsCpuCopy());
    EXPECT_FALSE(mesh->clone());
    const auto clone = asset->clone();
    ASSERT_TRUE(clone);
    EXPECT_NE(mesh->instanceId(), clone->instanceId());
    EXPECT_NE(mesh->indexBuffer(), clone->indexBuffer());
}

TEST_F(MeshIsolationTest, RebuildUpdatesAllDrawMetadataAndCapacity) {
    auto initial = MeshBuilder::build(recipe());
    ASSERT_TRUE(initial);
    auto mesh = Mesh::create(*initial);
    ASSERT_TRUE(mesh);
    const auto id = mesh->instanceId();
    const auto revision = mesh->revision();
    const auto old = mesh->gpuBuffers();
    auto nextRecipe = recipe(3);
    nextRecipe.indexPolicy = MeshIndexPolicy::UInt32;
    nextRecipe.vertexLayout = PrimitiveVertexLayout::PositionNormalUv;
    nextRecipe.usage = MeshUsage::Static;
    nextRecipe.parts[0].translation = {10.0F, 0.0F, 0.0F};
    nextRecipe.parts[0].materialSlot = 3;
    auto next = MeshBuilder::build(nextRecipe);
    ASSERT_TRUE(next);
    next->topology = MeshTopology::LineList;
    ASSERT_TRUE(mesh->rebuild(*next));
    EXPECT_EQ(mesh->instanceId(), id);
    EXPECT_EQ(mesh->revision(), revision + 1);
    EXPECT_NE(mesh->gpuBuffers(), old);
    EXPECT_EQ(mesh->vertexBuffers().size(), 3U);
    EXPECT_EQ(mesh->indexFormat(), rhi::IndexFormat::UInt32);
    EXPECT_EQ(mesh->usage(), MeshUsage::Static);
    EXPECT_EQ(mesh->topology(), MeshTopology::LineList);
    EXPECT_EQ(mesh->bounds(), next->bounds);
    EXPECT_EQ(mesh->subMeshes(), next->subMeshes);
    EXPECT_EQ(device.uploadedBuffers.at(mesh->indexBuffer().index()).size(),
              next->data.indices.size());
    const auto stable = mesh->gpuBuffers();
    const auto stableRevision = mesh->revision();
    EXPECT_FALSE(mesh->upload(initial->data));
    EXPECT_EQ(mesh->gpuBuffers(), stable);
    EXPECT_EQ(mesh->revision(), stableRevision);
}

TEST_F(MeshIsolationTest, InvalidDataAndPartialAllocationFailurePreserveOldState) {
    auto initial = MeshBuilder::build(recipe());
    ASSERT_TRUE(initial);
    auto mesh = Mesh::create(*initial);
    ASSERT_TRUE(mesh);
    const auto buffers = mesh->gpuBuffers();
    const auto revision = mesh->revision();
    auto invalid = *initial;
    invalid.data.indexCount = 0;
    EXPECT_FALSE(mesh->rebuild(invalid));
    EXPECT_EQ(mesh->revision(), revision);
    EXPECT_EQ(mesh->gpuBuffers(), buffers);
    auto next = MeshBuilder::build(recipe(4));
    ASSERT_TRUE(next);
    const auto destroyed = device.destroyedBuffers;
    device.bufferAllocationsUntilFailure = 1;
    EXPECT_FALSE(mesh->rebuild(*next));
    EXPECT_EQ(device.destroyedBuffers, destroyed + 1);
    EXPECT_EQ(mesh->gpuBuffers(), buffers);
    EXPECT_EQ(mesh->bounds(), initial->bounds);
    EXPECT_EQ(mesh->revision(), revision);
    EXPECT_TRUE(mesh->isValid());
}

TEST_F(MeshIsolationTest, SubmittedBufferSnapshotSurvivesUpdateAndMeshDestruction) {
    ASSERT_TRUE(FRAME_GPU_MANAGER.initialize());
    FRAME_GPU_MANAGER.beginFrame(0);
    auto info = MeshBuilder::build(recipe());
    ASSERT_TRUE(info);
    auto mesh = Mesh::create(*info);
    ASSERT_TRUE(mesh);
    auto submitted = mesh->gpuBuffers();
    FRAME_GPU_MANAGER.retain(0, submitted);
    const auto originalRid = submitted->vertexBuffers()[0].buffer;
    auto changed = info->data;
    const float x = -0.75F;
    std::memcpy(changed.vertexStreams[0].bytes.data(), &x, sizeof(x));
    ASSERT_TRUE(mesh->upload(changed));
    EXPECT_NE(mesh->gpuBuffers(), submitted);
    EXPECT_EQ(device.uploadedBuffers.at(originalRid.index()), info->data.vertexStreams[0].bytes);
    const auto destroyed = device.destroyedBuffers;
    submitted.reset();
    mesh.reset();
    EXPECT_EQ(device.destroyedBuffers, destroyed + 2);
    FRAME_GPU_MANAGER.beginFrame(1);
    EXPECT_EQ(device.destroyedBuffers, destroyed + 2);
    FRAME_GPU_MANAGER.beginFrame(0);
    EXPECT_EQ(device.destroyedBuffers, destroyed + 4);
}

TEST_F(MeshIsolationTest, ReloadPreservesIdentityAndDetachedCloneAndRejectsInvalidPayload) {
    auto asset = buildMeshAsset(recipe());
    ASSERT_TRUE(asset);
    const auto mesh = asset->instantiate();
    ASSERT_TRUE(mesh);
    const auto clone = mesh->clone();
    ASSERT_TRUE(clone);
    const auto id = mesh->instanceId();
    const auto revision = mesh->revision();
    const auto cloneBuffer = clone->indexBuffer();
    auto updatedRecipe = recipe(3);
    updatedRecipe.parts[0].translation = {5.0F, 0.0F, 0.0F};
    auto updated = buildMeshAsset(updatedRecipe);
    ASSERT_TRUE(updated);
    BinaryWriter writer;
    ASSERT_TRUE(updated->transfer(writer));
    BinaryReader reader{writer.bytes()};
    ASSERT_TRUE(asset->transfer(reader));
    EXPECT_EQ(asset->instantiate(), mesh);
    EXPECT_EQ(mesh->instanceId(), id);
    EXPECT_EQ(mesh->revision(), revision + 1);
    EXPECT_NE(mesh->bounds(), clone->bounds());
    EXPECT_EQ(clone->indexBuffer(), cloneBuffer);
    auto badData = updated->data;
    badData.meshData.indexCount = 0;
    BinaryWriter bad;
    ASSERT_TRUE(badData.transfer(bad));
    BinaryReader badReader{bad.bytes()};
    const auto stableBuffer = mesh->indexBuffer();
    EXPECT_FALSE(asset->transfer(badReader));
    EXPECT_EQ(mesh->indexBuffer(), stableBuffer);
    EXPECT_EQ(mesh->revision(), revision + 1);
    EXPECT_EQ(asset->data.meshData.indexCount, updated->data.meshData.indexCount);
}

TEST_F(MeshIsolationTest, StreamUploadsReplaceSnapshotsWithoutDestroyingDeviceOwnedBuffers) {
    auto source = recipe();
    source.usage = MeshUsage::Stream;
    auto info = MeshBuilder::build(source);
    ASSERT_TRUE(info);
    auto mesh = Mesh::create(*info);
    ASSERT_TRUE(mesh);
    const auto old = mesh->gpuBuffers();
    const auto destroyed = device.destroyedBuffers;
    ASSERT_TRUE(mesh->upload(info->data));
    EXPECT_NE(mesh->gpuBuffers(), old);
    mesh.reset();
    EXPECT_EQ(device.destroyedBuffers, destroyed);
}

TEST_F(MeshIsolationTest, PrimitiveComponentRebuildsSameInstanceAndKeepsOldMeshOnFailure) {
    MeshComponent component;
    component.setPrimitive(PlaneGeometry{});
    ASSERT_TRUE(component.applyPrimitiveChanges());
    const auto original = component.mesh();
    ASSERT_TRUE(original);
    const auto revision = original->revision();
    component.setPrimitive(PlaneGeometry{{4.0F, 2.0F}, 3, 2});
    ASSERT_TRUE(component.applyPrimitiveChanges());
    EXPECT_EQ(component.mesh(), original);
    EXPECT_EQ(original->revision(), revision + 1);
    const auto bounds = original->bounds();
    component.setPrimitive(PlaneGeometry{{0.0F, 2.0F}, 1, 1});
    EXPECT_FALSE(component.applyPrimitiveChanges());
    EXPECT_EQ(component.mesh(), original);
    EXPECT_EQ(original->bounds(), bounds);
}

TEST(MeshDeviceTest, RetainedCpuDataRecreatesBuffersAndOldHandlesCannotDestroyNewDeviceResources) {
    Ref<Mesh> retained;
    Ref<Mesh> discarded;
    Ref<MeshGpuBuffers> oldSnapshot;
    {
        MockDevice first;
        auto info = MeshBuilder::build(recipe());
        ASSERT_TRUE(info);
        retained = Mesh::create(*info);
        info->keepCpuCopy = false;
        discarded = Mesh::create(*info);
        ASSERT_TRUE(retained);
        ASSERT_TRUE(discarded);
        oldSnapshot = retained->gpuBuffers();
    }
    MockDevice second;
    EXPECT_FALSE(oldSnapshot->currentDevice());
    EXPECT_FALSE(discarded->isValid());
    EXPECT_TRUE(retained->isValid());
    EXPECT_NE(retained->gpuBuffers(), oldSnapshot);
    EXPECT_EQ(retained->gpuBuffers()->deviceUid(), second.uid());
    const auto destroyed = second.destroyedBuffers;
    discarded.reset();
    oldSnapshot.reset();
    EXPECT_EQ(second.destroyedBuffers, destroyed);
    retained.reset();
    EXPECT_EQ(second.destroyedBuffers, destroyed + 2);
}

TEST(MeshSchemaTest, BuilderAndAssetConversionWorkWithoutGpuDevice) {
    ASSERT_EQ(RHI_DEVICE, nullptr);
    auto info = MeshBuilder::build(recipe());
    ASSERT_TRUE(info);
    const auto asset = buildMeshAsset(recipe());
    ASSERT_TRUE(asset);
    ASSERT_TRUE(asset->buildRuntimeInfo());
    EXPECT_FALSE(asset->instantiate());
    EXPECT_FALSE(asset->runtimeInstance());
}

TEST(MeshSchemaTest, RewritesPreSplitV4ArtifactWithoutChangingPayload) {
    const auto path =
        std::filesystem::path{MINI_TEST_MESH_ISOLATION_FIXTURE_DIR} / "mesh_legacy_v4.artifact";
    std::ifstream input{path, std::ios::binary};
    ASSERT_TRUE(input);
    const std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    const auto artifact = parseAssetArtifact(VirtualPath{"fixture://mesh_legacy_v4.artifact"},
                                             std::as_bytes(std::span{raw}));
    ASSERT_TRUE(artifact);
    MeshAsset asset;
    BinaryReader reader{artifact->payload};
    ASSERT_TRUE(asset.transfer(reader));
    ASSERT_TRUE(reader.finished());
    ASSERT_TRUE(asset.data.buildRecipe);
    EXPECT_FALSE(asset.data.meshData.vertexStreams.empty());
    ASSERT_TRUE(asset.buildRuntimeInfo());
    BinaryWriter writer;
    ASSERT_TRUE(asset.transfer(writer));
    EXPECT_EQ(writer.bytes(), artifact->payload);
}
} // namespace

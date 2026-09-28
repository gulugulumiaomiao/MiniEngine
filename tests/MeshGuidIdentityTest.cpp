#include "render/mesh/Mesh.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "TestAssetEnvironment.h"
#include "TestRenderDevice.h"

#include <gtest/gtest.h>

namespace {

// The MockDevice member registers itself as the active device on construction, so layer-2
// Mesh creation (which resolves the device through IDevice::active()) works headlessly.
class MeshGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override { engine::test::shutdownAssetEnvironment(); }

    MockDevice device;
};

TEST_F(MeshGuidIdentityTest, ResolveSamePathReturnsSameInstance) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> first = resolveMeshReference(path.string());
    ASSERT_TRUE(first);

    // Resolving the same reference again yields the very same runtime instance: with no mesh
    // manager, "one instantiate instance per asset" is the dedup mechanism.
    const Ref<Mesh> second = resolveMeshReference(path.string());
    EXPECT_EQ(first, second);

    const Ref<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(path);
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->instantiate(), first);
    EXPECT_TRUE(first->isAssetBacked());
    EXPECT_TRUE(first->isValid());
    EXPECT_EQ(first->assetPath(), path);
}

TEST_F(MeshGuidIdentityTest, CloneReturnsDifferentObjectAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> original = resolveMeshReference(path.string());
    ASSERT_TRUE(original);

    const Ref<Mesh> cloned = original->clone();
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);
    EXPECT_TRUE(original->isAssetBacked());
    EXPECT_FALSE(cloned->isAssetBacked());
    EXPECT_TRUE(cloned->isValid());
}

TEST_F(MeshGuidIdentityTest, LastRefReleasesGpuBuffers) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    Ref<Mesh> mesh = resolveMeshReference(path.string());
    ASSERT_TRUE(mesh);
    const std::uint32_t vertexBufferCount =
        static_cast<std::uint32_t>(mesh->vertexBuffers().size());
    const std::uint32_t destroyedBefore = device.destroyedBuffers;

    // The asset only holds a raw instance_ observer, so this Ref is the sole owner.
    mesh.reset();

    // Two-step destroy releases every vertex buffer plus the index buffer.
    EXPECT_EQ(device.destroyedBuffers, destroyedBefore + vertexBufferCount + 1);
}

TEST_F(MeshGuidIdentityTest, ReloadInPlacePreservesInstanceAndReuploads) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> original = resolveMeshReference(path.string());
    ASSERT_TRUE(original);
    const std::size_t uploadsBefore = device.uploadedBytes.size();

    // Hot reload re-transfers the cached asset in place; MeshAsset::transfer then pushes the
    // refreshed data to the single live instance instead of creating a new object.
    ASSET_MANAGER.reloadInPlace(path);

    EXPECT_EQ(resolveMeshReference(path.string()), original);
    EXPECT_GT(device.uploadedBytes.size(), uploadsBefore);
}

} // namespace

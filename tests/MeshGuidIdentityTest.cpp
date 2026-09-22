#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class MeshGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override {
        MESH_MANAGER.clear();
        engine::test::shutdownAssetEnvironment();
    }
};

TEST_F(MeshGuidIdentityTest, LoadByGuidReturnsSameObject) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> byPath = MESH_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Mesh> byId = MESH_RESOURCE_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(MESH_RESOURCE_MANAGER.find(*assetId), byPath);
}

TEST_F(MeshGuidIdentityTest, CloneReturnsDifferentObjectAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> original = MESH_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const Ref<Mesh> cloned = MESH_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    EXPECT_TRUE(original->isAssetBacked());
    EXPECT_FALSE(cloned->isAssetBacked());
    EXPECT_EQ(cloned->assetPath(), original->assetPath());
}

TEST_F(MeshGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const Ref<Mesh> original = MESH_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Mesh> cloned = MESH_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(MESH_RESOURCE_MANAGER.find(*assetId), original);
    EXPECT_NE(MESH_RESOURCE_MANAGER.find(*assetId), cloned);
}

TEST_F(MeshGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Mesh> original = MESH_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);
    const Ref<Mesh> cloned = MESH_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    const std::uint64_t originalVersion = original->version();
    const std::uint64_t clonedVersion = cloned->version();

    MESH_RESOURCE_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(original->version(), originalVersion);
    EXPECT_EQ(cloned->version(), clonedVersion);
}

} // namespace

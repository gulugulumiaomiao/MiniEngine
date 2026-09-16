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

TEST_F(MeshGuidIdentityTest, LoadByGuidReturnsSameHandle) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const MeshHandle byPath = MESH_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const MeshHandle byId = MESH_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(MESH_MANAGER.find(*assetId), MESH_MANAGER.find(byPath));
}

TEST_F(MeshGuidIdentityTest, CloneReturnsDifferentHandleAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const MeshHandle original = MESH_MANAGER.load(path);
    ASSERT_TRUE(original);

    const MeshHandle cloned = MESH_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    const Mesh* originalData = MESH_MANAGER.find(original);
    const Mesh* clonedData = MESH_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    EXPECT_TRUE(originalData->isAssetBacked());
    EXPECT_FALSE(clonedData->isAssetBacked());
    EXPECT_EQ(clonedData->assetPath(), originalData->assetPath());
}

TEST_F(MeshGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const MeshHandle original = MESH_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const MeshHandle cloned = MESH_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(MESH_MANAGER.find(*assetId), MESH_MANAGER.find(original));
    EXPECT_NE(MESH_MANAGER.find(*assetId), MESH_MANAGER.find(cloned));
}

TEST_F(MeshGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://meshes/procedural_showcase.mesh.json"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const MeshHandle original = MESH_MANAGER.load(path);
    ASSERT_TRUE(original);
    const MeshHandle cloned = MESH_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Mesh* originalData = MESH_MANAGER.find(original);
    Mesh* clonedData = MESH_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const std::uint64_t originalVersion = originalData->version();
    const std::uint64_t clonedVersion = clonedData->version();

    MESH_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(originalData->version(), originalVersion);
    EXPECT_EQ(clonedData->version(), clonedVersion);
}

} // namespace

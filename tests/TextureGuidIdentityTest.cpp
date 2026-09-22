#include "render/gpu/texture/TextureStorage.h"
#include "render/texture/Texture.h"
#include "render/texture/TextureManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"
#include "TestRenderDevice.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class TextureGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
        TEXTURE_RESOURCE_MANAGER.clear();
        ASSERT_TRUE(TEXTURE_STORAGE.initialize(device));
    }

    void TearDown() override {
        TEXTURE_RESOURCE_MANAGER.clear();
        TEXTURE_STORAGE.shutdown();
        engine::test::shutdownAssetEnvironment();
    }

    MockDevice device;
};

TEST_F(TextureGuidIdentityTest, LoadByGuidReturnsSameObject) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const Ref<Texture> byPath = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Texture> byId = TEXTURE_RESOURCE_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(TEXTURE_RESOURCE_MANAGER.find(*assetId), byPath);
}

TEST_F(TextureGuidIdentityTest, CloneReturnsDifferentObjectAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const Ref<Texture> original = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const Ref<Texture> cloned = TEXTURE_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    EXPECT_TRUE(original->isAssetBacked());
    EXPECT_FALSE(cloned->isAssetBacked());
    EXPECT_EQ(cloned->assetPath(), original->assetPath());
}

TEST_F(TextureGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const Ref<Texture> original = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Texture> cloned = TEXTURE_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(TEXTURE_RESOURCE_MANAGER.find(*assetId), original);
    EXPECT_NE(TEXTURE_RESOURCE_MANAGER.find(*assetId), cloned);
}

TEST_F(TextureGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Texture> original = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);
    const Ref<Texture> cloned = TEXTURE_RESOURCE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    const std::uint64_t originalVersion = original->version();
    const std::uint64_t clonedVersion = cloned->version();

    TEXTURE_RESOURCE_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(original->version(), originalVersion);
    EXPECT_EQ(cloned->version(), clonedVersion);
}

} // namespace

#include "render/texture/Texture.h"
#include "render/texture/TextureManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class TextureGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override {
        TEXTURE_MANAGER.clear();
        engine::test::shutdownAssetEnvironment();
    }
};

TEST_F(TextureGuidIdentityTest, LoadByGuidReturnsSameHandle) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const TextureHandle byPath = TEXTURE_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const TextureHandle byId = TEXTURE_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(TEXTURE_MANAGER.find(*assetId), TEXTURE_MANAGER.find(byPath));
}

TEST_F(TextureGuidIdentityTest, CloneReturnsDifferentHandleAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const TextureHandle original = TEXTURE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const TextureHandle cloned = TEXTURE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    const Texture* originalData = TEXTURE_MANAGER.find(original);
    const Texture* clonedData = TEXTURE_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    EXPECT_TRUE(originalData->isAssetBacked());
    EXPECT_FALSE(clonedData->isAssetBacked());
    EXPECT_EQ(clonedData->assetPath(), originalData->assetPath());
}

TEST_F(TextureGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const TextureHandle original = TEXTURE_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const TextureHandle cloned = TEXTURE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(TEXTURE_MANAGER.find(*assetId), TEXTURE_MANAGER.find(original));
    EXPECT_NE(TEXTURE_MANAGER.find(*assetId), TEXTURE_MANAGER.find(cloned));
}

TEST_F(TextureGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const TextureHandle original = TEXTURE_MANAGER.load(path);
    ASSERT_TRUE(original);
    const TextureHandle cloned = TEXTURE_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Texture* originalData = TEXTURE_MANAGER.find(original);
    Texture* clonedData = TEXTURE_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const std::uint64_t originalVersion = originalData->version();
    const std::uint64_t clonedVersion = clonedData->version();

    TEXTURE_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(originalData->version(), originalVersion);
    EXPECT_EQ(clonedData->version(), clonedVersion);
}

} // namespace

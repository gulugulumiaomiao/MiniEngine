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

TEST_F(TextureGuidIdentityTest, LastRefReleasesStorageAndWeakCacheEntry) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    Ref<Texture> texture = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(texture);
    const RID resourceId = texture->resourceId();
    ASSERT_NE(TEXTURE_STORAGE.resolve(*texture), nullptr);
    const int destroyedBefore = device.destroyedTextures;

    texture.reset();

    EXPECT_FALSE(TEXTURE_RESOURCE_MANAGER.find(resourceId));
    EXPECT_EQ(device.destroyedTextures, destroyedBefore + 1);
}

TEST_F(TextureGuidIdentityTest, RefreshAssetBumpsVersion) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Texture> original = TEXTURE_RESOURCE_MANAGER.load(path);
    ASSERT_TRUE(original);
    const std::uint64_t originalVersion = original->version();

    TEXTURE_RESOURCE_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(original->version(), originalVersion);
}

} // namespace

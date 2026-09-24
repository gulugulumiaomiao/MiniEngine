#include "render/texture/Texture.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "TestAssetEnvironment.h"
#include "TestRenderDevice.h"

#include <gtest/gtest.h>

namespace {

// The MockDevice member registers itself as the active device on construction, so layer-2
// Texture creation (which resolves the device through IDevice::active()) works headlessly.
class TextureGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override { engine::test::shutdownAssetEnvironment(); }

    MockDevice device;
};

TEST_F(TextureGuidIdentityTest, ResolveSamePathReturnsSameInstance) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const Ref<Texture> first = resolveTextureReference(path.string());
    ASSERT_TRUE(first);

    // Resolving the same reference again yields the very same runtime instance: with no
    // texture manager, "one instantiate instance per asset" is the dedup mechanism.
    const Ref<Texture> second = resolveTextureReference(path.string());
    EXPECT_EQ(first, second);

    const Ref<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(path);
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->instantiate(), first);
    EXPECT_TRUE(first->isAssetBacked());
    EXPECT_TRUE(first->textureHandle());
}

TEST_F(TextureGuidIdentityTest, LastRefReleasesGpuTexture) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    Ref<Texture> texture = resolveTextureReference(path.string());
    ASSERT_TRUE(texture);
    const int destroyedBefore = device.destroyedTextures;

    // The asset only holds a raw instance_ observer, so this Ref is the sole owner.
    texture.reset();

    EXPECT_EQ(device.destroyedTextures, destroyedBefore + 1);
}

TEST_F(TextureGuidIdentityTest, ReloadInPlacePreservesInstanceAndReuploads) {
    using namespace engine;
    const VirtualPath path{"assets://textures/checker.png"};
    const Ref<Texture> original = resolveTextureReference(path.string());
    ASSERT_TRUE(original);
    const std::uint32_t uploadsBefore = device.textureUploads;

    // Hot reload re-transfers the cached asset in place; TextureAsset::transfer then pushes the
    // refreshed pixels to the single live instance instead of creating a new object.
    ASSET_MANAGER.reloadInPlace(path);

    EXPECT_EQ(resolveTextureReference(path.string()), original);
    EXPECT_GT(device.textureUploads, uploadsBefore);
}

} // namespace

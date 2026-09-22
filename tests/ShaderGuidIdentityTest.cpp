#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class ShaderGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override {
        SHADER_MANAGER.clear();
        engine::test::shutdownAssetEnvironment();
    }
};

TEST_F(ShaderGuidIdentityTest, LoadByGuidReturnsSameHandle) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const Ref<Shader> byPath = SHADER_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Shader> byId = SHADER_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(SHADER_MANAGER.find(*assetId), byPath);
}

TEST_F(ShaderGuidIdentityTest, CloneReturnsDifferentHandleAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const Ref<Shader> original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);

    const Ref<Shader> cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    const Shader* originalData = original.get();
    const Shader* clonedData = cloned.get();
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    EXPECT_TRUE(originalData->isAssetBacked());
    EXPECT_FALSE(clonedData->isAssetBacked());
    EXPECT_EQ(clonedData->assetPath(), originalData->assetPath());
}

TEST_F(ShaderGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const Ref<Shader> original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Shader> cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(SHADER_MANAGER.find(*assetId), original);
    EXPECT_NE(SHADER_MANAGER.find(*assetId), cloned);
}

TEST_F(ShaderGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const Ref<Shader> original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);
    const Ref<Shader> cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Shader* originalData = original.get();
    Shader* clonedData = cloned.get();
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const std::uint64_t originalRevision = originalData->revision();
    const std::uint64_t clonedRevision = clonedData->revision();

    SHADER_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(originalData->revision(), originalRevision);
    EXPECT_EQ(clonedData->revision(), clonedRevision);
}

} // namespace

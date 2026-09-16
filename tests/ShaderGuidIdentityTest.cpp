#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "asset/importer/FileWatcher.h"
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
    const ShaderHandle byPath = SHADER_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const ShaderHandle byId = SHADER_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(SHADER_MANAGER.find(*assetId), SHADER_MANAGER.find(byPath));
}

TEST_F(ShaderGuidIdentityTest, CloneReturnsDifferentHandleAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const ShaderHandle original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);

    const ShaderHandle cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    const Shader* originalData = SHADER_MANAGER.find(original);
    const Shader* clonedData = SHADER_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    EXPECT_TRUE(originalData->isAssetBacked());
    EXPECT_FALSE(clonedData->isAssetBacked());
    EXPECT_EQ(clonedData->assetPath(), originalData->assetPath());
}

TEST_F(ShaderGuidIdentityTest, CloneDoesNotAppearInAssetIndex) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const ShaderHandle original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const ShaderHandle cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    EXPECT_EQ(SHADER_MANAGER.find(*assetId), SHADER_MANAGER.find(original));
    EXPECT_NE(SHADER_MANAGER.find(*assetId), SHADER_MANAGER.find(cloned));
}

TEST_F(ShaderGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://shaders/builtin_color.shader.json"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const ShaderHandle original = SHADER_MANAGER.load(path);
    ASSERT_TRUE(original);
    const ShaderHandle cloned = SHADER_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Shader* originalData = SHADER_MANAGER.find(original);
    Shader* clonedData = SHADER_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const std::uint64_t originalRevision = originalData->revision();
    const std::uint64_t clonedRevision = clonedData->revision();

    SHADER_MANAGER.refreshAsset(*assetId);

    EXPECT_GT(originalData->revision(), originalRevision);
    EXPECT_EQ(clonedData->revision(), clonedRevision);
}

} // namespace

#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/shader/ShaderManager.h"
#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class MaterialGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override {
        MATERIAL_MANAGER.clear();
        SHADER_MANAGER.clear();
        engine::test::shutdownAssetEnvironment();
    }
};

TEST_F(MaterialGuidIdentityTest, LoadByGuidReturnsSameHandle) {
    using namespace engine;
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    const MaterialHandle byPath = MATERIAL_MANAGER.load(path);
    ASSERT_TRUE(byPath);

    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const MaterialHandle byId = MATERIAL_MANAGER.load(*assetId);
    EXPECT_EQ(byId, byPath);
    EXPECT_EQ(MATERIAL_MANAGER.find(*assetId), MATERIAL_MANAGER.find(byPath));
}

TEST_F(MaterialGuidIdentityTest, CloneReturnsDifferentHandleAndIsNotAssetBacked) {
    using namespace engine;
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    const MaterialHandle original = MATERIAL_MANAGER.load(path);
    ASSERT_TRUE(original);

    const MaterialHandle cloned = MATERIAL_MANAGER.clone(original);
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);

    const Material* originalData = MATERIAL_MANAGER.find(original);
    const Material* clonedData = MATERIAL_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    EXPECT_TRUE(originalData->isAssetBacked());
    EXPECT_FALSE(clonedData->isAssetBacked());
    EXPECT_EQ(clonedData->assetPath(), originalData->assetPath());
}

TEST_F(MaterialGuidIdentityTest, CloneDoesNotShareRuntimeState) {
    using namespace engine;
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    const MaterialHandle original = MATERIAL_MANAGER.load(path);
    ASSERT_TRUE(original);

    const MaterialHandle cloned = MATERIAL_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Material* originalData = MATERIAL_MANAGER.find(original);
    Material* clonedData = MATERIAL_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const std::uint64_t originalVersion = originalData->version();
    clonedData->setVec4("BaseColor", {0.1F, 0.2F, 0.3F, 1.0F});

    EXPECT_EQ(originalData->version(), originalVersion);
    EXPECT_NE(originalData->getVec4("BaseColor"), clonedData->getVec4("BaseColor"));
}

TEST_F(MaterialGuidIdentityTest, RefreshAssetUpdatesOriginalButNotClone) {
    using namespace engine;
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    const auto assetId = ASSET_DATABASE.findGuid(path);
    ASSERT_TRUE(assetId);

    const MaterialHandle original = MATERIAL_MANAGER.load(path);
    ASSERT_TRUE(original);
    const MaterialHandle cloned = MATERIAL_MANAGER.clone(original);
    ASSERT_TRUE(cloned);

    Material* originalData = MATERIAL_MANAGER.find(original);
    Material* clonedData = MATERIAL_MANAGER.find(cloned);
    ASSERT_NE(originalData, nullptr);
    ASSERT_NE(clonedData, nullptr);

    const math::Vec4 originalColor = originalData->getVec4("BaseColor");
    originalData->setVec4("BaseColor", {0.9F, 0.8F, 0.7F, 1.0F});
    clonedData->setVec4("BaseColor", {0.1F, 0.2F, 0.3F, 1.0F});

    MATERIAL_MANAGER.refreshAsset(*assetId);

    EXPECT_EQ(originalData->getVec4("BaseColor"), originalColor);
    EXPECT_EQ(clonedData->getVec4("BaseColor"), math::Vec4(0.1F, 0.2F, 0.3F, 1.0F));
}

} // namespace

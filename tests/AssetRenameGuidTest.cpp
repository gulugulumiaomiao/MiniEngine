#include <gtest/gtest.h>

#include "asset/base/AssetId.h"
#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

using namespace engine;

constexpr std::string_view kShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "RenameGuidShader",
  "properties": [
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShader": {
    "passes": [{
      "name": "Forward",
      "lightMode": "Forward",
      "program": { "vertex": "simple.vert", "frag": "simple.frag" }
    }]
  }
})json";

class AssetRenameGuidTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineAssetRenameGuidTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets / "shaders", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        FILE_WATCHER.stop();

        shaderPath = VirtualPath{"assets://shaders/rename_test.shader.json"};
        renamedPath = VirtualPath{"assets://shaders/renamed_test.shader.json"};
        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.vert"},
                                          "#version 450\nvoid main(){gl_Position=vec4(0);}\n"));
        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.frag"},
                                          "#version 450\nlayout(location=0) out vec4 c;"
                                          "void main(){c=vec4(1);}\n"));
        ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, std::string{kShaderJson}));
    }

    void TearDown() override {
        FILE_WATCHER.stop();
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    [[nodiscard]] AssetId metaGuid(const VirtualPath& sourcePath) const {
        const auto meta = loadAssetMeta(assetMetaPath(sourcePath));
        EXPECT_TRUE(meta.has_value()) << sourcePath.string();
        return meta ? meta->assetId : AssetId{};
    }

    std::filesystem::path root;
    std::filesystem::path assets;
    VirtualPath shaderPath;
    VirtualPath renamedPath;
};

TEST_F(AssetRenameGuidTest, GuidPersistsAfterRename) {
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(shaderPath));

    const AssetId originalGuid = metaGuid(shaderPath);
    ASSERT_TRUE(originalGuid.valid());
    const auto beforeRecord = ASSET_DATABASE.findByPath(shaderPath);
    ASSERT_TRUE(beforeRecord.has_value());
    EXPECT_EQ(beforeRecord->id, originalGuid);

    // Start the watcher first so its snapshot contains the pre-rename state, then
    // perform the physical rename and let a scan surface the Renamed event.
    ASSERT_TRUE(FILE_WATCHER.start(VirtualPath{"assets://"}, std::chrono::milliseconds{100}, false));

    // Physically rename both the source and its .meta sidecar.
    const auto oldPhysical = FILE_SYSTEM.resolvePhysicalPath(shaderPath);
    const auto newPhysical = FILE_SYSTEM.resolvePhysicalPath(renamedPath);
    ASSERT_TRUE(oldPhysical);
    ASSERT_TRUE(newPhysical);
    const auto oldMetaPhysical = FILE_SYSTEM.resolvePhysicalPath(assetMetaPath(shaderPath));
    const auto newMetaPhysical = FILE_SYSTEM.resolvePhysicalPath(assetMetaPath(renamedPath));
    ASSERT_TRUE(oldMetaPhysical);
    ASSERT_TRUE(newMetaPhysical);

    std::error_code error;
    std::filesystem::create_directories(newPhysical->parent_path(), error);
    ASSERT_FALSE(error);
    std::filesystem::rename(*oldPhysical, *newPhysical, error);
    ASSERT_FALSE(error);
    std::filesystem::rename(*oldMetaPhysical, *newMetaPhysical, error);
    ASSERT_FALSE(error);

    FILE_WATCHER.scanNow();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    ASSET_IMPORT_PIPELINE.processFileEvents();

    const AssetId afterGuid = metaGuid(renamedPath);
    EXPECT_EQ(afterGuid, originalGuid);

    const auto afterRecord = ASSET_DATABASE.findByPath(renamedPath);
    ASSERT_TRUE(afterRecord.has_value());
    EXPECT_EQ(afterRecord->id, originalGuid);
    EXPECT_EQ(afterRecord->sourcePath, renamedPath);
    EXPECT_EQ(afterRecord->metaPath, assetMetaPath(renamedPath));

    // Old path should no longer resolve.
    EXPECT_FALSE(ASSET_DATABASE.findByPath(shaderPath).has_value());
}

} // namespace

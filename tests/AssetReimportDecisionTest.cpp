#include <gtest/gtest.h>

#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/FileWatcher.h"
#include "asset/importer/MaterialAssetImporter.h"
#include "core/filesystem/FileSystem.h"
#include "core/hash.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace engine;
using Json = nlohmann::json;

std::string shaderSource(std::string_view name) {
    return std::string{R"json({
  "$schemaVersion": 1,
  "name": ")json"} +
           std::string{name} + R"json(",
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
}

std::string materialSource() {
    return R"json({
  "$schemaVersion": 1,
  "name": "Reimport Material",
  "shader": "shaders/first.shader.json",
  "properties": { "BaseColor": [0.25, 0.5, 0.75, 1.0] },
  "keywords": [],
  "renderQueue": 2450
})json";
}

// 每个用例挂载独立的临时项目目录，AssetDatabase 从空 library 起步。
class AssetReimportDecisionTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineAssetReimportTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets / "shaders", error);
        std::filesystem::create_directories(assets / "materials", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        FILE_WATCHER.stop();

        shaderPath = VirtualPath{"assets://shaders/first.shader.json"};
        materialPath = VirtualPath{"assets://materials/test.material.json"};
        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.vert"},
                                          "#version 450\nvoid main(){gl_Position=vec4(0);}\n"));
        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.frag"},
                                          "#version 450\nlayout(location=0) out vec4 c;"
                                          "void main(){c=vec4(1);}\n"));
        ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, shaderSource("Reimport/First")));
        ASSERT_TRUE(FILE_SYSTEM.writeText(materialPath, materialSource()));
    }

    void TearDown() override {
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path assets;
    VirtualPath shaderPath;
    VirtualPath materialPath;
};

TEST_F(AssetReimportDecisionTest, PersistsSettingsAndDependencyHashes) {
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    const auto record = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(record.has_value());
    const std::uint64_t expectedSettingsHash =
        MaterialAssetImporter{}.createDefaultSettings(materialPath)->hash();
    const auto shaderHash = hashFile(shaderPath);
    ASSERT_TRUE(shaderHash.has_value());
    EXPECT_EQ(record->settingsHash, expectedSettingsHash);
    ASSERT_EQ(record->dependencies, std::vector<VirtualPath>{shaderPath});
    ASSERT_EQ(record->dependencyHashes.size(), 1U);
    EXPECT_EQ(record->dependencyHashes.front(), *shaderHash);

    // save/load 往返保留新字段。
    ASSERT_TRUE(ASSET_DATABASE.save());
    ASSET_DATABASE.clear();
    ASSERT_TRUE(ASSET_DATABASE.load());
    const auto reloaded = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->settingsHash, record->settingsHash);
    EXPECT_EQ(reloaded->dependencyHashes, record->dependencyHashes);
}

TEST_F(AssetReimportDecisionTest, ReimportsWhenDependencySourceChanges) {
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto before = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before->dependencyHashes.size(), 1U);
    const std::uint64_t staleHash = before->dependencyHashes.front();

    // 只改 Shader 源文件，Material 自身不变：依赖哈希快照必须触发 Material 重导入。
    ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, shaderSource("Reimport/FirstReloaded")));
    const auto changedHash = hashFile(shaderPath);
    ASSERT_TRUE(changedHash.has_value());
    ASSERT_NE(*changedHash, staleHash);

    EXPECT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    const auto after = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->status, AssetImportStatus::Imported);
    ASSERT_EQ(after->dependencyHashes.size(), 1U);
    EXPECT_EQ(after->dependencyHashes.front(), *changedHash);
}

TEST_F(AssetReimportDecisionTest, ReimportsWhenSettingsHashDiffers) {
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto record = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(record.has_value());

    // 模拟设置变化：篡改记录中的 settingsHash 后，下一次导入必须重走完整路径。
    AssetRecord tampered = *record;
    tampered.settingsHash = 0xDEADBEEF;
    ASSERT_TRUE(ASSET_DATABASE.addOrUpdate(tampered));

    EXPECT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    const auto after = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->settingsHash, record->settingsHash);
    EXPECT_NE(after->settingsHash, 0xDEADBEEF);
}

TEST_F(AssetReimportDecisionTest, KeepsSnapshotWhenNothingChanges) {
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto first = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(first.has_value());

    // 无变化时 up-to-date 快速路径直接命中，快照保持稳定。
    EXPECT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto second = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->dependencyHashes, first->dependencyHashes);
    EXPECT_EQ(second->settingsHash, first->settingsHash);
}

TEST_F(AssetReimportDecisionTest, DiscardsLegacyRecordsWithoutNewFields) {
    // 旧格式记录（缺 settings_hash / dependency_hashes）在加载时被丢弃，
    // 由下一次导入重新生成新字段；数据库 version 保持 1。
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto record = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(record.has_value());

    // 构造完整记录后裁掉新字段，得到合法的旧格式 JSON。
    Json legacyRecord{{"asset_id", record->id.toString()},
                      {"asset_type", assetTypeName(record->type)},
                      {"source_path", record->sourcePath.string()},
                      {"meta_path", record->metaPath.string()},
                      {"artifact_path", record->artifactPath.string()},
                      {"importer_version", 4},
                      {"source_hash", 1},
                      {"meta_hash", 2},
                      {"artifact_hash", 3},
                      {"dependencies", Json::array()},
                      {"status", "Imported"},
                      {"last_error", ""}};
    legacyRecord.erase("settings_hash");
    legacyRecord.erase("dependency_hashes");
    const std::string legacyJson =
        Json{{"version", 1}, {"assets", Json::array({legacyRecord})}}.dump(2);
    ASSERT_TRUE(FILE_SYSTEM.writeText(ASSET_DATABASE.databasePath(), legacyJson));

    ASSERT_TRUE(ASSET_DATABASE.load());
    EXPECT_TRUE(ASSET_DATABASE.records().empty());

    // 丢弃后的资产在下一次导入中重新生成完整记录。
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));
    const auto reimported = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(reimported.has_value());
    EXPECT_EQ(reimported->status, AssetImportStatus::Imported);
    EXPECT_EQ(reimported->dependencyHashes.size(), reimported->dependencies.size());
    EXPECT_NE(reimported->settingsHash, 0U);
}

} // namespace

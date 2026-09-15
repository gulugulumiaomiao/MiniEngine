#include <gtest/gtest.h>

#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/FileWatcher.h"
#include "core/filesystem/FileSystem.h"
#include "core/math/Math.h"
#include "TestAssetEnvironment.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace engine;

std::string shaderSource(std::string_view name, bool withTexture = false) {
    std::string properties = R"json(
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] })json";
    if (withTexture) {
        properties += R"json(,
    { "name": "MainTex", "type": "Texture2D", "default": "textures/ref.png" })json";
    }
    return std::string{R"json({
  "$schemaVersion": 1,
  "name": ")json"} +
           std::string{name} + R"json(",
  "properties": [)json" +
           properties + R"json(],
  "subShader": {
    "passes": [{
      "name": "Forward",
      "lightMode": "Forward",
      "program": { "vertex": "simple.vert", "frag": "simple.frag" }
    }]
  }
})json";
}

std::string meshSource(float leftX) {
    const std::array positions{
        math::Vec3{leftX, -1.0F, 0.0F},
        math::Vec3{1.0F, -1.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
    };
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : std::as_bytes(std::span{positions})) {
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    }
    nlohmann::json mesh{
        {"name", "Scheduler Mesh"},
        {"index_type", "uint16"},
        {"usage", "dynamic"},
        {"bindings", {{{"binding", 0}, {"stride", sizeof(math::Vec3)}}}},
        {"attributes",
         {{{"semantic", "position"},
           {"format", "vec3_float32"},
           {"location", 0},
           {"binding", 0},
           {"offset", 0}}}},
        {"vertex_streams", {{{"binding", 0}, {"vertex_count", 3}, {"bytes", bytes}}}},
        {"indices", {0, 1, 2}},
    };
    return mesh.dump();
}

// 每个用例挂载独立的临时项目目录，AssetDatabase 随之从空 library 起步，
// 保证“冷启动直接导入依赖型资产”的前提成立。
class AssetDependencySchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineAssetSchedulerTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets / "shaders", error);
        std::filesystem::create_directories(assets / "materials", error);
        std::filesystem::create_directories(assets / "meshes", error);
        std::filesystem::create_directories(assets / "scenes", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        FILE_WATCHER.stop();
    }

    void TearDown() override {
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path assets;
};

TEST_F(AssetDependencySchedulerTest, ImportsShaderBeforeMaterial) {
    const VirtualPath shaderPath{"assets://shaders/first.shader.json"};
    const VirtualPath materialPath{"assets://materials/test.material.json"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, shaderSource("Scheduler/First")));
    ASSERT_TRUE(FILE_SYSTEM.writeText(materialPath, R"json({
  "$schemaVersion": 1,
  "name": "Scheduler Material",
  "shader": "shaders/first.shader.json",
  "properties": { "BaseColor": [0.25, 0.5, 0.75, 1.0] },
  "keywords": [],
  "renderQueue": 2450
})json"));

    // 冷启动：不经过 scanAll，直接导入 material。调度器必须先导入其依赖的 Shader。
    EXPECT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    const auto shaderRecord = ASSET_DATABASE.findByPath(shaderPath);
    ASSERT_TRUE(materialRecord.has_value());
    ASSERT_TRUE(shaderRecord.has_value());
    EXPECT_EQ(materialRecord->status, AssetImportStatus::Imported);
    EXPECT_EQ(shaderRecord->status, AssetImportStatus::Imported);
    const std::vector<VirtualPath> expectedDependencies{shaderPath};
    EXPECT_EQ(materialRecord->dependencies, expectedDependencies);
    EXPECT_TRUE(FILE_SYSTEM.isFile(shaderRecord->artifactPath));
    EXPECT_TRUE(FILE_SYSTEM.isFile(materialRecord->artifactPath));
}

TEST_F(AssetDependencySchedulerTest, ImportsTransitiveSceneChain) {
    const VirtualPath shaderPath{"assets://shaders/first.shader.json"};
    const VirtualPath materialPath{"assets://materials/test.material.json"};
    const VirtualPath meshPath{"assets://meshes/test.mesh.json"};
    const VirtualPath scenePath{"assets://scenes/test.scene.json"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, shaderSource("Scheduler/First")));
    ASSERT_TRUE(FILE_SYSTEM.writeText(materialPath, R"json({
  "$schemaVersion": 1,
  "name": "Scheduler Material",
  "shader": "shaders/first.shader.json",
  "properties": { "BaseColor": [0.25, 0.5, 0.75, 1.0] },
  "keywords": [],
  "renderQueue": 2450
})json"));
    ASSERT_TRUE(FILE_SYSTEM.writeText(meshPath, meshSource(-1.0F)));
    ASSERT_TRUE(FILE_SYSTEM.writeText(scenePath, R"json({
  "$schemaVersion": 1,
  "name": "Scheduler Scene",
  "nodes": [{
    "id": 1,
    "name": "Triangle",
    "components": [
      {"type": "Transform"},
      {"type": "Mesh", "mesh": "../meshes/test.mesh.json"},
      {"type": "Material", "materials": ["../materials/test.material.json"]}
    ]
  }]
})json"));

    // scene → (mesh, material)，material → shader：递归两层依赖全部按需先导入。
    EXPECT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(scenePath));

    for (const VirtualPath& path : {scenePath, materialPath, meshPath, shaderPath}) {
        const auto record = ASSET_DATABASE.findByPath(path);
        ASSERT_TRUE(record.has_value()) << path.string();
        EXPECT_EQ(record->status, AssetImportStatus::Imported) << path.string();
        EXPECT_TRUE(FILE_SYSTEM.isFile(record->artifactPath)) << path.string();
    }
    const auto sceneRecord = ASSET_DATABASE.findByPath(scenePath);
    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    const std::vector<VirtualPath> expectedSceneDependencies{materialPath, meshPath};
    const std::vector<VirtualPath> expectedMaterialDependencies{shaderPath};
    EXPECT_EQ(sceneRecord->dependencies, expectedSceneDependencies);
    EXPECT_EQ(materialRecord->dependencies, expectedMaterialDependencies);
}

TEST_F(AssetDependencySchedulerTest, ReportsFailureWhenTextureDependencyIsMissing) {
    const VirtualPath shaderPath{"assets://shaders/textured.shader.json"};
    const VirtualPath materialPath{"assets://materials/missing-texture.material.json"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, shaderSource("Scheduler/Textured", true)));
    ASSERT_TRUE(FILE_SYSTEM.writeText(materialPath, R"json({
  "$schemaVersion": 1,
  "name": "Missing Texture Material",
  "shader": "shaders/textured.shader.json",
  "properties": { "MainTex": "textures/missing.png" },
  "keywords": [],
  "renderQueue": 2450
})json"));

    // Texture 源文件不存在：依赖导入失败必须级联为 Material 的导入失败。
    EXPECT_FALSE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    // 调度按声明顺序执行：Shader 已成功导入，Material 因缺失的 Texture 失败。
    const auto shaderRecord = ASSET_DATABASE.findByPath(shaderPath);
    ASSERT_TRUE(shaderRecord.has_value());
    EXPECT_EQ(shaderRecord->status, AssetImportStatus::Imported);

    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(materialRecord.has_value());
    EXPECT_EQ(materialRecord->status, AssetImportStatus::Failed);
    EXPECT_NE(materialRecord->lastError.find("Dependency import failed"), std::string::npos);
}

TEST_F(AssetDependencySchedulerTest, ReportsFailureWhenShaderSourceIsMissing) {
    const VirtualPath materialPath{"assets://materials/missing-shader.material.json"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(materialPath, R"json({
  "$schemaVersion": 1,
  "name": "Missing Shader Material",
  "shader": "shaders/absent.shader.json",
  "properties": {},
  "keywords": [],
  "renderQueue": 2450
})json"));

    // Shader 源文件缺失时 gatherDependencies 无法声明依赖，失败在 import 阶段暴露。
    EXPECT_FALSE(ASSET_IMPORT_PIPELINE.importAsset(materialPath));

    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(materialRecord.has_value());
    EXPECT_EQ(materialRecord->status, AssetImportStatus::Failed);
    EXPECT_FALSE(materialRecord->lastError.empty());
}

} // namespace

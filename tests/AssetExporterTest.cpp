#include <gtest/gtest.h>

#include "asset/base/AssetReference.h"
#include "asset/base/GenericAsset.h"
#include "asset/database/AssetDatabase.h"
#include "asset/exporter/AssetExportPipeline.h"
#include "asset/exporter/AssetExporterRegistry.h"
#include "asset/exporter/GenericAssetExporter.h"
#include "asset/exporter/MaterialAssetExporter.h"
#include "asset/exporter/SceneAssetExporter.h"
#include "asset/importer/FileWatcher.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "scene/scene/SceneAsset.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace engine;

// 测试 shader：覆盖 Float / Color / Boolean / Texture2D 四类属性 + 一个 feature
// keyword；program 指向同目录的 vert/frag（导入只 parse JSON，不编译 GLSL）。
constexpr std::string_view kShaderJson = R"JSON({
  "$schemaVersion": 1,
  "name": "Test/ExportShader",
  "properties": [
    { "name": "Shininess", "displayName": "Shininess", "type": "Float", "default": 32.0 },
    { "name": "BaseColor", "displayName": "Base Color", "type": "Color", "default": [1.0, 1.0, 1.0, 1.0] },
    { "name": "EnableFeature", "displayName": "Enable Feature", "type": "Bool", "default": false },
    { "name": "MainTex", "displayName": "Main Tex", "type": "Texture2D", "default": "" }
  ],
  "subShaders": [{
    "tags": { "renderPipeline": "MiniForward", "queue": "Opaque" },
    "passes": [{
      "name": "Forward",
      "state": { "cull": "Back", "frontFace": "CW", "fill": "Solid", "topology": "TriangleList", "depthWrite": true, "depthTest": "LessEqual", "blend": "Off", "colorMask": "RGBA" },
      "program": { "vertex": "export_test.Forward.vert", "frag": "export_test.Forward.frag" },
      "vertexInput": [{ "name": "position", "semantic": "POSITION", "type": "Vec3", "location": 0 }],
      "varyings": [],
      "fragmentOutputs": [{ "name": "color", "type": "Vec4", "location": 0 }],
      "features": ["FEATURE_A"]
    }]
  }]
}
)JSON";

// 初始 material 源用路径引用；写回时数据库已知这些资产，应升级为 guid://。
constexpr std::string_view kMaterialJson = R"JSON({
  "$schemaVersion": 1,
  "name": "Export Test Material",
  "shader": "assets://shaders/export_test.shader.json",
  "properties": {
    "Shininess": 64.0,
    "BaseColor": [0.2, 0.4, 0.6, 1.0],
    "EnableFeature": true,
    "MainTex": "assets://textures/checker.png"
  },
  "keywords": ["FEATURE_A"]
}
)JSON";

// 带 renderQueue override 的第二个 material：验证 override 的往返保留。
constexpr std::string_view kQueuedMaterialJson = R"JSON({
  "$schemaVersion": 1,
  "name": "Queued Material",
  "shader": "assets://shaders/export_test.shader.json",
  "renderQueue": 2450,
  "properties": {}
}
)JSON";

void writeDiskFile(const std::filesystem::path& path, std::string_view content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    ASSERT_FALSE(error);
    std::ofstream file{path, std::ios::binary};
    ASSERT_TRUE(file.is_open());
    file << content;
}

// 每个用例挂载独立的临时项目目录：源文件在挂载前就位，一次 scanAll 完成导入。
class AssetExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineAssetExporterTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        writeDiskFile(assets / "shaders" / "export_test.shader.json", kShaderJson);
        writeDiskFile(assets / "shaders" / "export_test.Forward.vert", "void main() {}\n");
        writeDiskFile(assets / "shaders" / "export_test.Forward.frag", "void main() {}\n");
        writeDiskFile(assets / "materials" / "export_test.material.json", kMaterialJson);
        writeDiskFile(assets / "materials" / "queued.material.json", kQueuedMaterialJson);
        std::error_code error;
        std::filesystem::create_directories(assets / "textures", error);
        ASSERT_FALSE(error);
        std::filesystem::copy_file(std::filesystem::path{MINI_TEST_BUILTIN_DIR} / "samples" /
                                       "textures" / "checker.png",
                                   assets / "textures" / "checker.png",
                                   error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        // 停掉文件监听：写回产物由测试显式 loadAsset 导入，避免异步重导入竞态。
        FILE_WATCHER.stop();
        ASSERT_TRUE(ASSET_EXPORT_PIPELINE.initialized());
    }

    void TearDown() override {
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    [[nodiscard]] Material loadRuntimeMaterial(const VirtualPath& path) {
        const auto source = ASSET_MANAGER.loadAsset<MaterialAsset>(path);
        EXPECT_TRUE(source);
        const ShaderHandle shaderHandle = SHADER_MANAGER.load(source->shader);
        EXPECT_TRUE(shaderHandle);
        return source->instantiate(shaderHandle);
    }

    std::filesystem::path root;
    std::filesystem::path assets;
};

// 注册表只承载引擎内可编辑类型：Material/Scene/Generic 命中，Shader 为空。
TEST_F(AssetExporterTest, RegistryRoutesEditableTypes) {
    AssetExporterRegistry registry;
    EXPECT_TRUE(registry.registerExporter(std::make_unique<MaterialAssetExporter>()));
    EXPECT_TRUE(registry.registerExporter(std::make_unique<SceneAssetExporter>()));
    EXPECT_TRUE(registry.registerExporter(std::make_unique<GenericAssetExporter>()));
    EXPECT_NE(registry.find(AssetType::Material), nullptr);
    EXPECT_NE(registry.find(AssetType::Scene), nullptr);
    EXPECT_NE(registry.find(AssetType::Generic), nullptr);
    EXPECT_EQ(registry.find(AssetType::Shader), nullptr);
    // 一个类型只允许一个 exporter；重复注册被拒。
    EXPECT_FALSE(registry.registerExporter(std::make_unique<MaterialAssetExporter>()));
}

// 运行时 Material 修改 → saveMaterial 写回 → parse + 导入管线重新加载，逐字段等值。
TEST_F(AssetExporterTest, MaterialRoundtripThroughRuntime) {
    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    const auto source = ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    ASSERT_TRUE(source);
    Material material = source->instantiate(SHADER_MANAGER.load(source->shader));
    material.setFloat("Shininess", 128.0F);
    material.setVec4("BaseColor", math::Vec4{0.9F, 0.8F, 0.7F, 0.6F});
    material.setBool("EnableFeature", false);

    const VirtualPath targetPath{"assets://materials/roundtrip.material.json"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.saveMaterial(material, targetPath, error)) << error;

    const auto reloaded = ASSET_MANAGER.loadAsset<MaterialAsset>(targetPath);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->name, "Export Test Material");
    EXPECT_EQ(reloaded->shader, source->shader);
    EXPECT_EQ(reloaded->keywords, source->keywords);

    ASSERT_TRUE(reloaded->properties.contains("Shininess"));
    EXPECT_FLOAT_EQ(std::get<float>(reloaded->properties.at("Shininess")), 128.0F);
    ASSERT_TRUE(reloaded->properties.contains("BaseColor"));
    const math::Vec4& baseColor = std::get<math::Vec4>(reloaded->properties.at("BaseColor"));
    EXPECT_FLOAT_EQ(baseColor.x, 0.9F);
    EXPECT_FLOAT_EQ(baseColor.y, 0.8F);
    EXPECT_FLOAT_EQ(baseColor.z, 0.7F);
    EXPECT_FLOAT_EQ(baseColor.w, 0.6F);
    ASSERT_TRUE(reloaded->properties.contains("EnableFeature"));
    EXPECT_FALSE(std::get<bool>(reloaded->properties.at("EnableFeature")));
    ASSERT_TRUE(reloaded->properties.contains("MainTex"));
    // 写回是 guid:// 引用；parse 侧经数据库解析回绝对虚拟路径。
    EXPECT_EQ(std::get<std::string>(reloaded->properties.at("MainTex")),
              "assets://textures/checker.png");
}

// 数据库已知的引用（shader + texture）写出为 guid://，对齐新源文件的引用规范。
TEST_F(AssetExporterTest, MaterialWritesGuidReferences) {
    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    Material material = loadRuntimeMaterial(materialPath);
    const VirtualPath targetPath{"assets://materials/guid_write.material.json"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.saveMaterial(material, targetPath, error)) << error;

    const auto text = FILE_SYSTEM.readText(targetPath);
    ASSERT_TRUE(text);
    const auto shaderGuid =
        ASSET_DATABASE.findGuid(VirtualPath{"assets://shaders/export_test.shader.json"});
    ASSERT_TRUE(shaderGuid);
    EXPECT_NE(text->find(AssetReference{*shaderGuid}.toString()), std::string::npos);
    const auto textureGuid = ASSET_DATABASE.findGuid(VirtualPath{"assets://textures/checker.png"});
    ASSERT_TRUE(textureGuid);
    EXPECT_NE(text->find(AssetReference{*textureGuid}.toString()), std::string::npos);
}

// 省略语义：无 renderQueue override / 空 keywords / 空纹理槽的字段一律省略，
// 不把派生值（shader 默认 queue、默认纹理）显式化。
TEST_F(AssetExporterTest, MaterialOmitsOptionalFields) {
    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    Material material = loadRuntimeMaterial(materialPath);
    material.keywords.clear();
    material.setTexture("MainTex", "");

    const VirtualPath targetPath{"assets://materials/omitted.material.json"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.saveMaterial(material, targetPath, error)) << error;

    const auto text = FILE_SYSTEM.readText(targetPath);
    ASSERT_TRUE(text);
    EXPECT_EQ(text->find("renderQueue"), std::string::npos);
    EXPECT_EQ(text->find("keywords"), std::string::npos);
    EXPECT_EQ(text->find("MainTex"), std::string::npos);
}

// 源文件带 renderQueue override 时，运行时提取保留 override 并在写回中还原。
TEST_F(AssetExporterTest, MaterialPreservesRenderQueueOverride) {
    const VirtualPath queuedPath{"assets://materials/queued.material.json"};
    const auto queued = ASSET_MANAGER.loadAsset<MaterialAsset>(queuedPath);
    ASSERT_TRUE(queued);
    ASSERT_EQ(queued->renderQueue, std::optional<int>{2450});

    Material material = queued->instantiate(SHADER_MANAGER.load(queued->shader));
    ASSERT_EQ(material.renderQueueOverride(), std::optional<int>{2450});
    const VirtualPath targetPath{"assets://materials/queued_out.material.json"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.saveMaterial(material, targetPath, error)) << error;

    const auto reloaded = ASSET_MANAGER.loadAsset<MaterialAsset>(targetPath);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->renderQueue, std::optional<int>{2450});
}

// 失败路径：目标扩展名不符、非 assets:// 目标、无绑定 shader 的 Material、以及
// 没有写回器的类型（Shader）都明确失败且携带原因。
TEST_F(AssetExporterTest, MaterialRejectsBadTargetsAndUnexportableTypes) {
    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    Material material = loadRuntimeMaterial(materialPath);
    std::string error;

    EXPECT_FALSE(ASSET_EXPORT_PIPELINE.saveMaterial(material,
                                                    VirtualPath{"assets://materials/out.txt"},
                                                    error));
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(ASSET_EXPORT_PIPELINE.saveMaterial(material,
                                                    VirtualPath{"library://out.material.json"},
                                                    error));
    EXPECT_FALSE(error.empty());
    error.clear();
    // 默认构造的 Material 没有绑定 ShaderHandle，提取阶段即失败。
    Material orphan;
    EXPECT_FALSE(ASSET_EXPORT_PIPELINE.saveMaterial(
        orphan, VirtualPath{"assets://materials/orphan.material.json"}, error));
    EXPECT_NE(error.find("Shader"), std::string::npos);
    error.clear();
    // Shader 无引擎内编辑工作流，没有写回器：路由失败必须显式报错。
    ShaderAsset shaderAsset;
    EXPECT_FALSE(ASSET_EXPORT_PIPELINE.exportAsset(
        shaderAsset, VirtualPath{"assets://shaders/nowhere.shader.json"}, error));
    EXPECT_NE(error.find("No exporter"), std::string::npos);
}

// SceneAsset 经管线写出 .scene.json，导入管线可重新加载且节点字段一致。
TEST_F(AssetExporterTest, SceneWriteViaPipeline) {
    SceneAsset sceneAsset;
    sceneAsset.name = "Pipeline Scene";
    TransformComponentAsset transform;
    transform.position = math::Vec3{1.0F, 2.0F, 3.0F};
    sceneAsset.nodes.emplace_back(1U, std::nullopt, "Root", true,
                                  std::vector<SceneComponentAsset>{std::move(transform)});

    const VirtualPath targetPath{"assets://scenes/pipeline.scene.json"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.exportAsset(sceneAsset, targetPath, error)) << error;

    const auto reloaded = ASSET_MANAGER.loadAsset<SceneAsset>(targetPath);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->name, "Pipeline Scene");
    ASSERT_EQ(reloaded->nodes.size(), 1U);
    EXPECT_EQ(reloaded->nodes.front().name, "Root");
    const auto* reloadedTransform =
        std::get_if<TransformComponentAsset>(&reloaded->nodes.front().components.front());
    ASSERT_TRUE(reloadedTransform);
    EXPECT_FLOAT_EQ(reloadedTransform->position.x, 1.0F);
    EXPECT_FLOAT_EQ(reloadedTransform->position.y, 2.0F);
    EXPECT_FLOAT_EQ(reloadedTransform->position.z, 3.0F);
}

// GenericAsset 透传写回：data 字节原样落盘。
TEST_F(AssetExporterTest, GenericRoundtrip) {
    GenericAsset asset;
    const std::string payload = "generic payload \xE2\x9C\x93";
    asset.data.assign(reinterpret_cast<const std::byte*>(payload.data()),
                      reinterpret_cast<const std::byte*>(payload.data()) + payload.size());
    const VirtualPath targetPath{"assets://notes/exported.bin"};
    std::string error;
    ASSERT_TRUE(ASSET_EXPORT_PIPELINE.exportAsset(asset, targetPath, error)) << error;

    const auto bytes = FILE_SYSTEM.readBinary(targetPath);
    ASSERT_TRUE(bytes);
    const std::string text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    EXPECT_EQ(text, payload);
}

} // namespace

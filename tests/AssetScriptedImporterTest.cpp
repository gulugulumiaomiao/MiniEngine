#include <gtest/gtest.h>

#include "asset/base/AssetMeta.h"
#include "asset/base/GenericAsset.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportHelpers.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/DefaultAssetImporter.h"
#include "core/filesystem/FileWatcher.h"
#include "asset/importer/ScriptedImporter.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/math/hash.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace engine;

// 标记前缀证明 Artifact 数据由本 stub 产出，而非 DefaultImporter 透传。
constexpr std::string_view kStubMarker = "stub:";

class StubScriptedImporter final : public ScriptedImporter {
public:
    StubScriptedImporter(std::string extension, AssetType type, std::uint32_t version)
        : extension_{std::move(extension)}, type_{type}, version_{version} {}

    [[nodiscard]] AssetType assetType() const override { return type_; }
    [[nodiscard]] std::uint32_t version() const override { return version_; }
    [[nodiscard]] std::string sourceExtension() const override { return extension_; }
    [[nodiscard]] std::string outputExtension() const override { return extension_; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath&) const override {
        return std::make_unique<GenericImportSettings>();
    }
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext&, const AssetImportSettings&) const override {
        return {};
    }
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings&) const override {
        const auto source = FILE_SYSTEM.readBinary(context.sourcePath);
        if (!source)
            return AssetImportResult::failed(assetType(), "Cannot read source file");
        GenericAsset asset;
        asset.data.reserve(kStubMarker.size() + source->size());
        asset.data.insert(asset.data.end(),
                          reinterpret_cast<const std::byte*>(kStubMarker.data()),
                          reinterpret_cast<const std::byte*>(kStubMarker.data()) +
                              kStubMarker.size());
        asset.data.insert(asset.data.end(), source->begin(), source->end());
        return writeAssetArtifact(context, asset, assetType());
    }

private:
    std::string extension_;
    AssetType type_;
    std::uint32_t version_;
};

// 每个用例挂载独立的临时项目目录，AssetDatabase 从空 library 起步。
class AssetScriptedImporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineScriptedImporterTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets / "notes", error);
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

// 没有专用 Importer 的扩展名：显式 importAsset 走 DefaultImporter 透传，
// 源字节原样进 Artifact，AssetManager 按 Generic 加载。
TEST_F(AssetScriptedImporterTest, DefaultImporterPassesUnknownFilesThrough) {
    const std::string payload = "plain text payload \xE2\x9C\x93";
    const VirtualPath textPath{"assets://notes/readme.txt"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(textPath, payload));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(textPath));

    const auto record = ASSET_DATABASE.findByPath(textPath);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->type, AssetType::Generic);
    EXPECT_EQ(record->importerVersion, 1U);
    EXPECT_EQ(record->settingsHash, GenericImportSettings{}.hash());
    EXPECT_TRUE(record->dependencies.empty());

    const std::shared_ptr<GenericAsset> asset =
        ASSET_MANAGER.loadAsset<GenericAsset>(textPath);
    ASSERT_TRUE(asset);
    const std::string data{reinterpret_cast<const char*>(asset->data.data()),
                           asset->data.size()};
    EXPECT_EQ(data, payload);
}

// .meta 是导入管线的伴生文件，任何路由下都不允许成为资产本体。
TEST_F(AssetScriptedImporterTest, DefaultImporterRejectsMetaSidecars) {
    const VirtualPath textPath{"assets://notes/readme.txt"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(textPath, "payload"));
    ASSERT_FALSE(ASSET_IMPORT_PIPELINE.importAsset(VirtualPath{textPath.string() + ".meta"}));
    EXPECT_FALSE(ASSET_DATABASE.findByPath(VirtualPath{textPath.string() + ".meta"}).has_value());
}

// scanAll 不做兜底：未知扩展名的文件不会被被动扫描进资产库。
TEST_F(AssetScriptedImporterTest, ScanAllSkipsPassthroughExtensions) {
    const VirtualPath textPath{"assets://notes/readme.txt"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(textPath, "payload"));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.scanAll());
    EXPECT_FALSE(ASSET_DATABASE.findByPath(textPath).has_value());
}

// ScriptedImporter 按扩展名接管路由：产出带标记的 Generic 资产。
TEST_F(AssetScriptedImporterTest, ScriptedImporterTakesOverExtension) {
    const VirtualPath objPath{"assets://notes/model.obj"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(objPath, "v 0 0 0"));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".obj", AssetType::Generic, 7)));

    // 扫描路由：scripted 扩展名参与 scanAll。
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.scanAll());
    const auto record = ASSET_DATABASE.findByPath(objPath);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->type, AssetType::Generic);
    EXPECT_EQ(record->importerVersion, 7U);

    const std::shared_ptr<GenericAsset> asset = ASSET_MANAGER.loadAsset<GenericAsset>(objPath);
    ASSERT_TRUE(asset);
    const std::string data{reinterpret_cast<const char*>(asset->data.data()), asset->data.size()};
    EXPECT_EQ(data, std::string{kStubMarker} + "v 0 0 0");
}

// ScriptedImporter 优先于内置路由：接管 .shader.json 后不再走内置 Shader 导入。
TEST_F(AssetScriptedImporterTest, ScriptedImporterOverridesBuiltinRoute) {
    const VirtualPath shaderPath{"assets://notes/curve.shader.json"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, "{}"));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".shader.json", AssetType::Generic, 42)));

    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(shaderPath));
    const auto record = ASSET_DATABASE.findByPath(shaderPath);
    ASSERT_TRUE(record.has_value());
    // 若走了内置 ShaderAssetImporter，版本是 3 且解析 "{}" 会失败。
    EXPECT_EQ(record->type, AssetType::Generic);
    EXPECT_EQ(record->importerVersion, 42U);

    const std::shared_ptr<GenericAsset> asset =
        ASSET_MANAGER.loadAsset<GenericAsset>(shaderPath);
    ASSERT_TRUE(asset);
    const std::string data{reinterpret_cast<const char*>(asset->data.data()), asset->data.size()};
    EXPECT_EQ(data, std::string{kStubMarker} + "{}");
}

// 路由类型变化时 Meta 重新生成；GUID 由路径确定性派生，资产身份保持稳定。
TEST_F(AssetScriptedImporterTest, ScriptedImporterRegeneratesMetaOnTypeChange) {
    const VirtualPath modelPath{"assets://notes/prop.model"};
    ASSERT_TRUE(FILE_SYSTEM.writeText(modelPath, "binary-ish"));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(modelPath));

    const auto passthrough = ASSET_DATABASE.findByPath(modelPath);
    ASSERT_TRUE(passthrough.has_value());
    ASSERT_EQ(passthrough->type, AssetType::Generic);
    const AssetId originalId = passthrough->id;

    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".model", AssetType::Mesh, 9)));
    ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(modelPath));

    const auto scripted = ASSET_DATABASE.findByPath(modelPath);
    ASSERT_TRUE(scripted.has_value());
    EXPECT_EQ(scripted->type, AssetType::Mesh);
    EXPECT_EQ(scripted->importerVersion, 9U);
    EXPECT_EQ(scripted->id, originalId);

    // 重新生成的 Meta 与路由类型一致。
    const auto meta = loadAssetMeta(assetMetaPath(modelPath));
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->assetType, AssetType::Mesh);
}

// 注册表单元行为：重复扩展名拒绝、无前导点拒绝、匹配大小写不敏感、
// 多级扩展名比单级更具体者优先。
TEST(AssetScriptedImporterRegistryTest, RegistryValidatesAndRoutesExtensions) {
    AssetImporterRegistry registry;

    EXPECT_FALSE(registry.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>("obj", AssetType::Generic, 1)));
    EXPECT_TRUE(registry.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".obj", AssetType::Generic, 1)));
    EXPECT_FALSE(registry.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".OBJ", AssetType::Generic, 2)));

    const ScriptedImporter* found =
        registry.findScripted(VirtualPath{"assets://meshes/statue.OBJ"});
    ASSERT_TRUE(found);
    EXPECT_EQ(found->version(), 1U);

    // 更具体的注册优先：.shader.json 覆盖 .json。
    EXPECT_TRUE(registry.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".json", AssetType::Generic, 3)));
    EXPECT_TRUE(registry.registerScriptedImporter(
        std::make_unique<StubScriptedImporter>(".shader.json", AssetType::Generic, 4)));
    const ScriptedImporter* shaderRoute =
        registry.findScripted(VirtualPath{"assets://shaders/x.shader.json"});
    ASSERT_TRUE(shaderRoute);
    EXPECT_EQ(shaderRoute->version(), 4U);
    const ScriptedImporter* plainRoute = registry.findScripted(VirtualPath{"assets://x.json"});
    ASSERT_TRUE(plainRoute);
    EXPECT_EQ(plainRoute->version(), 3U);

    EXPECT_EQ(registry.findScripted(VirtualPath{"assets://meshes/empty"}), nullptr);
}

} // namespace

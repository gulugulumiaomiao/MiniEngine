#include <gtest/gtest.h>

#include "asset/base/AssetId.h"
#include "asset/database/AssetDatabase.h"
#include "asset/exporter/AssetPackage.h"
#include "core/filesystem/FileWatcher.h"
#include "asset/manager/AssetManager.h"
#include "core/archive/ZipArchive.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "TestAssetEnvironment.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace engine;

// program 指向同目录 vert/frag；vert 内 local include common.glsl，common.glsl
// 再 include deep.glsl——覆盖闭包收集的三条边：数据库依赖图（material→shader/
// texture）、Shader program 伴随文件、GLSL 文本 include 递归。
constexpr std::string_view kShaderJson = R"JSON({
  "$schemaVersion": 1,
  "name": "Test/PackageShader",
  "properties": [
    { "name": "BaseColor", "displayName": "Base Color", "type": "Color", "default": [1.0, 1.0, 1.0, 1.0] },
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
      "features": []
    }]
  }]
}
)JSON";

constexpr std::string_view kMaterialJson = R"JSON({
  "$schemaVersion": 1,
  "name": "Package Test Material",
  "shader": "assets://shaders/export_test.shader.json",
  "properties": {
    "BaseColor": [0.2, 0.4, 0.6, 1.0],
    "MainTex": "assets://textures/checker.png"
  }
}
)JSON";

constexpr std::string_view kVertSource = R"GLSL(#include "common.glsl"
void main() { gl_Position = vec4(kDeepFactor); }
)GLSL";

constexpr std::string_view kFragSource = "void main() {}\n";

constexpr std::string_view kCommonGlsl = R"GLSL(#include "deep.glsl"
vec3 commonShade(vec3 color) { return color; }
)GLSL";

constexpr std::string_view kDeepGlsl = "const float kDeepFactor = 0.5;\n";

void writeDiskFile(const std::filesystem::path& path, std::string_view content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    ASSERT_FALSE(error);
    std::ofstream file{path, std::ios::binary};
    ASSERT_TRUE(file.is_open());
    file << content;
}

std::string toString(std::span<const std::byte> data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

std::vector<std::byte> bytesOf(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data()) + text.size()};
}

void writeBinaryDiskFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    ASSERT_FALSE(error);
    std::ofstream file{path, std::ios::binary};
    ASSERT_TRUE(file.is_open());
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

class AssetPackageTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineAssetPackageTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        writeDiskFile(assets / "shaders" / "export_test.shader.json", kShaderJson);
        writeDiskFile(assets / "shaders" / "export_test.Forward.vert", kVertSource);
        writeDiskFile(assets / "shaders" / "export_test.Forward.frag", kFragSource);
        writeDiskFile(assets / "shaders" / "common.glsl", kCommonGlsl);
        writeDiskFile(assets / "shaders" / "deep.glsl", kDeepGlsl);
        writeDiskFile(assets / "materials" / "export_test.material.json", kMaterialJson);
        std::error_code error;
        std::filesystem::create_directories(assets / "textures", error);
        ASSERT_FALSE(error);
        std::filesystem::copy_file(std::filesystem::path{MINI_TEST_BUILTIN_DIR} / "samples" /
                                       "textures" / "checker.png",
                                   assets / "textures" / "checker.png",
                                   error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        // 停掉文件监听：包内容落盘后由测试显式控制导入时机，避免异步竞态。
        FILE_WATCHER.stop();
    }

    void TearDown() override {
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    [[nodiscard]] std::vector<VirtualPath> materialRoot() const {
        return {VirtualPath{"assets://materials/export_test.material.json"}};
    }

    [[nodiscard]] const AssetPackageEntry*
    findEntry(const std::vector<AssetPackageEntry>& entries, std::string_view path) const {
        const auto it = std::find_if(entries.begin(), entries.end(),
                                     [&](const AssetPackageEntry& entry) {
                                         return entry.path.string() == path;
                                     });
        return it == entries.end() ? nullptr : &*it;
    }

    std::filesystem::path root;
    std::filesystem::path assets;
};

// 闭包完整性：material 根 → 数据库依赖（shader + texture）→ Shader program 伴随
// 文件（vert/frag）→ GLSL local include 递归（common → deep）。资产条目带
// guid/type，伴随条目只有 path。
TEST_F(AssetPackageTest, ClosureCollectsDatabaseAndShaderCompanionDependencies) {
    std::string error;
    const std::vector<AssetPackageEntry> entries = collectExportClosure(materialRoot(), error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(entries.size(), 7U);

    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    const AssetPackageEntry* material = findEntry(entries, materialPath.string());
    ASSERT_NE(material, nullptr);
    ASSERT_TRUE(material->isAsset());
    ASSERT_TRUE(material->guid.has_value());
    EXPECT_EQ(*material->guid, AssetId::fromPath(materialPath));
    EXPECT_EQ(material->type, AssetType::Material);

    const VirtualPath shaderPath{"assets://shaders/export_test.shader.json"};
    const AssetPackageEntry* shader = findEntry(entries, shaderPath.string());
    ASSERT_NE(shader, nullptr);
    ASSERT_TRUE(shader->isAsset());
    EXPECT_EQ(*shader->guid, AssetId::fromPath(shaderPath));
    EXPECT_EQ(shader->type, AssetType::Shader);

    const VirtualPath texturePath{"assets://textures/checker.png"};
    const AssetPackageEntry* texture = findEntry(entries, texturePath.string());
    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(texture->isAsset());
    EXPECT_EQ(texture->type, AssetType::Texture);

    for (std::string_view companion : {"assets://shaders/export_test.Forward.vert",
                                       "assets://shaders/export_test.Forward.frag",
                                       "assets://shaders/common.glsl",
                                       "assets://shaders/deep.glsl"}) {
        const AssetPackageEntry* entry = findEntry(entries, companion);
        ASSERT_NE(entry, nullptr) << companion;
        EXPECT_FALSE(entry->isAsset()) << companion;
        EXPECT_EQ(entry->type, AssetType::Unknown) << companion;
    }
}

// 包布局：manifest.json + assets/ 镜像条目（源 + 资产 meta），源字节逐位一致，
// manifest 结构与 guid 派生值一致。
TEST_F(AssetPackageTest, PackageLayoutMirrorsSourcesAndManifest) {
    std::string error;
    std::vector<std::byte> package;
    ASSERT_TRUE(exportAssetPackageToBuffer(materialRoot(), package, error)) << error;
    ASSERT_FALSE(package.empty());

    const auto reader = ZipReader::open(package);
    ASSERT_TRUE(reader);
    // 7 源 + 3 meta（material/shader/texture）+ manifest。
    EXPECT_EQ(reader->entryCount(), 11U);
    EXPECT_TRUE(reader->hasEntry("manifest.json"));
    EXPECT_TRUE(reader->hasEntry("assets/materials/export_test.material.json"));
    EXPECT_TRUE(reader->hasEntry("assets/materials/export_test.material.json.meta"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/export_test.shader.json"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/export_test.shader.json.meta"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/export_test.Forward.vert"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/export_test.Forward.frag"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/common.glsl"));
    EXPECT_TRUE(reader->hasEntry("assets/shaders/deep.glsl"));
    EXPECT_TRUE(reader->hasEntry("assets/textures/checker.png"));
    EXPECT_TRUE(reader->hasEntry("assets/textures/checker.png.meta"));

    // 源字节镜像：包内条目与磁盘逐位一致。
    const std::pair<std::string_view, std::string_view> byteMirrors[] = {
        {"assets://materials/export_test.material.json",
         "assets/materials/export_test.material.json"},
        {"assets://shaders/common.glsl", "assets/shaders/common.glsl"},
    };
    for (const auto& [virtualPath, entryName] : byteMirrors) {
        const auto onDisk = FILE_SYSTEM.readBinary(VirtualPath{virtualPath});
        ASSERT_TRUE(onDisk);
        const auto inPackage = reader->extract(entryName);
        ASSERT_TRUE(inPackage);
        EXPECT_EQ(*inPackage, *onDisk);
    }

    // manifest：schemaVersion、条目数、资产/伴随条目字段形态、guid 派生值。
    const auto manifestBytes = reader->extract("manifest.json");
    ASSERT_TRUE(manifestBytes);
    const nlohmann::json manifest =
        nlohmann::json::parse(toString(*manifestBytes), nullptr, false);
    ASSERT_FALSE(manifest.is_discarded());
    ASSERT_TRUE(manifest.is_object());
    EXPECT_EQ(manifest.at("$schemaVersion").get<int>(), 1);
    ASSERT_TRUE(manifest.contains("entries"));
    const nlohmann::json& jsonEntries = manifest.at("entries");
    ASSERT_EQ(jsonEntries.size(), 7U);

    const nlohmann::json& materialEntry = jsonEntries.at(0);
    EXPECT_EQ(materialEntry.at("path").get<std::string>(),
              "materials/export_test.material.json");
    EXPECT_EQ(materialEntry.at("type").get<std::string>(), "Material");
    EXPECT_EQ(materialEntry.at("guid").get<std::string>(),
              AssetId::fromPath(VirtualPath{"assets://materials/export_test.material.json"})
                  .toString());

    // 伴随文件条目（排序第二位 common.glsl）只有 path。
    const nlohmann::json& companionEntry = jsonEntries.at(1);
    EXPECT_EQ(companionEntry.at("path").get<std::string>(), "shaders/common.glsl");
    EXPECT_EQ(companionEntry.size(), 1U);
}

// exportAssetPackage：buffer 版之外的落盘形态，产物是可解析的 zip。
TEST_F(AssetPackageTest, ExportAssetPackageWritesArchiveFile) {
    std::string error;
    const VirtualPath packagePath{"assets://export_test.mepackage"};
    ASSERT_TRUE(exportAssetPackage(materialRoot(), packagePath, error)) << error;

    const auto bytes = FILE_SYSTEM.readBinary(packagePath);
    ASSERT_TRUE(bytes);
    const auto reader = ZipReader::open(*bytes);
    ASSERT_TRUE(reader);
    EXPECT_TRUE(reader->hasEntry("manifest.json"));
    EXPECT_EQ(reader->entryCount(), 11U);
}

// 失败路径：根不存在 / 非 assets:// 根 / 空根集合——均不产出包字节。
TEST_F(AssetPackageTest, ExportFailsForMissingOrNonAssetRoot) {
    std::string error;
    std::vector<std::byte> package;

    std::vector<VirtualPath> roots{VirtualPath{"assets://materials/missing.material.json"}};
    EXPECT_FALSE(exportAssetPackageToBuffer(roots, package, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(package.empty());

    error.clear();
    roots = {VirtualPath{"library://whatever.bin"}};
    EXPECT_FALSE(exportAssetPackageToBuffer(roots, package, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(package.empty());

    error.clear();
    EXPECT_FALSE(exportAssetPackageToBuffer({}, package, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(package.empty());
}

// 导入往返：包导入全新环境后源字节逐位一致、meta 镜像、数据库重建可加载。
TEST_F(AssetPackageTest, ImportRoundtripIntoFreshEnvironment) {
    std::string error;
    std::vector<std::byte> package;
    ASSERT_TRUE(exportAssetPackageToBuffer(materialRoot(), package, error)) << error;

    // 对照字节：环境切换前从源磁盘抓取。
    const auto materialBytes =
        FILE_SYSTEM.readBinary(VirtualPath{"assets://materials/export_test.material.json"});
    const auto vertBytes =
        FILE_SYSTEM.readBinary(VirtualPath{"assets://shaders/export_test.Forward.vert"});
    ASSERT_TRUE(materialBytes);
    ASSERT_TRUE(vertBytes);

    // 全新环境：空 assets 里只放包文件。
    const std::filesystem::path freshRoot = root / "fresh";
    std::error_code fsError;
    std::filesystem::create_directories(freshRoot / "assets", fsError);
    ASSERT_FALSE(fsError);
    writeBinaryDiskFile(freshRoot / "assets" / "package.mepackage", package);

    test::shutdownAssetEnvironment();
    ASSERT_TRUE(test::initializeAssetEnvironment(freshRoot / "assets"));
    FILE_WATCHER.stop();

    AssetPackageImportReport report;
    std::string importError;
    EXPECT_TRUE(importAssetPackage(VirtualPath{"assets://package.mepackage"},
                                   AssetPackageConflictPolicy::Abort, report, importError))
        << importError;
    ASSERT_EQ(report.imported.size(), 3U); // material + shader + texture
    EXPECT_TRUE(report.skipped.empty());
    EXPECT_TRUE(report.failed.empty());

    // 源字节逐位一致（资产源 + 伴随文件），meta 镜像存在。
    EXPECT_EQ(*FILE_SYSTEM.readBinary(VirtualPath{"assets://materials/export_test.material.json"}),
              *materialBytes);
    EXPECT_EQ(*FILE_SYSTEM.readBinary(VirtualPath{"assets://shaders/export_test.Forward.vert"}),
              *vertBytes);
    EXPECT_TRUE(FILE_SYSTEM.exists(
        VirtualPath{"assets://materials/export_test.material.json.meta"}));

    // 数据库重建：material 有导入成功的 record，依赖闭包完整。
    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    const auto record = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(record);
    EXPECT_EQ(record->type, AssetType::Material);
    EXPECT_EQ(record->status, AssetImportStatus::Imported);
    const auto hasDependency = [&](std::string_view path) {
        return std::any_of(record->dependencies.begin(), record->dependencies.end(),
                           [&](const VirtualPath& dependency) {
                               return dependency.string() == path;
                           });
    };
    EXPECT_TRUE(hasDependency("assets://shaders/export_test.shader.json"));
    EXPECT_TRUE(hasDependency("assets://textures/checker.png"));

    // 重建后 loadAsset 可用。
    const auto material = ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    ASSERT_TRUE(material);
    EXPECT_EQ(material->name, "Package Test Material");
}

// 冲突三策略：Skip 不触碰冲突条目继续其余；Overwrite 覆盖重建；Abort 预检
// 失败零落盘（排在冲突条目之后的文件不被写入）。
TEST_F(AssetPackageTest, ImportConflictPolicies) {
    std::string error;
    std::vector<std::byte> package;
    ASSERT_TRUE(exportAssetPackageToBuffer(materialRoot(), package, error)) << error;
    const auto materialBytes =
        FILE_SYSTEM.readBinary(VirtualPath{"assets://materials/export_test.material.json"});
    ASSERT_TRUE(materialBytes);

    // 全新环境 + 预置冲突的 material 源（内容与包不同）。
    const std::filesystem::path freshRoot = root / "fresh";
    std::error_code fsError;
    std::filesystem::create_directories(freshRoot / "assets" / "materials", fsError);
    ASSERT_FALSE(fsError);
    {
        std::ofstream file{freshRoot / "assets" / "materials" / "export_test.material.json",
                           std::ios::binary};
        ASSERT_TRUE(file.is_open());
        file << "conflict-placeholder";
    }
    writeBinaryDiskFile(freshRoot / "assets" / "package.mepackage", package);

    test::shutdownAssetEnvironment();
    ASSERT_TRUE(test::initializeAssetEnvironment(freshRoot / "assets"));
    FILE_WATCHER.stop();

    const VirtualPath materialPath{"assets://materials/export_test.material.json"};
    const VirtualPath packagePath{"assets://package.mepackage"};

    // Skip：material 保持冲突内容，shader/texture 正常导入。
    {
        AssetPackageImportReport report;
        std::string importError;
        EXPECT_TRUE(importAssetPackage(packagePath, AssetPackageConflictPolicy::Skip, report,
                                       importError))
            << importError;
        const auto conflictBytes = FILE_SYSTEM.readBinary(materialPath);
        ASSERT_TRUE(conflictBytes);
        EXPECT_EQ(toString(*conflictBytes), "conflict-placeholder");
        ASSERT_EQ(report.skipped.size(), 1U);
        EXPECT_EQ(report.skipped.front().string(), materialPath.string());
        EXPECT_EQ(report.imported.size(), 2U);
        EXPECT_TRUE(report.failed.empty());
    }

    // Overwrite：material 被包内容覆盖，全部资产条目重建。
    {
        AssetPackageImportReport report;
        std::string importError;
        EXPECT_TRUE(importAssetPackage(packagePath, AssetPackageConflictPolicy::Overwrite, report,
                                       importError))
            << importError;
        const auto restored = FILE_SYSTEM.readBinary(materialPath);
        ASSERT_TRUE(restored);
        EXPECT_EQ(*restored, *materialBytes);
        EXPECT_EQ(report.imported.size(), 3U);
        EXPECT_TRUE(report.skipped.empty());
        EXPECT_TRUE(report.failed.empty());
    }

    // Abort：删掉排在最前的 material（无冲突）保留已存在的 shader（冲突）→
    // 预检整体失败，material 零落盘——证明预检先于任何写入。
    {
        std::error_code removeError;
        std::filesystem::remove(freshRoot / "assets" / "materials" / "export_test.material.json",
                                removeError);
        std::filesystem::remove(
            freshRoot / "assets" / "materials" / "export_test.material.json.meta", removeError);
        AssetPackageImportReport report;
        std::string importError;
        EXPECT_FALSE(importAssetPackage(packagePath, AssetPackageConflictPolicy::Abort, report,
                                        importError));
        EXPECT_FALSE(importError.empty());
        EXPECT_FALSE(FILE_SYSTEM.exists(materialPath));
        EXPECT_TRUE(report.imported.empty());
    }
}

// 拒绝损坏包：坏 zip、manifest 缺失、schemaVersion 不符、路径逃逸、guid 与派生
// 值不符、zip 与 manifest 条目不对应。
TEST_F(AssetPackageTest, ImportRejectsCorruptPackages) {
    std::string error;
    std::vector<std::byte> package;
    ASSERT_TRUE(exportAssetPackageToBuffer(materialRoot(), package, error)) << error;

    const VirtualPath packagePath{"assets://corrupt.mepackage"};
    AssetPackageImportReport report;
    std::string importError;
    const auto tryImport = [&](std::span<const std::byte> bytes) {
        if (!FILE_SYSTEM.writeBinaryAtomic(packagePath, bytes))
            return false;
        report = {};
        importError.clear();
        return importAssetPackage(packagePath, AssetPackageConflictPolicy::Overwrite, report,
                                  importError);
    };

    // 坏 zip（非归档字节）。
    EXPECT_FALSE(tryImport(bytesOf("this is not a zip archive")));

    // manifest 缺失：合法 zip 但没有 manifest.json。
    {
        ZipWriter writer;
        ASSERT_TRUE(writer.addEntry("assets/stray.txt", bytesOf("stray")));
        std::vector<std::byte> noManifest;
        ASSERT_TRUE(writer.finalize(noManifest));
        EXPECT_FALSE(tryImport(noManifest));
    }

    // 重写 manifest：其余 zip 条目原样保留，只替换 manifest 内容。
    const auto rewriteManifest = [&](const nlohmann::json& manifest) {
        std::vector<std::byte> rebuilt;
        const auto source = ZipReader::open(package);
        if (!source)
            return rebuilt;
        ZipWriter writer;
        for (const std::string& name : source->entryNames()) {
            if (name == "manifest.json")
                continue;
            const auto data = source->extract(name);
            if (!data || !writer.addEntry(name, *data))
                return rebuilt;
        }
        if (!writer.addEntry("manifest.json", bytesOf(manifest.dump(2) + "\n")))
            return rebuilt;
        (void)writer.finalize(rebuilt);
        return rebuilt;
    };

    const auto reader = ZipReader::open(package);
    ASSERT_TRUE(reader);
    const auto manifestBytes = reader->extract("manifest.json");
    ASSERT_TRUE(manifestBytes);
    const nlohmann::json manifest = nlohmann::json::parse(toString(*manifestBytes), nullptr, false);
    ASSERT_FALSE(manifest.is_discarded());

    // schemaVersion 不符。
    {
        nlohmann::json bad = manifest;
        bad["$schemaVersion"] = 2;
        const std::vector<std::byte> rebuilt = rewriteManifest(bad);
        ASSERT_FALSE(rebuilt.empty());
        EXPECT_FALSE(tryImport(rebuilt));
    }

    // 路径逃逸（..）。
    {
        nlohmann::json bad = manifest;
        bad["entries"][0]["path"] = "../evil.txt";
        const std::vector<std::byte> rebuilt = rewriteManifest(bad);
        ASSERT_FALSE(rebuilt.empty());
        EXPECT_FALSE(tryImport(rebuilt));
    }

    // guid 与派生值不符（包被篡改）。
    {
        nlohmann::json bad = manifest;
        bad["entries"][0]["guid"] = AssetId::generate().toString();
        const std::vector<std::byte> rebuilt = rewriteManifest(bad);
        ASSERT_FALSE(rebuilt.empty());
        EXPECT_FALSE(tryImport(rebuilt));
    }

    // zip 与 manifest 条目不对应（manifest 少声明一个条目，zip 里多余）。
    {
        nlohmann::json bad = manifest;
        bad["entries"].erase(1); // 去掉 common.glsl 伴随条目
        const std::vector<std::byte> rebuilt = rewriteManifest(bad);
        ASSERT_FALSE(rebuilt.empty());
        EXPECT_FALSE(tryImport(rebuilt));
    }
}

} // namespace

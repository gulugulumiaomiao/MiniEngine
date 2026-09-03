#include "asset/AssetArtifact.h"
#include "asset/AssetDatabase.h"
#include "asset/AssetMeta.h"
#include "core/io/FileSystem.h"

#include <chrono>
#include <filesystem>

int main() {
    using namespace engine;

    const AssetId id = AssetId::generate();
    const auto parsedId = AssetId::parse(id.toString());
    if (!id.valid() || !parsedId || *parsedId != id ||
        AssetId::parse("not-an-asset-id")) {
        return 1;
    }

    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mini-vulkan-asset-test-" + std::to_string(unique));
    const std::filesystem::path assetRoot = root / "assets";
    const std::filesystem::path libraryRoot = root / "library";
    std::error_code error;
    std::filesystem::create_directories(assetRoot, error);
    std::filesystem::create_directories(libraryRoot, error);
    if (error || !FILE_SYSTEM.mountDirectory("asset", assetRoot) ||
        !FILE_SYSTEM.mountDirectory("library", libraryRoot)) {
        return 2;
    }

    const VirtualPath shaderPath{"asset://shaders/test.shader.json"};
    const VirtualPath materialPath{"asset://materials/test.material.json"};
    if (!FILE_SYSTEM.writeText(shaderPath, "{}") ||
        !FILE_SYSTEM.writeText(materialPath, "{}") ||
        inferAssetType(shaderPath) != AssetType::Shader ||
        inferAssetType(materialPath) != AssetType::Material) {
        return 3;
    }

    const auto shaderMeta = createAssetMeta(shaderPath);
    const auto materialMeta = createAssetMeta(materialPath);
    if (!shaderMeta || !materialMeta ||
        shaderMeta->assetType != AssetType::Shader ||
        materialMeta->assetType != AssetType::Material) {
        return 4;
    }
    const auto loadedShaderMeta = loadAssetMeta(assetMetaPath(shaderPath));
    if (!loadedShaderMeta || loadedShaderMeta->assetId != shaderMeta->assetId) {
        return 5;
    }

    AssetDatabase& database = ASSET_DATABASE;
    database.clear();
    if (!database.initialize()) {
        return 6;
    }
    AssetRecord shaderRecord;
    shaderRecord.id = shaderMeta->assetId;
    shaderRecord.type = AssetType::Shader;
    shaderRecord.sourcePath = shaderPath;
    shaderRecord.metaPath = assetMetaPath(shaderPath);
    shaderRecord.artifactPath = database.artifactPath(shaderMeta->assetId);
    shaderRecord.importerVersion = 1;
    shaderRecord.sourceHash = 10;
    shaderRecord.metaHash = 20;
    shaderRecord.artifactHash = 30;
    shaderRecord.status = AssetImportStatus::Imported;

    AssetRecord materialRecord;
    materialRecord.id = materialMeta->assetId;
    materialRecord.type = AssetType::Material;
    materialRecord.sourcePath = materialPath;
    materialRecord.metaPath = assetMetaPath(materialPath);
    materialRecord.artifactPath = database.artifactPath(materialMeta->assetId);
    materialRecord.importerVersion = 1;
    materialRecord.dependencies.push_back(shaderPath);
    if (!database.addOrUpdate(shaderRecord) ||
        !database.addOrUpdate(materialRecord) || !database.save()) {
        return 7;
    }
    const auto found = database.findByPath(shaderPath);
    const auto dependents = database.dependentsOf(shaderPath);
    if (!found || found->id != shaderMeta->assetId || dependents.size() != 1 ||
        dependents.front().string() != materialPath.string()) {
        return 8;
    }

    if (!database.prepareArtifactDirectory(shaderMeta->assetId)) {
        return 9;
    }
    const AssetArtifact artifact{1, shaderMeta->assetId, AssetType::Shader,
                                 shaderPath, R"({"name":"Test"})"};
    const VirtualPath artifactPath = database.artifactPath(shaderMeta->assetId);
    if (!saveAssetArtifact(artifactPath, artifact)) {
        return 10;
    }
    const auto loadedArtifact = loadAssetArtifact(artifactPath);
    if (!loadedArtifact || loadedArtifact->assetId != shaderMeta->assetId ||
        loadedArtifact->assetType != AssetType::Shader ||
        loadedArtifact->sourcePath.string() != shaderPath.string()) {
        return 11;
    }

    database.clear();
    if (!database.load() || !database.findById(shaderMeta->assetId)) {
        return 12;
    }
    database.shutdown();
    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    std::filesystem::remove_all(root, error);
    return error ? 13 : 0;
}

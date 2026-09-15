#include "asset/importer/SceneAssetImporter.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/SceneAssetFormat.h"
#include "asset/importer/AssetImportHelpers.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace engine {
namespace {

// 收集 Scene 声明的依赖（Mesh / Material 组件引用的源资产）。import 与
// gatherDependencies 共用，保证两份依赖列表一致。
[[nodiscard]] std::vector<VirtualPath> collectSceneDependencies(const SceneAsset& scene) {
    std::vector<VirtualPath> dependencies;
    for (const SceneNodeAsset& node : scene.nodes) {
        for (const SceneComponentAsset& component : node.components) {
            std::visit(
                [&dependencies](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, MeshComponentAsset>) {
                        if (value.sourceType == MeshComponentSourceType::Asset)
                            dependencies.push_back(value.mesh);
                    } else if constexpr (std::is_same_v<T, MaterialComponentAsset>) {
                        dependencies.insert(
                            dependencies.end(), value.materials.begin(), value.materials.end());
                    }
                },
                component);
        }
    }
    return dependencies;
}

} // namespace

bool SceneImportSettings::transfer(Transfer& archive) {
    (void)archive;
    return true;
}

Hash64 SceneImportSettings::hash() const {
    return hashString("SceneImportSettings");
}

std::unique_ptr<AssetImportSettings>
SceneAssetImporter::createDefaultSettings(const VirtualPath&) const {
    return std::make_unique<SceneImportSettings>();
}

std::vector<VirtualPath> SceneAssetImporter::gatherDependencies(
    const AssetImportContext& context, const AssetImportSettings&) const {
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return {};
    }
    const std::shared_ptr<SceneAsset> scene =
        format::parseSceneAsset(context.sourcePath, *source, ASSET_DATABASE);
    if (!scene) {
        return {};
    }
    return collectSceneDependencies(*scene);
}

AssetImportResult SceneAssetImporter::import(const AssetImportContext& context,
                                             const AssetImportSettings&) const {
    const auto fail = [](std::string error) {
        Log::error("SceneAssetImporter", "%s", error.c_str());
        return AssetImportResult::failed(AssetType::Scene, std::move(error));
    };
    if (context.meta.assetType != AssetType::Scene || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid()) {
        return fail("Invalid Scene import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return fail("Cannot read SceneAsset: " + context.sourcePath.string());
    }
    const std::shared_ptr<SceneAsset> scene =
        format::parseSceneAsset(context.sourcePath, *source, ASSET_DATABASE);
    if (!scene) {
        return fail("Cannot parse SceneAsset: " + context.sourcePath.string());
    }

    return writeAssetArtifact(
        context, *scene, AssetType::Scene, collectSceneDependencies(*scene));
}

} // namespace engine

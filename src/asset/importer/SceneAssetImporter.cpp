#include "asset/importer/SceneAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "scene/scene/SceneAsset.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace engine {

AssetImportResult SceneAssetImporter::import(const AssetImportContext& context) const {
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
    const std::shared_ptr<SceneAsset> scene = detail::parseSceneAsset(context.sourcePath, *source);
    if (!scene) {
        return fail("Cannot parse SceneAsset: " + context.sourcePath.string());
    }

    std::vector<VirtualPath> dependencies;
    for (const SceneNodeAsset& node : scene->nodes) {
        for (const SceneComponentAsset& component : node.components) {
            std::visit(
                [&dependencies](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, MeshComponentAsset>) {
                        dependencies.push_back(value.mesh);
                    } else if constexpr (std::is_same_v<T, MaterialComponentAsset>) {
                        dependencies.insert(
                            dependencies.end(), value.materials.begin(), value.materials.end());
                    }
                },
                component);
        }
    }
    std::ranges::sort(dependencies, {}, &VirtualPath::string);
    dependencies.erase(std::ranges::unique(dependencies, {}, &VirtualPath::string).begin(),
                       dependencies.end());

    BinaryWriter writer;
    if (!scene->transfer(writer)) {
        return fail("Cannot serialize SceneAsset: " + context.sourcePath.string());
    }
    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return fail("Cannot prepare Scene Artifact: " + context.artifactPath.string());
    }
    const AssetArtifact artifact{
        1, context.meta.assetId, AssetType::Scene, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return fail("Cannot save Scene Artifact: " + context.artifactPath.string());
    }
    return AssetImportResult::succeeded(
        AssetType::Scene, context.artifactPath, std::move(dependencies));
}

} // namespace engine

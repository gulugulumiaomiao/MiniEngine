#include "asset/importer/MaterialAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "asset/database/AssetDatabase.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"

#include <utility>
#include <algorithm>

namespace engine {

AssetImportResult MaterialAssetImporter::import(const AssetImportContext& context) const {
    const auto fail = [](std::string error) {
        Log::error("MaterialAssetImporter", "%s", error.c_str());
        return AssetImportResult::failed(AssetType::Material, std::move(error));
    };
    if (context.meta.assetType != AssetType::Material || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid()) {
        return fail("Invalid Material import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return fail("Cannot read MaterialAsset: " + context.sourcePath.string());
    }
    const std::shared_ptr<MaterialAsset> material =
        detail::parseMaterialAsset(context.sourcePath, *source);
    if (!material) {
        return fail("Cannot parse MaterialAsset: " + context.sourcePath.string());
    }
    const auto shaderRecord = ASSET_DATABASE.findByPath(material->shader);
    if (!shaderRecord || shaderRecord->status != AssetImportStatus::Imported ||
        shaderRecord->type != AssetType::Shader) {
        return fail("Material Shader has not been imported: " + material->shader.string());
    }
    const auto shaderArtifact = loadAssetArtifact(shaderRecord->artifactPath);
    if (!shaderArtifact || shaderArtifact->assetId != shaderRecord->id ||
        shaderArtifact->assetType != AssetType::Shader) {
        return fail("Cannot load Material Shader Artifact: " + material->shader.string());
    }
    auto shader = std::make_shared<ShaderAsset>();
    shader->setAssetIdentity(shaderRecord->id, shaderRecord->sourcePath);
    BinaryReader shaderReader{shaderArtifact->payload};
    if (!shader->transfer(shaderReader) || !shaderReader.finished()) {
        return fail("Cannot parse Material Shader Artifact: " + material->shader.string());
    }
    if (!validateMaterialAsset(*material, *shader, context.sourcePath)) {
        return fail("Material properties do not match Shader: " + context.sourcePath.string());
    }
    std::vector<VirtualPath> dependencies{material->shader};
    for (const ShaderPropertyDesc& property : shader->properties) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const auto override = material->properties.find(property.name);
        const ShaderValue& value =
            override == material->properties.end() ? property.defaultValue : override->second;
        const std::string* reference = std::get_if<std::string>(&value);
        if (!reference || reference->empty())
            continue;
        VirtualPath texturePath{*reference};
        if (!texturePath.valid())
            texturePath = VirtualPath{"asset://" + *reference};
        const auto textureRecord = ASSET_DATABASE.findByPath(texturePath);
        if (!texturePath.valid() || !textureRecord ||
            textureRecord->status != AssetImportStatus::Imported ||
            textureRecord->type != AssetType::Texture) {
            return fail("Material Texture has not been imported: " + *reference);
        }
        if (override != material->properties.end())
            override->second = texturePath.string();
        if (std::ranges::find(dependencies, texturePath) == dependencies.end())
            dependencies.push_back(std::move(texturePath));
    }
    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return fail("Cannot prepare Material Artifact: " + context.artifactPath.string());
    }
    BinaryWriter writer;
    if (!material->transfer(writer)) {
        return fail("Cannot serialize Material Artifact: " + context.sourcePath.string());
    }
    const AssetArtifact artifact{
        1, context.meta.assetId, AssetType::Material, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return fail("Cannot save Material Artifact: " + context.artifactPath.string());
    }
    return AssetImportResult::succeeded(
        AssetType::Material, context.artifactPath, std::move(dependencies));
}

} // namespace engine

#include "asset/importer/MaterialAssetImporter.h"

#include "asset/database/AssetDatabase.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/format/MaterialAssetFormat.h"
#include "asset/format/ShaderAssetFormat.h"
#include "asset/importer/AssetImportHelpers.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"

#include <algorithm>
#include <utility>

namespace engine {
namespace {

// 收集 Material 声明的依赖（Shader + 实际引用的 Texture）。只解析源文件，
// 不读取依赖 Artifact：管线据此保证依赖先导入。
[[nodiscard]] std::vector<VirtualPath>
collectMaterialDependencies(const VirtualPath& materialPath) {
    const auto source = FILE_SYSTEM.readText(materialPath);
    if (!source) {
        return {};
    }
    const std::shared_ptr<MaterialAsset> material =
        format::parseMaterialAsset(materialPath, *source, ASSET_DATABASE);
    if (!material || inferAssetType(material->shader) != AssetType::Shader) {
        return {};
    }
    const auto shaderSource = FILE_SYSTEM.readText(material->shader);
    const std::shared_ptr<ShaderAsset> shader =
        shaderSource ? format::parseShaderAsset(material->shader, *shaderSource) : nullptr;
    if (!shader) {
        return {};
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
            texturePath = VirtualPath{"assets://" + *reference};
        if (!texturePath.valid() || inferAssetType(texturePath) != AssetType::Texture)
            continue;
        if (std::ranges::find(dependencies, texturePath) == dependencies.end())
            dependencies.push_back(std::move(texturePath));
    }
    return dependencies;
}

} // namespace

bool MaterialImportSettings::transfer(Transfer& archive) {
    (void)archive;
    return true;
}

Hash64 MaterialImportSettings::hash() const {
    return hashString("MaterialImportSettings");
}

std::unique_ptr<AssetImportSettings>
MaterialAssetImporter::createDefaultSettings(const VirtualPath&) const {
    return std::make_unique<MaterialImportSettings>();
}

std::vector<VirtualPath> MaterialAssetImporter::gatherDependencies(
    const AssetImportContext& context, const AssetImportSettings&) const {
    return collectMaterialDependencies(context.sourcePath);
}

AssetImportResult MaterialAssetImporter::import(const AssetImportContext& context,
                                                const AssetImportSettings&) const {
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
        format::parseMaterialAsset(context.sourcePath, *source, ASSET_DATABASE);
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
    if (!format::validateMaterialAsset(*material, *shader, context.sourcePath)) {
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
            texturePath = VirtualPath{"assets://" + *reference};
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
    return writeAssetArtifact(
        context, *material, AssetType::Material, std::move(dependencies));
}

} // namespace engine

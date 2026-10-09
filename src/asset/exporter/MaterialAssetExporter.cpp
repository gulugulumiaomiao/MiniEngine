#include "asset/runtime/ShaderAssetResolver.h"
#include "asset/runtime/MaterialAssetResolver.h"
#include "asset/exporter/MaterialAssetExporter.h"
#include "asset/types/MaterialAsset.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/MaterialAssetFormat.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"

namespace engine {

bool MaterialAssetExporter::supports(const VirtualPath& targetPath) const {
    return targetPath.valid() && isAssetScheme(targetPath.scheme()) &&
           targetPath.relativePath().ends_with(".material.json");
}

AssetExportResult MaterialAssetExporter::write(const Asset& asset,
                                               const VirtualPath& targetPath) const {
    const auto* material = dynamic_cast<const MaterialAsset*>(&asset);
    if (!material)
        return AssetExportResult::failed(AssetType::Material, "Asset is not a MaterialAsset");
    const std::string json = format::writeMaterialAssetJson(*material, ASSET_DATABASE);
    if (!FILE_SYSTEM.writeTextAtomic(targetPath, json))
        return AssetExportResult::failed(AssetType::Material,
                                         "Cannot write " + targetPath.string());
    return AssetExportResult::succeeded(AssetType::Material, targetPath);
}

std::unique_ptr<MaterialAsset>
exportMaterialToAsset(const Material& material, const VirtualPath& targetPath, std::string& error) {
    const Shader* shader = material.shaderRef().get();
    const auto shaderPath = shader ? shaderAssetPath(*shader) : VirtualPath{};
    if (!shaderPath.valid()) {
        error = "Material '" + material.name + "' has no resolvable Shader";
        return nullptr;
    }

    auto asset = std::make_unique<MaterialAsset>();
    asset->setAssetPath(targetPath);
    asset->data.name = material.name;
    asset->data.shader = shaderPath;
    asset->data.keywords = material.keywords;
    asset->data.renderQueue = material.renderQueueOverride();
    for (const ShaderPropertyInfo& property : shader->properties()) {
        switch (property.type) {
        case ShaderPropertyType::Float:
            asset->data.properties.emplace(property.name, material.getFloat(property.name));
            break;
        case ShaderPropertyType::Boolean:
            asset->data.properties.emplace(property.name, material.getBool(property.name));
            break;
        case ShaderPropertyType::Vec2:
            asset->data.properties.emplace(property.name, material.getVec2(property.name));
            break;
        case ShaderPropertyType::Vec3:
            asset->data.properties.emplace(property.name, material.getVec3(property.name));
            break;
        case ShaderPropertyType::Vec4:
            asset->data.properties.emplace(property.name, material.getVec4(property.name));
            break;
        case ShaderPropertyType::Texture2D: {
            // 空纹理槽（未绑定的 Texture2D 属性）不写属性：parse 侧会把缺失属性
            // 解释为 shader 默认值，语义等价且保持源文件最小。
            const auto reference = materialTextureReference(material, property.name);
            if (!reference.empty())
                asset->data.properties.emplace(property.name, reference);
            break;
        }
        case ShaderPropertyType::Matrix:
            asset->data.properties.emplace(property.name, material.getMatrix(property.name));
            break;
        }
    }
    return asset;
}

} // namespace engine

#include "asset/exporter/MaterialAssetExporter.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/MaterialAssetFormat.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"

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

std::unique_ptr<MaterialAsset> exportMaterialToAsset(const Material& material,
                                                     const VirtualPath& targetPath,
                                                     std::string& error) {
    const Shader* shader = SHADER_MANAGER.find(material.shaderHandle());
    if (!shader || !shader->assetPath().valid()) {
        error = "Material '" + material.name + "' has no resolvable Shader";
        return nullptr;
    }

    auto asset = std::make_unique<MaterialAsset>();
    asset->setAssetPath(targetPath);
    asset->name = material.name;
    asset->shader = shader->assetPath();
    asset->keywords = material.keywords;
    asset->renderQueue = material.renderQueueOverride();
    for (const ShaderPropertyDesc& property : shader->properties()) {
        switch (property.type) {
        case ShaderPropertyType::Float:
        case ShaderPropertyType::Range:
            asset->properties.emplace(property.name, material.getFloat(property.name));
            break;
        case ShaderPropertyType::Boolean:
            asset->properties.emplace(property.name, material.getBool(property.name));
            break;
        case ShaderPropertyType::Vec2:
            asset->properties.emplace(property.name, material.getVec2(property.name));
            break;
        case ShaderPropertyType::Vec3:
            asset->properties.emplace(property.name, material.getVec3(property.name));
            break;
        case ShaderPropertyType::Vec4:
        case ShaderPropertyType::Color:
            asset->properties.emplace(property.name, material.getVec4(property.name));
            break;
        case ShaderPropertyType::Texture2D: {
            // 空纹理槽（未绑定的 Texture2D 属性）不写属性：parse 侧会把缺失属性
            // 解释为 shader 默认值，语义等价且保持源文件最小。
            const auto texture = material.textures.find(property.name);
            if (texture != material.textures.end() && !texture->second.empty())
                asset->properties.emplace(property.name, texture->second);
            break;
        }
        }
    }
    return asset;
}

} // namespace engine

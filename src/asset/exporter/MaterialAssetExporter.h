#pragma once

#include "asset/exporter/AssetExporter.h"

#include <memory>
#include <string>

namespace engine {

class Material;
class MaterialAsset;

// 运行时 Material → MaterialAsset 提取（对称 scene 模块的 exportSceneToAsset）。
// error 接收人类可读的失败原因；空结果表示至少有一项无法表达（如 ShaderHandle
// 失效）。renderQueue 只提取 override：写回生效值会把 shader 默认显式化，破坏
// 源文件的省略语义。
[[nodiscard]] std::unique_ptr<MaterialAsset> exportMaterialToAsset(const Material& material,
                                                                   const VirtualPath& targetPath,
                                                                   std::string& error);

// .material.json 写回：format::writeMaterialAssetJson 编码后原子写入。
class MaterialAssetExporter final : public AssetExporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Material; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    [[nodiscard]] bool supports(const VirtualPath& targetPath) const override;
    [[nodiscard]] AssetExportResult
    write(const Asset& asset, const VirtualPath& targetPath) const override;
};

} // namespace engine

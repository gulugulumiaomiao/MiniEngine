#pragma once

#include "asset/base/Asset.h"
#include "asset/base/AssetMeta.h"

#include <string>
#include <utility>

namespace engine {

// 写回结果：AssetImportResult 的对称物。失败携带人类可读原因；成功携带目标路径。
struct AssetExportResult {
    bool success{};
    AssetType type{AssetType::Unknown};
    VirtualPath targetPath;
    std::string error;

    [[nodiscard]] static AssetExportResult failed(AssetType type, std::string error) {
        AssetExportResult result;
        result.success = false;
        result.type = type;
        result.error = std::move(error);
        return result;
    }

    [[nodiscard]] static AssetExportResult succeeded(AssetType type, VirtualPath targetPath) {
        AssetExportResult result;
        result.success = true;
        result.type = type;
        result.targetPath = std::move(targetPath);
        return result;
    }
};

// AssetExporter 是 AssetImporter 的写回侧对称物：CPU Asset → 源格式文件。
// 与导入侧的刻意非对称（写进文档 docs/asset/Exporter.md）：
// - 无 AssetExportSettings：导入设置参与 Artifact hash 失效判定；写回即时执行、
//   无缓存无增量，没有可哈希的东西。
// - 无 gatherDependencies：写回是单资产操作，没有依赖调度。
// write 的输入是 CPU Asset（MaterialAsset/SceneAsset/GenericAsset）；运行时对象
// （Material/Scene）到 Asset 的提取由各 exporter 模块的自由函数承担（对称 scene
// 模块的 exportSceneToAsset）。
class AssetExporter {
public:
    virtual ~AssetExporter() = default;

    [[nodiscard]] virtual AssetType assetType() const = 0;
    [[nodiscard]] virtual std::uint32_t version() const = 0; // exporter 代码版本

    // 目标路径是否可作为该类型的写回目标；默认要求 assets:// 下的有效路径。
    [[nodiscard]] virtual bool supports(const VirtualPath& targetPath) const {
        return targetPath.valid() && isAssetScheme(targetPath.scheme());
    }

    // 执行写回：CPU Asset → 源格式文件（原子写入）。
    [[nodiscard]] virtual AssetExportResult
    write(const Asset& asset, const VirtualPath& targetPath) const = 0;
};

} // namespace engine

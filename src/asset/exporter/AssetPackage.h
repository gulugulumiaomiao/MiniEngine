#pragma once

// .mepackage 导出/导入：源文件级资产分发（对齐 Unity Export/Import Package 语义）。
// 包 = zip 归档（deflate）：manifest.json + assets/<relative-path> 源文件镜像 +
// 资产条目的 .meta 侧车。导出收集依赖闭包（数据库依赖图 + Shader 的 vert/frag
// 伴随文件 + GLSL local include 递归）；导入校验 manifest、按冲突策略落盘并触发
// 导入管线重建数据库记录。文件级镜像不经运行时对象——包机制与 exporter 写回
// 体系正交组合，而非内含。

#include "asset/base/AssetId.h"
#include "asset/base/AssetMeta.h"
#include "core/filesystem/VirtualPath.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine {

// manifest 条目：资产条目（有 guid/type，必有 .meta 伴随）或伴随文件条目
// （vert/frag/glsl 等纯文件，仅 path）。
struct AssetPackageEntry {
    VirtualPath path;
    std::optional<AssetId> guid;
    AssetType type{AssetType::Unknown};

    [[nodiscard]] bool isAsset() const { return guid.has_value(); }
};

// 导入冲突策略：目标路径已存在时的处置。
enum class AssetPackageConflictPolicy {
    Overwrite, // 覆盖源与 meta
    Skip,      // 跳过该条目（源 + meta），继续其余
    Abort,     // 任一冲突立即整体失败，落盘前完成全部预检
};

struct AssetPackageImportReport {
    std::vector<VirtualPath> imported; // 成功落盘且重导入成功
    std::vector<VirtualPath> skipped;  // 冲突跳过（源 + meta 均未触碰）
    std::vector<VirtualPath> failed;   // 落盘成功但重导入失败
};

// 收集 roots 的导出闭包：数据库依赖图（Scene→Mesh/Material→Shader/Texture）+
// Shader program 的 vert/frag 伴随文件 + 文本伴随文件的 local #include 递归。
// 结果按 path 排序，保证相同输入产出相同包字节。
[[nodiscard]] std::vector<AssetPackageEntry>
collectExportClosure(std::span<const VirtualPath> roots, std::string& error);

// 导出闭包到 .mepackage 单文件（zip）。
[[nodiscard]] bool exportAssetPackage(std::span<const VirtualPath> roots,
                                      const VirtualPath& packagePath,
                                      std::string& error);

// 导出闭包到内存 buffer（测试与程序化消费）。
[[nodiscard]] bool exportAssetPackageToBuffer(std::span<const VirtualPath> roots,
                                              std::vector<std::byte>& output,
                                              std::string& error);

// 导入 .mepackage：解析校验 manifest → 冲突预检 → 落盘（源 + meta）→ 对每个
// 资产条目触发 ASSET_IMPORT_PIPELINE.importAsset 重建。report 汇总三组路径；
// 返回 false 表示包本身无效或整体中止，此时不落盘。
[[nodiscard]] bool importAssetPackage(const VirtualPath& packagePath,
                                      AssetPackageConflictPolicy policy,
                                      AssetPackageImportReport& report,
                                      std::string& error);

} // namespace engine

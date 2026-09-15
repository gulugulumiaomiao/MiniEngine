#pragma once

#include "asset/base/AssetMeta.h"
#include "core/hash.h"
#include "core/serialization/Transferable.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace engine {

// Typed per-importer settings (Unity-style ImportSettings). Concrete importers
// subclass this through AssetImportSettingsBase; the pipeline hashes the active
// settings so that changing them invalidates the produced Artifact. Default
// settings live inside the importer, so hand-written settings files are optional.
class AssetImportSettings : public Transferable {
public:
    [[nodiscard]] virtual Hash64 hash() const = 0;
    [[nodiscard]] virtual std::unique_ptr<AssetImportSettings> clone() const = 0;
};

// CRTP helper implementing the polymorphic clone() for copy-constructible settings.
template <typename Derived>
class AssetImportSettingsBase : public AssetImportSettings {
public:
    [[nodiscard]] std::unique_ptr<AssetImportSettings> clone() const override {
        static_assert(std::is_copy_constructible_v<Derived>);
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

struct AssetImportContext {
    AssetMeta meta;                       // .meta 中的 GUID/类型
    VirtualPath sourcePath;               // 源资产路径
    VirtualPath metaPath;                 // .meta 路径（可写）
    VirtualPath artifactPath;             // 输出 Artifact 路径
    std::vector<VirtualPath> searchPaths; // 解析相对引用时的搜索上下文
};

struct AssetImportResult {
    bool success{};
    AssetType type{AssetType::Unknown};
    VirtualPath artifactPath;
    std::vector<VirtualPath> dependencies;
    std::string error;

    [[nodiscard]] static AssetImportResult failed(AssetType type, std::string error);
    [[nodiscard]] static AssetImportResult
    succeeded(AssetType type, VirtualPath artifactPath, std::vector<VirtualPath> dependencies = {});
};

// 把 importer 从"单个函数"扩展为"可配置、可扩展、可声明依赖"的小管线单元：
// 调度方先取默认设置，再通过 gatherDependencies 声明依赖，最后执行 import。
class AssetImporter {
public:
    virtual ~AssetImporter() = default;

    [[nodiscard]] virtual AssetType assetType() const = 0;
    [[nodiscard]] virtual std::uint32_t version() const = 0; // importer 代码版本

    // 是否支持该路径/扩展名；默认返回 true。
    [[nodiscard]] virtual bool supports(const VirtualPath& sourcePath) const;

    // 创建该源文件的默认导入设置。
    [[nodiscard]] virtual std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const = 0;

    // importer 主动声明的依赖源资产列表。此时依赖尚未导入，importer 不得读取
    // 依赖 Artifact；管线保证依赖先于本资产导入。
    [[nodiscard]] virtual std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const = 0;

    // 执行导入：源文件 → CPU Asset → Artifact。
    [[nodiscard]] virtual AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const = 0;
};

} // namespace engine

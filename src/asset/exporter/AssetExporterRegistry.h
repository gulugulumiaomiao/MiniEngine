#pragma once

#include "asset/exporter/AssetExporter.h"

#include <memory>
#include <unordered_map>

namespace engine {

// 写回侧注册表：按 AssetType 路由，一个类型一个 exporter（对称
// AssetImporterRegistry，但没有 Scripted 扩展名表——写回类型集合封闭于
// 引擎内可编辑类型，没有新扩展名接管的需求）。
class AssetExporterRegistry final {
public:
    [[nodiscard]] bool registerExporter(std::unique_ptr<AssetExporter> exporter);
    [[nodiscard]] const AssetExporter* find(AssetType type) const;
    void clear();

private:
    std::unordered_map<AssetType, std::unique_ptr<AssetExporter>> exporters_;
};

} // namespace engine

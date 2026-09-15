#pragma once

#include "asset/importer/AssetImporter.h"
#include "asset/importer/ScriptedImporter.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace engine {

class AssetImporterRegistry final {
public:
    // 内置 importer：按 AssetType 路由（一个类型一个 importer）。
    [[nodiscard]] bool registerImporter(std::unique_ptr<AssetImporter> importer);
    // ScriptedImporter：按源文件扩展名路由（小写，含前导点）。优先级高于
    // 内置路由——注册后该扩展名由脚本接管，目标类型由脚本声明。
    [[nodiscard]] bool registerScriptedImporter(std::unique_ptr<ScriptedImporter> importer);
    [[nodiscard]] const AssetImporter* find(AssetType type) const;
    [[nodiscard]] const ScriptedImporter* findScripted(const VirtualPath& sourcePath) const;
    void clear();

private:
    std::unordered_map<AssetType, std::unique_ptr<AssetImporter>> importers_;
    std::unordered_map<std::string, std::unique_ptr<ScriptedImporter>> scriptedImporters_;
};

} // namespace engine

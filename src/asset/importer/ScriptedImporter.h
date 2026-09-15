#pragma once

// ScriptedImporter：用户/编辑器自定义 importer 的注册点（对齐 Unity 的同名概念）。
// 与内置 importer 按 AssetType 路由不同，ScriptedImporter 按源文件扩展名路由：
// 注册表用 sourceExtension() 作为键，因此一个扩展名（如 .obj）可以接管为任意
// 目标 AssetType（如 Mesh），或者声明 Generic 做透传转换。

#include "asset/importer/AssetImporter.h"

#include <cctype>
#include <string>

namespace engine {

// 注册键的规范化形式：小写化（".OBJ" → ".obj"）。
[[nodiscard]] inline std::string lowercaseExtension(const std::string& extension) {
    std::string normalized;
    normalized.reserve(extension.size());
    for (const char c : extension)
        normalized.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return normalized;
}

// 路径尾部与（已小写化的）扩展名的大小写不敏感后缀匹配，支持多级扩展名
// （.shader.json / .material.json 等）。
[[nodiscard]] bool sourceExtensionMatches(const VirtualPath& sourcePath,
                                          const std::string& extension);

class ScriptedImporter : public AssetImporter {
public:
    // 接管的源文件扩展名（含前导点，如 ".obj"）。注册表按它路由；同一扩展名
    // 只允许注册一个 ScriptedImporter。
    [[nodiscard]] virtual std::string sourceExtension() const = 0;

    // 产出资产的目标扩展名（如 ".mesh.json"）；透传导入器返回源扩展名本身。
    // 仅作元信息，供编辑器 UI 与未来的 exporter 使用。
    [[nodiscard]] virtual std::string outputExtension() const = 0;

    // 默认按扩展名匹配；需要更细粒度控制（如按文件头）时由派生类覆盖。
    [[nodiscard]] bool supports(const VirtualPath& sourcePath) const override;
};

} // namespace engine

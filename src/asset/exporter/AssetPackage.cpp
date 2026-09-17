#include "asset/exporter/AssetPackage.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/ShaderAssetFormat.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/archive/ZipArchive.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view kCategory = "AssetPackage";
constexpr std::uint32_t kManifestSchemaVersion = 1;
constexpr std::string_view kManifestEntryName = "manifest.json";
constexpr std::string_view kAssetsEntryPrefix = "assets/";

[[nodiscard]] std::vector<std::byte> toBytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data()) + text.size()};
}

[[nodiscard]] std::string toString(std::span<const std::byte> data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

// .vert/.frag/.glsl 是文本 shader 源：需要扫描 local #include。
[[nodiscard]] bool isShaderSourceCompanion(const VirtualPath& path) {
    const std::string& relative = path.relativePath();
    return relative.ends_with(".vert") || relative.ends_with(".frag") ||
           relative.ends_with(".glsl");
}

// 包内条目名：assets/ 镜像前缀 + 正斜杠相对路径。
[[nodiscard]] std::string packageEntryName(const VirtualPath& path) {
    return std::string{kAssetsEntryPrefix} + path.relativePath();
}

// 包内路径安全：非空、正斜杠分段、无 "." / ".." / 反斜杠 / 绝对路径——防 zip
// 路径逃逸。分段级检查（"a..b.txt" 合法，"../a" 不合法）。
[[nodiscard]] bool isSafeRelativePath(std::string_view relative) {
    if (relative.empty() || relative.front() == '/' || relative.back() == '/')
        return false;
    if (relative.find('\\') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = relative.find('/', start);
        const std::string_view segment =
            relative.substr(start,
                            end == std::string_view::npos ? end : end - start);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (end == std::string_view::npos)
            return true;
        start = end + 1;
    }
}

[[nodiscard]] std::string_view trimLeft(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    return text.substr(start);
}

// 扫描 shader 文本源的 local `#include "..."`：相对当前文件所在目录解析并入队。
// `#include <...>` 是 search-path 形式，引擎没有搜索路径来源，记 warning 跳过。
void collectLocalIncludes(const VirtualPath& path,
                          std::string_view source,
                          std::deque<VirtualPath>& queue,
                          std::unordered_set<std::string>& queued) {
    const auto enqueue = [&](const VirtualPath& next) {
        if (queued.insert(next.string()).second)
            queue.push_back(next);
    };
    std::size_t position = 0;
    while (position < source.size()) {
        const std::size_t lineEnd = source.find('\n', position);
        const std::string_view line = trimLeft(
            source.substr(position,
                          lineEnd == std::string_view::npos ? lineEnd : lineEnd - position));
        position = lineEnd == std::string_view::npos ? source.size() : lineEnd + 1;

        if (!line.starts_with("#include"))
            continue;
        const std::string_view rest = trimLeft(line.substr(8)); // "#include".size() == 8
        if (rest.empty())
            continue;
        if (rest.front() == '"') {
            const std::size_t close = rest.find('"', 1);
            if (close == std::string_view::npos)
                continue;
            const std::string_view name = rest.substr(1, close - 1);
            if (!name.empty())
                enqueue(path.parent().joined(name));
        } else if (rest.front() == '<') {
            const std::size_t close = rest.find('>', 1);
            const std::string_view name = close == std::string_view::npos
                                              ? rest.substr(1)
                                              : rest.substr(1, close - 1);
            Log::warn(kCategory, "Skipping search-path include <%s> in %s (not packaged)",
                      std::string{name}.c_str(), path.string().c_str());
        }
    }
}

} // namespace

std::vector<AssetPackageEntry>
collectExportClosure(std::span<const VirtualPath> roots, std::string& error) {
    std::vector<AssetPackageEntry> entries;
    if (roots.empty()) {
        error = "Package export requires at least one root path";
        return entries;
    }

    // BFS：visited 在入队时标记（防重复入队），出队时展开。
    std::unordered_map<std::string, AssetPackageEntry> collected;
    std::unordered_set<std::string> queued;
    std::deque<VirtualPath> queue;
    for (const VirtualPath& root : roots) {
        if (!root.valid() || !isAssetScheme(root.scheme())) {
            error = "Package root must be an assets:// path: " + root.string();
            return {};
        }
        if (queued.insert(root.string()).second)
            queue.push_back(root);
    }
    const auto enqueue = [&](const VirtualPath& next) {
        if (queued.insert(next.string()).second)
            queue.push_back(next);
    };

    while (!queue.empty()) {
        const VirtualPath path = std::move(queue.front());
        queue.pop_front();

        if (!FILE_SYSTEM.exists(path)) {
            error = "Package closure references missing file: " + path.string();
            return {};
        }

        if (const auto record = ASSET_DATABASE.findByPath(path)) {
            // 资产节点：源 + meta 进包，数据库依赖图（Scene→Mesh/Material→
            // Shader/Texture）全部入队。
            collected.insert_or_assign(path.string(),
                                       AssetPackageEntry{path, record->id, record->type});

            for (const VirtualPath& dependency : record->dependencies)
                enqueue(dependency);

            if (record->type == AssetType::Shader) {
                // Shader 的 vert/frag 伴随文件不在数据库依赖图（其 importer 的
                // gatherDependencies 为空）：解析源 JSON 的 program 声明收集，路径
                // 规则与导入侧一致（相对 shader 所在目录）。
                const auto source = FILE_SYSTEM.readText(path);
                if (!source) {
                    error = "Cannot read Shader source: " + path.string();
                    return {};
                }
                const auto shader = format::parseShaderAsset(path, *source);
                if (!shader) {
                    error = "Cannot parse Shader source: " + path.string();
                    return {};
                }
                for (const SubShaderDesc& subShader : shader->subShaders) {
                    for (const ShaderPassAsset& passAsset : subShader.passes) {
                        enqueue(passAsset.pass.program.vertexSource);
                        enqueue(passAsset.pass.program.fragmentSource);
                    }
                }
            }
        } else {
            // 伴随文件节点（vert/frag/glsl 等纯文件：无 record、无 meta）。
            collected.insert_or_assign(path.string(), AssetPackageEntry{path});

            if (isShaderSourceCompanion(path)) {
                const auto source = FILE_SYSTEM.readText(path);
                if (!source) {
                    error = "Cannot read shader companion source: " + path.string();
                    return {};
                }
                collectLocalIncludes(path, *source, queue, queued);
            }
        }
    }

    entries.reserve(collected.size());
    for (auto& [key, entry] : collected)
        entries.push_back(std::move(entry));
    // 按 path 排序：相同闭包产出相同包字节（manifest 与 zip 条目顺序均确定）。
    std::sort(entries.begin(), entries.end(),
              [](const AssetPackageEntry& a, const AssetPackageEntry& b) {
                  return a.path.string() < b.path.string();
              });
    return entries;
}

bool exportAssetPackageToBuffer(std::span<const VirtualPath> roots,
                                std::vector<std::byte>& output,
                                std::string& error) {
    output.clear();
    const std::vector<AssetPackageEntry> entries = collectExportClosure(roots, error);
    if (!error.empty())
        return false;

    ZipWriter writer;
    OrderedJson jsonEntries = OrderedJson::array();
    for (const AssetPackageEntry& entry : entries) {
        if (!isSafeRelativePath(entry.path.relativePath())) {
            error = "Unsafe package entry path: " + entry.path.string();
            return false;
        }
        const auto sourceBytes = FILE_SYSTEM.readBinary(entry.path);
        if (!sourceBytes) {
            error = "Cannot read package source: " + entry.path.string();
            return false;
        }
        if (!writer.addEntry(packageEntryName(entry.path), *sourceBytes)) {
            error = "Cannot add package entry: " + entry.path.string();
            return false;
        }

        OrderedJson jsonEntry;
        jsonEntry["path"] = entry.path.relativePath();
        if (entry.isAsset()) {
            // 资产条目必有 meta 侧车：源与 meta 相邻写入。
            const VirtualPath metaPath = assetMetaPath(entry.path);
            const auto metaBytes = FILE_SYSTEM.readBinary(metaPath);
            if (!metaBytes) {
                error = "Cannot read package meta: " + metaPath.string();
                return false;
            }
            if (!writer.addEntry(packageEntryName(metaPath), *metaBytes)) {
                error = "Cannot add package meta entry: " + metaPath.string();
                return false;
            }
            jsonEntry["guid"] = entry.guid->toString();
            jsonEntry["type"] = assetTypeName(entry.type);
        }
        jsonEntries.push_back(std::move(jsonEntry));
    }

    OrderedJson manifest;
    manifest["$schemaVersion"] = kManifestSchemaVersion;
    manifest["entries"] = std::move(jsonEntries);
    if (!writer.addEntry(kManifestEntryName, toBytes(manifest.dump(2) + "\n"))) {
        error = "Cannot add package manifest";
        return false;
    }
    if (!writer.finalize(output)) {
        error = "Cannot finalize package archive";
        return false;
    }
    return true;
}

bool exportAssetPackage(std::span<const VirtualPath> roots,
                        const VirtualPath& packagePath,
                        std::string& error) {
    std::vector<std::byte> bytes;
    if (!exportAssetPackageToBuffer(roots, bytes, error))
        return false;
    if (!FILE_SYSTEM.writeBinaryAtomic(packagePath, bytes)) {
        error = "Cannot write package file: " + packagePath.string();
        return false;
    }
    return true;
}

bool importAssetPackage(const VirtualPath& packagePath,
                        AssetPackageConflictPolicy policy,
                        AssetPackageImportReport& report,
                        std::string& error) {
    report = {};

    // 读包并解析 zip。
    const auto packageBytes = FILE_SYSTEM.readBinary(packagePath);
    if (!packageBytes) {
        error = "Cannot read package file: " + packagePath.string();
        return false;
    }
    const auto reader = ZipReader::open(*packageBytes);
    if (!reader) {
        error = "Package is not a valid archive: " + packagePath.string();
        return false;
    }

    // manifest 解析与校验。
    const auto manifestBytes = reader->extract(kManifestEntryName);
    if (!manifestBytes) {
        error = "Package is missing manifest.json";
        return false;
    }
    const Json manifest = Json::parse(toString(*manifestBytes), nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        error = "Package manifest is not a valid JSON object";
        return false;
    }
    const auto schemaVersion = manifest.find("$schemaVersion");
    if (schemaVersion == manifest.end() || !schemaVersion->is_number_unsigned() ||
        schemaVersion->get<std::uint64_t>() != kManifestSchemaVersion) {
        error = "Unsupported package schema version";
        return false;
    }
    const auto jsonEntries = manifest.find("entries");
    if (jsonEntries == manifest.end() || !jsonEntries->is_array() || jsonEntries->empty()) {
        error = "Package manifest has no entries";
        return false;
    }

    std::vector<AssetPackageEntry> entries;
    std::vector<std::string> expectedArchiveEntries;
    std::unordered_set<std::string> manifestPaths;
    for (const Json& jsonEntry : *jsonEntries) {
        if (!jsonEntry.is_object()) {
            error = "Package manifest entry is not an object";
            return false;
        }
        const auto jsonPath = jsonEntry.find("path");
        if (jsonPath == jsonEntry.end() || !jsonPath->is_string()) {
            error = "Package manifest entry has no path";
            return false;
        }
        const std::string relative = jsonPath->get<std::string>();
        if (!isSafeRelativePath(relative)) {
            error = "Unsafe package entry path: " + relative;
            return false;
        }
        if (!manifestPaths.insert(relative).second) {
            error = "Duplicate package entry path: " + relative;
            return false;
        }

        AssetPackageEntry entry;
        entry.path = VirtualPath{"assets://" + relative};
        const auto jsonGuid = jsonEntry.find("guid");
        if (jsonGuid != jsonEntry.end()) {
            // 资产条目：guid 必须存在且可解析。GUID 现在由 .meta 随机生成并持久化，
            // 不再与包内路径绑定，因此只要 GUID 合法即可（允许包内重命名）。
            if (!jsonGuid->is_string()) {
                error = "Invalid package guid for: " + relative;
                return false;
            }
            const auto guid = AssetId::parse(jsonGuid->get<std::string>());
            if (!guid) {
                error = "Package guid is not a valid AssetId: " + relative;
                return false;
            }
            const auto jsonType = jsonEntry.find("type");
            if (jsonType == jsonEntry.end() || !jsonType->is_string()) {
                error = "Invalid package entry type for: " + relative;
                return false;
            }
            entry.type = assetTypeFromName(jsonType->get<std::string>());
            if (entry.type == AssetType::Unknown) {
                error = "Unknown package entry type for: " + relative;
                return false;
            }
            entry.guid = *guid;
        }
        entries.push_back(entry);
        expectedArchiveEntries.push_back(packageEntryName(entry.path));
        if (entry.isAsset())
            expectedArchiveEntries.push_back(packageEntryName(assetMetaPath(entry.path)));
    }

    // zip 条目与 manifest 一一对应：多余/缺失均拒绝。
    expectedArchiveEntries.emplace_back(kManifestEntryName);
    std::sort(expectedArchiveEntries.begin(), expectedArchiveEntries.end());
    std::vector<std::string> actualEntries = reader->entryNames();
    std::sort(actualEntries.begin(), actualEntries.end());
    if (actualEntries != expectedArchiveEntries) {
        error = "Package archive entries do not match the manifest";
        return false;
    }

    // 冲突预检：写任何文件之前全部完成。Abort 策略下任一冲突即整体失败零落盘。
    std::unordered_set<std::string> skipSet;
    for (const AssetPackageEntry& entry : entries) {
        if (!FILE_SYSTEM.exists(entry.path))
            continue;
        if (policy == AssetPackageConflictPolicy::Abort) {
            error = "Package entry conflicts with existing asset (use Overwrite/Skip): " +
                    entry.path.string();
            return false;
        }
        if (policy == AssetPackageConflictPolicy::Skip)
            skipSet.insert(entry.path.string());
    }

    // 落盘：源（+ 资产 meta）逐条目镜像。
    for (const AssetPackageEntry& entry : entries) {
        if (skipSet.count(entry.path.string()) != 0)
            continue;
        const auto sourceBytes = reader->extract(packageEntryName(entry.path));
        if (!sourceBytes) {
            error = "Corrupt package entry: " + entry.path.string();
            return false;
        }
        if (!FILE_SYSTEM.createDirectories(entry.path.parent()) ||
            !FILE_SYSTEM.writeBinaryAtomic(entry.path, *sourceBytes)) {
            error = "Cannot write package source: " + entry.path.string();
            return false;
        }
        if (entry.isAsset()) {
            const VirtualPath metaPath = assetMetaPath(entry.path);
            const auto metaBytes = reader->extract(packageEntryName(metaPath));
            if (!metaBytes) {
                error = "Corrupt package meta entry: " + metaPath.string();
                return false;
            }
            if (!FILE_SYSTEM.writeBinaryAtomic(metaPath, *metaBytes)) {
                error = "Cannot write package meta: " + metaPath.string();
                return false;
            }
        }
    }

    // 重建：按 manifest 顺序对每个资产条目触发导入（幂等，自身递归处理依赖）；
    // 失败记入 report 不中断其余。导入前清除可能存在的旧路径记录，避免本地随机
    // GUID 与包内 .meta 的 GUID 冲突。
    for (const AssetPackageEntry& entry : entries) {
        if (!entry.isAsset())
            continue;
        if (skipSet.count(entry.path.string()) != 0) {
            report.skipped.push_back(entry.path);
            continue;
        }
        (void)ASSET_DATABASE.remove(entry.path);
        if (!ASSET_IMPORT_PIPELINE.importAsset(entry.path)) {
            report.failed.push_back(entry.path);
            Log::error(kCategory, "Failed to import package asset: %s",
                       entry.path.string().c_str());
            continue;
        }
        report.imported.push_back(entry.path);
    }
    return true;
}

} // namespace engine

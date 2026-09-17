#include "tools/editor/ProjectBrowserModel.h"

#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <utility>

namespace engine::editor {
namespace {

constexpr std::size_t kHistoryLimit = 64;

const VirtualPath& rootDirectory() {
    static const VirtualPath root{"assets://"};
    return root;
}

[[nodiscard]] bool isMetaName(const std::string& name) {
    return name.size() > 5 && name.ends_with(".meta");
}

[[nodiscard]] std::string toLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

[[nodiscard]] bool containsInsensitive(const std::string& text, const std::string& needle) {
    return toLower(text).find(toLower(needle)) != std::string::npos;
}

// Splits "name.scene.json" into {"name", ".scene.json"} using the known asset
// double suffixes (the same recognition assetTypeOf/inferAssetType use, since
// std::filesystem only yields the final ".json"). Plain names keep their final
// extension; directories have no suffix. "name 1" increments reuse the stem.
[[nodiscard]] std::pair<std::string, std::string>
splitNameSuffix(const std::string& name, bool directory) {
    if (directory)
        return {name, {}};
    static constexpr std::string_view kAssetSuffixes[] = {
        ".scene.json", ".material.json", ".mesh.json", ".shader.json"};
    for (const std::string_view suffix : kAssetSuffixes) {
        if (name.ends_with(suffix) && name.size() > suffix.size())
            return {name.substr(0, name.size() - suffix.size()), std::string{suffix}};
    }
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return {name, {}};
    return {name.substr(0, dot), name.substr(dot)};
}

[[nodiscard]] bool validEntryName(const std::string& name) {
    if (name.empty() || name == "." || name == "..")
        return false;
    // Leading dot hides the entry; trailing dot/space is rejected by Windows.
    if (name.front() == '.' || name.back() == '.' || name.back() == ' ')
        return false;
    return std::ranges::all_of(name, [](char character) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::iscntrl(value) != 0)
            return false;
        switch (character) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            return true;
        }
    });
}

[[nodiscard]] bool entryOrder(const ProjectEntry& left, const ProjectEntry& right) {
    if (left.directory != right.directory)
        return left.directory;
    return left.name < right.name;
}

[[nodiscard]] const char* assetSuffixFor(AssetType type) {
    switch (type) {
    case AssetType::Scene: return ".scene.json";
    case AssetType::Material: return ".material.json";
    case AssetType::Shader: return ".shader.json";
    default: return nullptr;
    }
}

// listFiles reports files only, so directories are derived from ancestor chains
// of the recursive file listing; empty directories stay invisible until the
// session learns about them through createFolder/movePath/duplicate
// (knownDirectories_).
void collectDirectoryPaths(const std::vector<VirtualPath>& files, std::set<std::string>& paths) {
    for (const VirtualPath& file : files) {
        VirtualPath directory = file.parent();
        while (directory.valid() && directory != rootDirectory()) {
            paths.insert(directory.string());
            directory = directory.parent();
        }
    }
}

// First imported Shader by path: the default shader reference for new materials.
// Deterministic pick (lexicographically smallest sourcePath) keeps tests stable.
[[nodiscard]] std::optional<VirtualPath> firstShaderPath() {
    std::optional<VirtualPath> best;
    for (const AssetRecord& record : ASSET_DATABASE.records()) {
        if (record.type != AssetType::Shader || record.status != AssetImportStatus::Imported)
            continue;
        if (!best || record.sourcePath.string() < best->string())
            best = record.sourcePath;
    }
    return best;
}

// Minimal valid templates distilled from the demo/builtin assets. Names are
// validated (no quotes/backslashes/control characters) before interpolation.
[[nodiscard]] std::string sceneTemplate(const std::string& name) {
    return "{\"$schemaVersion\":1,\"name\":\"" + name + "\",\"nodes\":[]}";
}

[[nodiscard]] std::string materialTemplate(const std::string& name, const AssetId& shaderGuid) {
    return "{\"$schemaVersion\":1,\"name\":\"" + name + "\",\"shader\":\"guid://" +
           shaderGuid.toString() + "\",\"properties\":{},\"keywords\":[]}";
}

[[nodiscard]] std::string shaderTemplate(const std::string& name) {
    return "{\"$schemaVersion\":1,\"name\":\"" + name + "\",\"properties\":[],\"subShaders\":[{"
           "\"tags\":{\"renderPipeline\":\"MiniForward\",\"queue\":\"Opaque\"},"
           "\"passes\":[{"
           "\"name\":\"Forward\","
           "\"state\":{\"cull\":\"Off\",\"frontFace\":\"CW\",\"fill\":\"Solid\","
           "\"topology\":\"TriangleList\",\"depthWrite\":true,\"depthTest\":\"LessEqual\","
           "\"blend\":\"Off\",\"colorMask\":\"RGBA\"},"
           "\"program\":{\"vertex\":\"" + name + ".Forward.vert\",\"frag\":\"" + name +
           ".Forward.frag\"},"
           "\"vertexInput\":[{\"name\":\"position\",\"semantic\":\"POSITION\",\"type\":\"Vec3\","
           "\"location\":0}],"
           "\"varyings\":[],"
           "\"fragmentOutputs\":[{\"name\":\"color\",\"type\":\"Vec4\",\"location\":0}],"
           "\"features\":[]}]}]}";
}

// "name.ext" -> "name 1.ext" -> "name 2.ext"; also probes the .meta sibling so a
// stale orphan sidecar cannot capture the new file's identity.
[[nodiscard]] VirtualPath uniqueTarget(const VirtualPath& parent,
                                       const std::string& stem,
                                       const std::string& suffix) {
    for (std::size_t attempt = 0; attempt < 1000; ++attempt) {
        const std::string name =
            attempt == 0 ? stem + suffix
                         : stem + " " + std::to_string(attempt) + suffix;
        const VirtualPath candidate = parent.joined(name);
        if (!FILE_SYSTEM.exists(candidate) && !FILE_SYSTEM.exists(assetMetaPath(candidate)))
            return candidate;
    }
    return {};
}

} // namespace

void ProjectBrowserModel::navigate(const VirtualPath& directory) {
    if (!directory.valid() || directory.scheme() != rootDirectory().scheme() ||
        !FILE_SYSTEM.isDirectory(directory) || directory == current_)
        return;
    if (history_.empty())
        history_.push_back(current_);
    history_.resize(historyIndex_ + 1);
    history_.push_back(directory);
    ++historyIndex_;
    while (history_.size() > kHistoryLimit) {
        history_.erase(history_.begin());
        --historyIndex_;
    }
    current_ = directory;
    snapshotsDirty_ = true;
}

bool ProjectBrowserModel::back() {
    if (historyIndex_ == 0)
        return false;
    --historyIndex_;
    current_ = history_[historyIndex_];
    snapshotsDirty_ = true;
    return true;
}

bool ProjectBrowserModel::forward() {
    if (history_.empty() || historyIndex_ + 1 >= history_.size())
        return false;
    ++historyIndex_;
    current_ = history_[historyIndex_];
    snapshotsDirty_ = true;
    return true;
}

bool ProjectBrowserModel::navigateUp() {
    const VirtualPath parent = current_.parent();
    if (!parent.valid() || parent == current_ || parent.scheme() != rootDirectory().scheme())
        return false;
    navigate(parent);
    return true;
}

const VirtualPath& ProjectBrowserModel::currentDirectory() const {
    return current_;
}

bool ProjectBrowserModel::canBack() const {
    return historyIndex_ > 0 && !history_.empty();
}

bool ProjectBrowserModel::canForward() const {
    return !history_.empty() && historyIndex_ + 1 < history_.size();
}

std::vector<VirtualPath> ProjectBrowserModel::breadcrumbs() const {
    std::vector<VirtualPath> chain;
    VirtualPath directory = current_;
    while (directory.valid() && directory != rootDirectory()) {
        chain.push_back(directory);
        directory = directory.parent();
    }
    if (!directory.valid())
        return {rootDirectory()};
    std::ranges::reverse(chain);
    chain.insert(chain.begin(), rootDirectory());
    return chain;
}

void ProjectBrowserModel::setSearchText(std::string text) {
    if (searchText_ == text)
        return;
    searchText_ = std::move(text);
    snapshotsDirty_ = true;
}

const std::string& ProjectBrowserModel::searchText() const {
    return searchText_;
}

void ProjectBrowserModel::setTypeFilter(AssetType type) {
    if (typeFilter_ == type)
        return;
    typeFilter_ = type;
    snapshotsDirty_ = true;
}

AssetType ProjectBrowserModel::typeFilter() const {
    return typeFilter_;
}

void ProjectBrowserModel::setViewMode(ViewMode mode) {
    viewMode_ = mode;
}

ProjectBrowserModel::ViewMode ProjectBrowserModel::viewMode() const {
    return viewMode_;
}

void ProjectBrowserModel::selectEntry(const VirtualPath& path) {
    selected_ = path;
}

void ProjectBrowserModel::clearSelection() {
    selected_.reset();
}

const VirtualPath* ProjectBrowserModel::selectedEntry() const {
    return selected_ ? &*selected_ : nullptr;
}

void ProjectBrowserModel::rebuildSnapshots() const {
    directoryTree_.clear();
    content_.clear();

    if (!FILE_SYSTEM.isMounted(rootDirectory().scheme()))
        return;

    const std::vector<VirtualPath> allFiles = FILE_SYSTEM.listFiles(rootDirectory(), true);
    std::set<std::string> directoryPaths;
    collectDirectoryPaths(allFiles, directoryPaths);
    // 会话已知目录（仍存在的）并入；失效项顺带清理。
    for (auto known = knownDirectories_.begin(); known != knownDirectories_.end();) {
        if (FILE_SYSTEM.isDirectory(VirtualPath{*known})) {
            directoryPaths.insert(*known);
            ++known;
        } else {
            known = knownDirectories_.erase(known);
        }
    }
    directoryTree_.clear();
    directoryTree_.reserve(directoryPaths.size());
    for (const std::string& path : directoryPaths)
        directoryTree_.push_back({VirtualPath{path}, VirtualPath{path}.filename(), true});

    const auto filePassesFilter = [this](const VirtualPath& file) {
        return typeFilter_ == AssetType::Unknown || inferAssetType(file) == typeFilter_;
    };

    if (isSearchActive()) {
        // 搜索模式：跨目录递归匹配。文件按名字+类型，目录只按名字。
        for (const VirtualPath& file : allFiles) {
            if (!isMetaName(file.filename()) && containsInsensitive(file.filename(), searchText_) &&
                filePassesFilter(file))
                content_.push_back({file, file.filename(), false});
        }
        for (const ProjectEntry& directory : directoryTree_) {
            if (containsInsensitive(directory.name, searchText_))
                content_.push_back(directory);
        }
    } else {
        // 浏览模式：当前目录的直接子项（文件按 parent 判定，目录来自全量目录树）。
        for (const VirtualPath& file : allFiles) {
            if (!isMetaName(file.filename()) && file.parent() == current_ &&
                filePassesFilter(file))
                content_.push_back({file, file.filename(), false});
        }
        for (const ProjectEntry& directory : directoryTree_) {
            if (directory.path.parent() == current_)
                content_.push_back(directory);
        }
    }
    std::ranges::sort(content_, entryOrder);

    if (selected_ && !FILE_SYSTEM.exists(*selected_))
        selected_.reset();
    // Clear only on the built path: an unmounted assets:// stays dirty so the
    // next frame retries once a project (mount) appears.
    snapshotsDirty_ = false;
}

std::vector<ProjectEntry> ProjectBrowserModel::directoryTreeEntries() const {
    if (snapshotsDirty_)
        rebuildSnapshots();
    return directoryTree_;
}

std::vector<ProjectEntry> ProjectBrowserModel::contentEntries() const {
    if (snapshotsDirty_)
        rebuildSnapshots();
    return content_;
}

void ProjectBrowserModel::invalidate() {
    snapshotsDirty_ = true;
}

const std::string& ProjectBrowserModel::lastError() const {
    return lastError_;
}

void ProjectBrowserModel::fail(std::string error) {
    lastError_ = std::move(error);
}

bool ProjectBrowserModel::createFolder(const VirtualPath& parent, const std::string& name) {
    lastError_.clear();
    if (!validEntryName(name) || isMetaName(name)) {
        fail("Invalid folder name: " + name);
        return false;
    }
    const VirtualPath target = parent.joined(name);
    if (FILE_SYSTEM.exists(target)) {
        fail("Already exists: " + target.string());
        return false;
    }
    if (!FILE_SYSTEM.createDirectories(target)) {
        fail("Cannot create folder: " + target.string());
        return false;
    }
    knownDirectories_.insert(target.string());
    invalidate();
    return true;
}

bool ProjectBrowserModel::createAsset(const VirtualPath& parent,
                                      AssetType type,
                                      const std::string& name) {
    lastError_.clear();
    const char* suffix = assetSuffixFor(type);
    if (!suffix) {
        fail("Unsupported asset type");
        return false;
    }
    std::string fileName = name;
    if (!fileName.ends_with(suffix))
        fileName += suffix;
    if (!validEntryName(fileName) || isMetaName(fileName)) {
        fail("Invalid asset name: " + name);
        return false;
    }
    const VirtualPath target = parent.joined(fileName);
    if (FILE_SYSTEM.exists(target)) {
        fail("Already exists: " + target.string());
        return false;
    }

    const auto [stem, stemSuffix] = splitNameSuffix(fileName, false);
    (void)stemSuffix;
    std::string content;
    switch (type) {
    case AssetType::Scene:
        content = sceneTemplate(stem);
        break;
    case AssetType::Material: {
        const auto shaderPath = firstShaderPath();
        if (!shaderPath) {
            fail("Creating a Material requires an imported Shader");
            return false;
        }
        const auto shaderGuid = ASSET_DATABASE.assetIdFromPath(*shaderPath);
        if (!shaderGuid) {
            fail("Cannot resolve the default Shader GUID");
            return false;
        }
        content = materialTemplate(stem, *shaderGuid);
        break;
    }
    case AssetType::Shader:
        content = shaderTemplate(stem);
        break;
    default:
        fail("Unsupported asset type");
        return false;
    }

    if (!FILE_SYSTEM.writeTextAtomic(target, content)) {
        fail("Cannot write asset: " + target.string());
        return false;
    }
    // The watcher would import on its next poll; importing now makes the .meta,
    // record and status visible without the delay.
    if (ASSET_IMPORT_PIPELINE.initialized())
        (void)ASSET_IMPORT_PIPELINE.importAsset(target);
    invalidate();
    return true;
}

bool ProjectBrowserModel::renameEntry(const VirtualPath& path, const std::string& newName) {
    lastError_.clear();
    const bool directory = FILE_SYSTEM.isDirectory(path);
    std::string fileName = newName;
    // A suffix-less rename keeps the recognized suffix (including double
    // suffixes like .scene.json): renaming the title does not change the type.
    if (!directory && fileName.find('.') == std::string::npos) {
        const auto [stem, suffix] = splitNameSuffix(path.filename(), false);
        if (!suffix.empty())
            fileName += suffix;
    }
    if (!validEntryName(fileName) || isMetaName(fileName)) {
        fail("Invalid name: " + newName);
        return false;
    }
    const VirtualPath target = path.parent().joined(fileName);
    if (target == path)
        return true;
    return movePath(path, target);
}

bool ProjectBrowserModel::moveEntry(const VirtualPath& source, const VirtualPath& targetDir) {
    lastError_.clear();
    if (!FILE_SYSTEM.isDirectory(targetDir)) {
        fail("Not a folder: " + targetDir.string());
        return false;
    }
    if (source.parent() == targetDir)
        return true;
    // Dropping a directory into itself or one of its descendants would cycle.
    if (source == targetDir || targetDir.string().starts_with(source.string() + "/")) {
        fail("Cannot move a folder into itself: " + targetDir.string());
        return false;
    }
    return movePath(source, targetDir.joined(source.filename()));
}

bool ProjectBrowserModel::movePath(const VirtualPath& from, const VirtualPath& to) {
    if (FILE_SYSTEM.exists(to)) {
        fail("Already exists: " + to.string());
        return false;
    }
    const bool pipelineReady = ASSET_IMPORT_PIPELINE.initialized();

    if (FILE_SYSTEM.isDirectory(from)) {
        // 目录：先物理搬移整棵子树，再把数据库记录逐个指到新路径。.meta 已随
        // 目录一起移动，moveAsset 走"信任新路径 Meta"分支，保持 GUID 并重导入。
        if (!FILE_SYSTEM.move(from, to)) {
            fail("Cannot move: " + from.string());
            return false;
        }
        knownDirectories_.insert(to.string());
        if (pipelineReady) {
            const std::string prefix = from.string() + "/";
            for (const AssetRecord& record : ASSET_DATABASE.records()) {
                if (!record.sourcePath.string().starts_with(prefix))
                    continue;
                const VirtualPath newChild{
                    to.string() + record.sourcePath.string().substr(from.string().size())};
                if (!ASSET_IMPORT_PIPELINE.moveAsset(record.sourcePath, newChild)) {
                    // 单条失败不回滚整棵子树：记录错误，继续处理其余记录。
                    fail("Cannot update asset: " + record.sourcePath.string());
                }
            }
        }
        invalidate();
        return lastError_.empty();
    }

    if (!FILE_SYSTEM.isFile(from)) {
        fail("Not found: " + from.string());
        return false;
    }
    // 文件：管线完成源+.meta 搬移、数据库原路径映射更新与重导入；未初始化时
    // （无项目）退化为纯文件移动。
    if (pipelineReady) {
        if (!ASSET_IMPORT_PIPELINE.moveAsset(from, to)) {
            fail("Cannot move: " + from.string());
            return false;
        }
    } else if (!FILE_SYSTEM.move(from, to)) {
        fail("Cannot move: " + from.string());
        return false;
    }
    invalidate();
    return true;
}

bool ProjectBrowserModel::removeEntry(const VirtualPath& path) {
    lastError_.clear();
    if (!FILE_SYSTEM.exists(path)) {
        fail("Not found: " + path.string());
        return false;
    }
    const bool pipelineReady = ASSET_IMPORT_PIPELINE.initialized();

    if (FILE_SYSTEM.isDirectory(path)) {
        if (pipelineReady) {
            const std::string prefix = path.string() + "/";
            for (const AssetRecord& record : ASSET_DATABASE.records()) {
                if (record.sourcePath.string().starts_with(prefix))
                    (void)ASSET_IMPORT_PIPELINE.removeAsset(record.sourcePath);
            }
        }
        if (!FILE_SYSTEM.removeDirectory(path)) {
            fail("Cannot delete: " + path.string());
            return false;
        }
    } else {
        if (pipelineReady)
            (void)ASSET_IMPORT_PIPELINE.removeAsset(path);
        // Meta 先删：删除源文件的 watcher 事件到来时记录已经不在了。未导入
        // 的文件没有 .meta，跳过以避免误导性的 "cannot remove" 日志。
        if (FILE_SYSTEM.isFile(assetMetaPath(path)))
            (void)FILE_SYSTEM.removeFile(assetMetaPath(path));
        if (!FILE_SYSTEM.removeFile(path)) {
            fail("Cannot delete: " + path.string());
            return false;
        }
    }
    invalidate();
    return true;
}

bool ProjectBrowserModel::duplicateEntry(const VirtualPath& path) {
    lastError_.clear();
    if (!FILE_SYSTEM.exists(path)) {
        fail("Not found: " + path.string());
        return false;
    }
    const bool directory = FILE_SYSTEM.isDirectory(path);
    const auto [stem, suffix] = splitNameSuffix(path.filename(), directory);
    const VirtualPath target = uniqueTarget(path.parent(), stem, suffix);
    if (!target.valid()) {
        fail("No free name for the duplicate of: " + path.string());
        return false;
    }
    if (!FILE_SYSTEM.copy(path, target)) {
        fail("Cannot duplicate: " + path.string());
        return false;
    }
    if (directory)
        knownDirectories_.insert(target.string());

    // 副本不带旧 GUID：目录副本里随树复制的 .meta 全部删除；单文件副本本来就没
    // 有复制 .meta。随后的立即导入为副本生成全新 GUID（与 watcher 语义一致，
    // 但没有轮询延迟）。
    const auto importKnownSources = [](const VirtualPath& root) {
        if (!ASSET_IMPORT_PIPELINE.initialized())
            return;
        for (const VirtualPath& file : FILE_SYSTEM.listFiles(root, true)) {
            if (!isMetaName(file.filename()) && inferAssetType(file) != AssetType::Unknown)
                (void)ASSET_IMPORT_PIPELINE.importAsset(file);
        }
    };
    if (directory) {
        for (const VirtualPath& file : FILE_SYSTEM.listFiles(target, true)) {
            if (isMetaName(file.filename()))
                (void)FILE_SYSTEM.removeFile(file);
        }
        importKnownSources(target);
    } else if (ASSET_IMPORT_PIPELINE.initialized() &&
               inferAssetType(target) != AssetType::Unknown) {
        (void)ASSET_IMPORT_PIPELINE.importAsset(target);
    }
    invalidate();
    return true;
}

bool ProjectBrowserModel::reimportEntry(const VirtualPath& path) {
    lastError_.clear();
    if (!FILE_SYSTEM.isFile(path)) {
        fail("Not a file: " + path.string());
        return false;
    }
    if (!ASSET_IMPORT_PIPELINE.initialized()) {
        fail("Pipeline is not initialized");
        return false;
    }
    if (!ASSET_IMPORT_PIPELINE.reimportAsset(path)) {
        fail("Reimport failed: " + path.string());
        return false;
    }
    invalidate();
    return true;
}

std::vector<VirtualPath> ProjectBrowserModel::dependentsOf(const VirtualPath& path) const {
    return ASSET_DATABASE.dependentsOf(path);
}

} // namespace engine::editor

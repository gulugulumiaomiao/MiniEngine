#include "asset/AssetDatabase.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace engine {
namespace {

using Json = nlohmann::json;

[[nodiscard]] const char* importStatusName(AssetImportStatus status) {
    switch (status) {
    case AssetImportStatus::Imported:
        return "Imported";
    case AssetImportStatus::Failed:
        return "Failed";
    case AssetImportStatus::Missing:
        return "Missing";
    default:
        return "NotImported";
    }
}

[[nodiscard]] AssetImportStatus importStatusFromName(std::string_view name) {
    if (name == "Imported") return AssetImportStatus::Imported;
    if (name == "Failed") return AssetImportStatus::Failed;
    if (name == "Missing") return AssetImportStatus::Missing;
    return AssetImportStatus::NotImported;
}

[[nodiscard]] bool readUnsigned(const Json& object, const char* key,
                                std::uint64_t& value) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_unsigned()) {
        return false;
    }
    value = found->get<std::uint64_t>();
    return true;
}

[[nodiscard]] std::optional<AssetRecord> parseRecord(const Json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto idValue = value.find("asset_id");
    const auto typeValue = value.find("asset_type");
    const auto sourceValue = value.find("source_path");
    const auto metaValue = value.find("meta_path");
    const auto artifactValue = value.find("artifact_path");
    if (idValue == value.end() || !idValue->is_string() ||
        typeValue == value.end() || !typeValue->is_string() ||
        sourceValue == value.end() || !sourceValue->is_string() ||
        metaValue == value.end() || !metaValue->is_string() ||
        artifactValue == value.end() || !artifactValue->is_string()) {
        return std::nullopt;
    }

    const auto id = AssetId::parse(idValue->get_ref<const std::string&>());
    AssetRecord record;
    record.id = id.value_or(AssetId{});
    record.type = assetTypeFromName(typeValue->get_ref<const std::string&>());
    record.sourcePath = VirtualPath{sourceValue->get_ref<const std::string&>()};
    record.metaPath = VirtualPath{metaValue->get_ref<const std::string&>()};
    record.artifactPath =
        VirtualPath{artifactValue->get_ref<const std::string&>()};
    if (!record.id.valid() || record.type == AssetType::Unknown ||
        !record.sourcePath.valid() || !record.metaPath.valid() ||
        !record.artifactPath.valid()) {
        return std::nullopt;
    }

    std::uint64_t importerVersion = 0;
    if (!readUnsigned(value, "importer_version", importerVersion) ||
        importerVersion > UINT32_MAX ||
        !readUnsigned(value, "source_hash", record.sourceHash) ||
        !readUnsigned(value, "meta_hash", record.metaHash) ||
        !readUnsigned(value, "artifact_hash", record.artifactHash)) {
        return std::nullopt;
    }
    record.importerVersion = static_cast<std::uint32_t>(importerVersion);

    const auto status = value.find("status");
    const auto error = value.find("last_error");
    const auto dependencies = value.find("dependencies");
    if (status == value.end() || !status->is_string() ||
        error == value.end() || !error->is_string() ||
        dependencies == value.end() || !dependencies->is_array()) {
        return std::nullopt;
    }
    record.status =
        importStatusFromName(status->get_ref<const std::string&>());
    record.lastError = error->get_ref<const std::string&>();
    for (const Json& dependencyValue : *dependencies) {
        if (!dependencyValue.is_string()) {
            return std::nullopt;
        }
        VirtualPath dependency{dependencyValue.get_ref<const std::string&>()};
        if (!dependency.valid()) {
            return std::nullopt;
        }
        record.dependencies.push_back(std::move(dependency));
    }
    return record;
}

[[nodiscard]] Json serializeRecord(const AssetRecord& record) {
    Json dependencies = Json::array();
    for (const VirtualPath& dependency : record.dependencies) {
        dependencies.push_back(dependency.string());
    }
    return Json{{"asset_id", record.id.toString()},
                {"asset_type", assetTypeName(record.type)},
                {"source_path", record.sourcePath.string()},
                {"meta_path", record.metaPath.string()},
                {"artifact_path", record.artifactPath.string()},
                {"importer_version", record.importerVersion},
                {"source_hash", record.sourceHash},
                {"meta_hash", record.metaHash},
                {"artifact_hash", record.artifactHash},
                {"dependencies", std::move(dependencies)},
                {"status", importStatusName(record.status)},
                {"last_error", record.lastError}};
}

} // namespace

AssetDatabase& AssetDatabase::instance() {
    static AssetDatabase database;
    return database;
}

const VirtualPath& AssetDatabase::databasePath() {
    static const VirtualPath path{"library://AssetDatabase.json"};
    return path;
}

const VirtualPath& AssetDatabase::artifactsRoot() {
    static const VirtualPath path{"library://artifacts"};
    return path;
}

bool AssetDatabase::initialize() {
    if (!FILE_SYSTEM.createDirectories(artifactsRoot())) {
        return false;
    }
    const bool result = FILE_SYSTEM.exists(databasePath()) ? load() : save();
    if (result) {
        std::scoped_lock lock{mutex_};
        initialized_ = true;
    }
    return result;
}

void AssetDatabase::shutdown() {
    bool shouldSave = false;
    {
        std::scoped_lock lock{mutex_};
        shouldSave = initialized_;
        initialized_ = false;
    }
    if (shouldSave) {
        (void)save();
    }
}

std::optional<AssetRecord> AssetDatabase::findById(AssetId id) const {
    std::scoped_lock lock{mutex_};
    const auto found = records_.find(id);
    return found == records_.end() ? std::nullopt
                                   : std::optional<AssetRecord>{found->second};
}

std::optional<AssetRecord>
AssetDatabase::findByPath(const VirtualPath& path) const {
    const auto id = assetIdFromPath(path);
    return id ? findById(*id) : std::nullopt;
}

std::optional<AssetId>
AssetDatabase::assetIdFromPath(const VirtualPath& path) const {
    std::scoped_lock lock{mutex_};
    const auto found = pathIndex_.find(path.string());
    return found == pathIndex_.end() ? std::nullopt
                                     : std::optional<AssetId>{found->second};
}

std::optional<VirtualPath> AssetDatabase::pathFromAssetId(AssetId id) const {
    std::scoped_lock lock{mutex_};
    const auto found = records_.find(id);
    return found == records_.end()
               ? std::nullopt
               : std::optional<VirtualPath>{found->second.sourcePath};
}

std::vector<VirtualPath>
AssetDatabase::dependenciesOf(const VirtualPath& path) const {
    const auto record = findByPath(path);
    return record ? record->dependencies : std::vector<VirtualPath>{};
}

std::vector<VirtualPath>
AssetDatabase::dependentsOf(const VirtualPath& path) const {
    std::scoped_lock lock{mutex_};
    const auto found = reverseDependencies_.find(path.string());
    return found == reverseDependencies_.end()
               ? std::vector<VirtualPath>{}
               : found->second;
}

bool AssetDatabase::addOrUpdate(AssetRecord record) {
    if (!record.id.valid() || record.type == AssetType::Unknown ||
        !record.sourcePath.valid() || !record.metaPath.valid()) {
        Log::error("AssetDatabase", "Cannot add invalid AssetRecord");
        return false;
    }
    if (!record.artifactPath.valid()) {
        record.artifactPath = artifactPath(record.id);
    }

    std::scoped_lock lock{mutex_};
    const auto pathOwner = pathIndex_.find(record.sourcePath.string());
    if (pathOwner != pathIndex_.end() && pathOwner->second != record.id) {
        Log::error("AssetDatabase", "Asset path already has another ID: %s",
                   record.sourcePath.string().c_str());
        return false;
    }
    const auto old = records_.find(record.id);
    if (old != records_.end() &&
        old->second.sourcePath.string() != record.sourcePath.string()) {
        pathIndex_.erase(old->second.sourcePath.string());
    }
    records_[record.id] = std::move(record);
    rebuildIndexesLocked();
    return true;
}

bool AssetDatabase::remove(const VirtualPath& path) {
    std::scoped_lock lock{mutex_};
    const auto indexed = pathIndex_.find(path.string());
    if (indexed == pathIndex_.end()) {
        return false;
    }
    records_.erase(indexed->second);
    rebuildIndexesLocked();
    return true;
}

void AssetDatabase::clear() {
    std::scoped_lock lock{mutex_};
    records_.clear();
    pathIndex_.clear();
    reverseDependencies_.clear();
}

bool AssetDatabase::load() {
    const auto source = FILE_SYSTEM.readText(databasePath());
    if (!source) {
        return false;
    }
    const Json root = Json::parse(*source, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        Log::error("AssetDatabase", "Invalid database JSON: %s",
                   databasePath().string().c_str());
        return false;
    }
    const auto version = root.find("version");
    const auto assets = root.find("assets");
    if (version == root.end() || !version->is_number_unsigned() ||
        version->get<std::uint32_t>() != 1 || assets == root.end() ||
        !assets->is_array()) {
        Log::error("AssetDatabase", "Unsupported database format: %s",
                   databasePath().string().c_str());
        return false;
    }

    std::unordered_map<AssetId, AssetRecord> loaded;
    std::unordered_map<std::string, AssetId> paths;
    for (const Json& value : *assets) {
        auto record = parseRecord(value);
        if (!record || loaded.contains(record->id) ||
            paths.contains(record->sourcePath.string())) {
            Log::error("AssetDatabase", "Invalid or duplicate AssetRecord");
            return false;
        }
        paths.emplace(record->sourcePath.string(), record->id);
        loaded.emplace(record->id, std::move(*record));
    }

    std::scoped_lock lock{mutex_};
    records_ = std::move(loaded);
    rebuildIndexesLocked();
    return true;
}

bool AssetDatabase::save() const {
    std::vector<AssetRecord> records;
    {
        std::scoped_lock lock{mutex_};
        records.reserve(records_.size());
        for (const auto& [id, record] : records_) {
            (void)id;
            records.push_back(record);
        }
    }
    std::ranges::sort(records, {}, [](const AssetRecord& record) {
        return record.sourcePath.string();
    });

    Json assets = Json::array();
    for (const AssetRecord& record : records) {
        assets.push_back(serializeRecord(record));
    }
    const std::string content =
        Json{{"version", 1}, {"assets", std::move(assets)}}.dump(2) + '\n';
    return FILE_SYSTEM.writeTextAtomic(databasePath(), content);
}

bool AssetDatabase::rebuild() {
    clear();
    return save();
}

VirtualPath AssetDatabase::artifactDirectory(AssetId id) const {
    return id.valid() ? artifactsRoot().joined(id.toString()) : VirtualPath{};
}

VirtualPath AssetDatabase::artifactPath(AssetId id) const {
    const VirtualPath directory = artifactDirectory(id);
    return directory.valid() ? directory.joined("asset.json") : VirtualPath{};
}

bool AssetDatabase::prepareArtifactDirectory(AssetId id) const {
    const VirtualPath directory = artifactDirectory(id);
    return directory.valid() && FILE_SYSTEM.createDirectories(directory);
}

void AssetDatabase::rebuildIndexesLocked() {
    pathIndex_.clear();
    reverseDependencies_.clear();
    for (const auto& [id, record] : records_) {
        pathIndex_[record.sourcePath.string()] = id;
        for (const VirtualPath& dependency : record.dependencies) {
            reverseDependencies_[dependency.string()].push_back(
                record.sourcePath);
        }
    }
    for (auto& [path, dependents] : reverseDependencies_) {
        (void)path;
        std::ranges::sort(dependents, {}, &VirtualPath::string);
    }
}

} // namespace engine

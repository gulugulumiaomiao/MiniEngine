#pragma once

#include "asset/Asset.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

enum class AssetImportStatus {
    NotImported,
    Imported,
    Failed,
    Missing,
};

struct AssetRecord {
    AssetId id;
    AssetType type{AssetType::Unknown};
    VirtualPath sourcePath;
    VirtualPath metaPath;
    VirtualPath artifactPath;
    std::uint32_t importerVersion{};
    std::uint64_t sourceHash{};
    std::uint64_t metaHash{};
    std::uint64_t artifactHash{};
    std::vector<VirtualPath> dependencies;
    AssetImportStatus status{AssetImportStatus::NotImported};
    std::string lastError;
};

class AssetDatabase final {
public:
    [[nodiscard]] static AssetDatabase& instance();

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] std::optional<AssetRecord> findById(AssetId id) const;
    [[nodiscard]] std::optional<AssetRecord>
    findByPath(const VirtualPath& path) const;
    [[nodiscard]] std::optional<AssetId>
    assetIdFromPath(const VirtualPath& path) const;
    [[nodiscard]] std::optional<VirtualPath> pathFromAssetId(AssetId id) const;
    [[nodiscard]] std::vector<VirtualPath>
    dependenciesOf(const VirtualPath& path) const;
    [[nodiscard]] std::vector<VirtualPath>
    dependentsOf(const VirtualPath& path) const;

    [[nodiscard]] bool addOrUpdate(AssetRecord record);
    [[nodiscard]] bool remove(const VirtualPath& path);
    void clear();

    [[nodiscard]] bool load();
    [[nodiscard]] bool save() const;
    [[nodiscard]] bool rebuild();

    [[nodiscard]] VirtualPath artifactDirectory(AssetId id) const;
    [[nodiscard]] VirtualPath artifactPath(AssetId id) const;
    [[nodiscard]] bool prepareArtifactDirectory(AssetId id) const;

    [[nodiscard]] static const VirtualPath& databasePath();
    [[nodiscard]] static const VirtualPath& artifactsRoot();

private:
    AssetDatabase() = default;

    void rebuildIndexesLocked();

    mutable std::mutex mutex_;
    std::unordered_map<AssetId, AssetRecord> records_;
    std::unordered_map<std::string, AssetId> pathIndex_;
    std::unordered_map<std::string, std::vector<VirtualPath>>
        reverseDependencies_;
    bool initialized_{};
};

} // namespace engine

#define ASSET_DATABASE (::engine::AssetDatabase::instance())

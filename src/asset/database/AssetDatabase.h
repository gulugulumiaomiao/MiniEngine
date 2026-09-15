#pragma once

#include "asset/base/Asset.h"
#include "asset/base/GuidResolver.h"
#include "core/base/Singleton.h"

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
    // ImportSettings 的 hash 快照；与依赖一一对应的 sourceHash 快照。任一与当前值
    // 不一致都会强制重导入（改 Shader 必须重导 Material 的可靠触发）。
    std::uint64_t settingsHash{};
    std::vector<std::uint64_t> dependencyHashes;
    std::vector<VirtualPath> dependencies;
    AssetImportStatus status{AssetImportStatus::NotImported};
    std::string lastError;
};

class AssetDatabase final : public Singleton<AssetDatabase>, public GuidResolver {
public:
    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] std::optional<AssetRecord> findById(AssetId id) const;
    [[nodiscard]] std::optional<AssetRecord> findByPath(const VirtualPath& path) const;
    [[nodiscard]] std::optional<AssetId> assetIdFromPath(const VirtualPath& path) const;
    [[nodiscard]] std::optional<VirtualPath> pathFromAssetId(AssetId id) const;

    // GuidResolver
    [[nodiscard]] std::optional<VirtualPath> findPath(const AssetId& guid) const override;
    [[nodiscard]] std::optional<AssetId> findGuid(const VirtualPath& path) const override;
    [[nodiscard]] std::vector<VirtualPath> dependenciesOf(const VirtualPath& path) const;
    [[nodiscard]] std::vector<VirtualPath> dependentsOf(const VirtualPath& path) const;
    [[nodiscard]] std::vector<AssetRecord> records() const;

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
    friend class Singleton<AssetDatabase>;
    AssetDatabase() = default;

    void rebuildIndexesLocked();

    mutable std::mutex mutex_;
    std::unordered_map<AssetId, AssetRecord> records_;
    std::unordered_map<std::string, AssetId> pathIndex_;
    bool initialized_{};
};

} // namespace engine

#define ASSET_DATABASE (::engine::AssetDatabase::instance())

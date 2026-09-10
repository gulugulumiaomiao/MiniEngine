#pragma once

#include "asset/base/Asset.h"
#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"

#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace engine {

enum class AssetManagerMode {
    Development,
    Packaged,
};

class AssetManager final : public Singleton<AssetManager> {
public:
    using ChangeListener = std::function<void(const VirtualPath&, AssetType, bool removed)>;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool initialize(AssetManagerMode mode);
    void shutdown();

    [[nodiscard]] std::shared_ptr<Asset> loadAsset(const VirtualPath& path);

    template <std::derived_from<Asset> AssetTypeT>
    [[nodiscard]] std::shared_ptr<AssetTypeT> loadAsset(const VirtualPath& path) {
        return std::dynamic_pointer_cast<AssetTypeT>(loadAsset(path));
    }

    void invalidate(const VirtualPath& path);
    void clear();
    void setChangeListener(ChangeListener listener) { changeListener_ = std::move(listener); }

private:
    friend class Singleton<AssetManager>;
    AssetManager() = default;

    [[nodiscard]] std::shared_ptr<Asset> findCached(const VirtualPath& path) const {
        std::scoped_lock lock{mutex_};
        const auto found = cache_.find(path.string());
        if (found == cache_.end())
            return {};
        return found->second.lock();
    }

    [[nodiscard]] std::shared_ptr<Asset> cache(const VirtualPath& path,
                                               std::shared_ptr<Asset> asset) {
        std::scoped_lock lock{mutex_};
        if (const auto found = cache_.find(path.string()); found != cache_.end()) {
            if (auto existing = found->second.lock())
                return existing;
        }
        cache_.insert_or_assign(path.string(), asset);
        return asset;
    }

    [[nodiscard]] bool ensureImported(const VirtualPath& path);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<Asset>> cache_;
    ChangeListener changeListener_;
    AssetManagerMode mode_{AssetManagerMode::Development};
};

} // namespace engine

#define ASSET_MANAGER (::engine::AssetManager::instance())

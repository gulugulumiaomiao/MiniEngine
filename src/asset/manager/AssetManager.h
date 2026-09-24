#pragma once

#include "asset/base/Asset.h"
#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"

#include <concepts>
#include <functional>
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

    [[nodiscard]] Ref<Asset> loadAsset(const VirtualPath& path);

    template <std::derived_from<Asset> AssetTypeT>
    [[nodiscard]] Ref<AssetTypeT> loadAsset(const VirtualPath& path) {
        return refDynamicCast<AssetTypeT>(loadAsset(path));
    }

    void invalidate(const VirtualPath& path);
    /// 就地重传缓存中的资产（不丢弃、不新建对象）：用于纹理热重载把新内容推送给唯一运行时
    /// 实例（TextureAsset::transfer 读取分支会调用 syncInstance）。未缓存则退化为 invalidate。
    void reloadInPlace(const VirtualPath& path);
    void clear();
    void setChangeListener(ChangeListener listener) { changeListener_ = std::move(listener); }

private:
    friend class Singleton<AssetManager>;
    AssetManager() = default;

    [[nodiscard]] Ref<Asset> findCached(const VirtualPath& path) const {
        std::scoped_lock lock{mutex_};
        const auto found = cache_.find(path.string());
        if (found == cache_.end())
            return {};
        return found->second;
    }

    [[nodiscard]] Ref<Asset> cache(const VirtualPath& path, Ref<Asset> asset) {
        std::scoped_lock lock{mutex_};
        if (const auto found = cache_.find(path.string()); found != cache_.end())
            return found->second;
        cache_.insert_or_assign(path.string(), asset);
        return asset;
    }

    [[nodiscard]] bool ensureImported(const VirtualPath& path);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Ref<Asset>> cache_;
    ChangeListener changeListener_;
    AssetManagerMode mode_{AssetManagerMode::Development};
};

} // namespace engine

#define ASSET_MANAGER (::engine::AssetManager::instance())

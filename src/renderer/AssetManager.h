#pragma once

#include "core/Log.h"
#include "core/io/FileSystem.h"
#include "core/io/VirtualPath.h"
#include "renderer/Mesh.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace engine {

class ShaderAsset;
class MaterialAsset;

class AssetManager final {
public:
  [[nodiscard]] static AssetManager &instance();

  AssetManager(const AssetManager &) = delete;
  AssetManager &operator=(const AssetManager &) = delete;

  void setAssetRoot(std::filesystem::path assetRoot);

  [[nodiscard]] std::filesystem::path assetRoot() const;
  [[nodiscard]] std::filesystem::path
  resolvePath(const std::filesystem::path &path) const;
  [[nodiscard]] VirtualPath
  resolveVirtualPath(const std::filesystem::path &path) const;

  [[nodiscard]] std::shared_ptr<ShaderAsset>
  loadShaderAsset(const std::filesystem::path &path);
  [[nodiscard]] std::shared_ptr<ShaderAsset>
  loadShaderAsset(const VirtualPath &path);
  [[nodiscard]] std::shared_ptr<MaterialAsset>
  loadMaterialAsset(const std::filesystem::path &path);
  [[nodiscard]] std::shared_ptr<MaterialAsset>
  loadMaterialAsset(const VirtualPath &path);

  template <typename Loader>
  [[nodiscard]] std::shared_ptr<MeshAsset>
  loadMeshAsset(const std::filesystem::path &path, Loader &&loader) {
    return loadAsset<MeshAsset>(
        path,
        [&loader](const VirtualPath &, const std::filesystem::path &resolved) {
          return loader(resolved);
        });
  }

  void clear();

private:
  AssetManager() = default;

  using TypeCache = std::unordered_map<std::string, std::shared_ptr<void>>;

  template <typename Asset, typename Loader>
  [[nodiscard]] std::shared_ptr<Asset>
  loadAsset(const std::filesystem::path &path, Loader &&loader) {
    if (path.empty()) {
      Log::error("AssetManager", "Asset path must not be empty");
      return {};
    }
    VirtualPath virtualPath;
    if (path.is_absolute()) {
      virtualPath = VirtualPath::fromNative(path);
    } else {
      std::scoped_lock lock{mutex_};
      if (assetRoot_.empty()) {
        Log::error("AssetManager", "Asset root has not been initialized");
        return {};
      }
      virtualPath = VirtualPath{"asset://" + path.generic_string()};
    }
    if (!virtualPath.valid()) {
      Log::error("AssetManager", "Invalid asset path: %s", path.string().c_str());
      return {};
    }
    return loadAsset<Asset>(virtualPath, std::forward<Loader>(loader));
  }

  template <typename Asset, typename Loader>
  [[nodiscard]] std::shared_ptr<Asset>
  loadAsset(const VirtualPath &virtualPath, Loader &&loader) {
    if (!virtualPath.valid() || virtualPath.empty()) {
      Log::error("AssetManager", "Invalid virtual asset path: %s",
                 virtualPath.string().c_str());
      return {};
    }
    const auto physicalPath = FILE_SYSTEM.resolvePhysicalPath(virtualPath);
    if (!physicalPath) {
      Log::error("AssetManager", "Cannot resolve asset path: %s",
                 virtualPath.string().c_str());
      return {};
    }
    const std::filesystem::path resolved = physicalPath->lexically_normal();
    const std::string key = resolved.generic_string();
    if (std::shared_ptr<Asset> cached = findResolved<Asset>(key)) {
      return cached;
    }
    std::shared_ptr<Asset> loaded = loader(virtualPath, resolved);
    if (!loaded) {
      return {};
    }
    std::scoped_lock lock{mutex_};
    auto &cache = caches_[std::type_index(typeid(Asset))];
    const auto [entry, inserted] = cache.emplace(key, loaded);
    return inserted ? std::move(loaded)
                    : std::static_pointer_cast<Asset>(entry->second);
  }

  template <typename Asset>
  [[nodiscard]] std::shared_ptr<Asset>
  findResolved(const std::string &key) const {
    std::scoped_lock lock{mutex_};
    const auto cache = caches_.find(std::type_index(typeid(Asset)));
    if (cache == caches_.end()) {
      return {};
    }
    const auto asset = cache->second.find(key);
    return asset == cache->second.end()
               ? std::shared_ptr<Asset>{}
               : std::static_pointer_cast<Asset>(asset->second);
  }

  std::filesystem::path assetRoot_;
  mutable std::mutex mutex_;
  std::unordered_map<std::type_index, TypeCache> caches_;
};

} // namespace engine

#define ASSET_MANAGER (::engine::AssetManager::instance())

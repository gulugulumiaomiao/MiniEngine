#include "renderer/AssetManager.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"
#include "renderer/Shader.h"

#include <utility>

namespace engine {

AssetManager &AssetManager::instance() {
  static AssetManager manager;
  return manager;
}

void AssetManager::setAssetRoot(std::filesystem::path assetRoot) {
  assetRoot = std::move(assetRoot).lexically_normal();
  if (!FILE_SYSTEM.mountDirectory("asset", assetRoot, true)) {
    Log::fatal("AssetManager", "Cannot mount asset root: %s",
               assetRoot.string().c_str());
  }
  std::scoped_lock lock{mutex_};
  if (assetRoot_ != assetRoot) {
    caches_.clear();
    assetRoot_ = std::move(assetRoot);
  }
}

std::filesystem::path AssetManager::assetRoot() const {
  std::scoped_lock lock{mutex_};
  return assetRoot_;
}

std::filesystem::path
AssetManager::resolvePath(const std::filesystem::path &path) const {
  const VirtualPath virtualPath = resolveVirtualPath(path);
  const auto resolved = FILE_SYSTEM.resolvePhysicalPath(virtualPath);
  if (!resolved) {
    Log::fatal("AssetManager", "Cannot resolve asset path: %s",
               virtualPath.string().c_str());
  }
  return resolved->lexically_normal();
}

VirtualPath
AssetManager::resolveVirtualPath(const std::filesystem::path &path) const {
  if (path.empty()) {
    Log::fatal("AssetManager", "Asset path must not be empty");
  }
  if (path.is_absolute()) {
    return VirtualPath::fromNative(path);
  }
  std::scoped_lock lock{mutex_};
  if (assetRoot_.empty()) {
    Log::fatal("AssetManager", "Asset root has not been initialized");
  }
  const VirtualPath result{"asset://" + path.generic_string()};
  if (!result.valid()) {
    Log::fatal("AssetManager", "Invalid asset path: %s", path.string().c_str());
  }
  return result;
}

std::shared_ptr<ShaderAsset>
AssetManager::loadShaderAsset(const std::filesystem::path &path) {
  if (path.empty()) {
    Log::error("AssetManager", "ShaderAsset path must not be empty");
    return {};
  }
  return loadShaderAsset(path.is_absolute()
                             ? VirtualPath::fromNative(path)
                             : VirtualPath{"asset://" + path.generic_string()});
}

std::shared_ptr<ShaderAsset>
AssetManager::loadShaderAsset(const VirtualPath &path) {
  return loadAsset<ShaderAsset>(
      path, [](const VirtualPath &virtualPath,
               const std::filesystem::path &) {
        const auto source = FILE_SYSTEM.readText(virtualPath);
        if (!source) {
          Log::error("AssetManager", "Cannot load ShaderAsset: %s",
                     virtualPath.string().c_str());
          return std::shared_ptr<ShaderAsset>{};
        }
        return detail::parseShaderAsset(virtualPath, *source);
      });
}

std::shared_ptr<MaterialAsset>
AssetManager::loadMaterialAsset(const std::filesystem::path &path) {
  if (path.empty()) {
    Log::error("AssetManager", "MaterialAsset path must not be empty");
    return {};
  }
  return loadMaterialAsset(path.is_absolute()
                               ? VirtualPath::fromNative(path)
                               : VirtualPath{"asset://" + path.generic_string()});
}

std::shared_ptr<MaterialAsset>
AssetManager::loadMaterialAsset(const VirtualPath &path) {
  return loadAsset<MaterialAsset>(
      path, [this](const VirtualPath &virtualPath,
                   const std::filesystem::path &) {
        const auto source = FILE_SYSTEM.readText(virtualPath);
        if (!source) {
          Log::error("AssetManager", "Cannot load MaterialAsset: %s",
                     virtualPath.string().c_str());
          return std::shared_ptr<MaterialAsset>{};
        }
        auto material = detail::parseMaterialAsset(virtualPath, *source);
        if (!material) {
          return std::shared_ptr<MaterialAsset>{};
        }
        material->shaderAsset = loadShaderAsset(material->shader);
        if (!material->shaderAsset) {
          Log::error("AssetManager", "Material ShaderAsset failed to load: %s",
                     material->shader.string().c_str());
          return std::shared_ptr<MaterialAsset>{};
        }
        if (!validateMaterialAsset(*material, *material->shaderAsset,
                                   virtualPath)) {
          return std::shared_ptr<MaterialAsset>{};
        }
        return material;
      });
}

void AssetManager::clear() {
  std::scoped_lock lock{mutex_};
  caches_.clear();
}

} // namespace engine

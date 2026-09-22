#include "render/shader/ShaderManager.h"

#include "asset/types/ShaderAsset.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <utility>
#include <vector>

namespace engine {
namespace {

const VirtualPath kBuiltinColorShaderPath{"assets://shaders/builtin_color.shader.json"};

} // namespace

Ref<Shader> ShaderResourceManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("ShaderResourceManager", "Invalid Shader AssetId");
        return {};
    }
    if (Ref<Shader> existing = find(assetId))
        return existing;
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("ShaderResourceManager",
                   "Unknown Shader AssetId: %s",
                   assetId.toString().c_str());
        return {};
    }
    return loadFromPath(*path, assetId);
}

Ref<Shader> ShaderResourceManager::load(const VirtualPath& shaderPath) {
    if (!shaderPath.valid()) {
        Log::error("ShaderResourceManager", "Invalid Shader path: %s", shaderPath.string().c_str());
        return {};
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        Log::error("ShaderResourceManager",
                   "Shader path has no AssetId: %s",
                   shaderPath.string().c_str());
        return {};
    }
    if (Ref<Shader> existing = find(*assetId))
        return existing;
    return loadFromPath(shaderPath, *assetId);
}

Ref<Shader> ShaderResourceManager::loadFromPath(const VirtualPath& shaderPath,
                                                const AssetId& assetId) {
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    if (!asset)
        return {};
    Ref<Shader> shader = asset->instantiate();
    if (!shader)
        return {};
    shader->assetId_ = assetId;
    return insert(shader);
}

Ref<Shader> ShaderResourceManager::builtinColor() {
    if (!builtinColor_)
        builtinColor_ = load(kBuiltinColorShaderPath);
    return builtinColor_;
}

Ref<Shader> ShaderResourceManager::clone(const Ref<Shader>& source) {
    if (!source) {
        Log::error("ShaderResourceManager", "Cannot clone an invalid Shader");
        return {};
    }
    return insertUnkeyed(source->clone());
}

void ShaderResourceManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("ShaderResourceManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("ShaderResourceManager",
                   "Unknown Shader AssetId: %s",
                   assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(*path);
    Ref<Shader> shader = find(assetId);
    if (!asset || !shader) {
        Log::error("ShaderResourceManager",
                   "Failed to reload shader asset: %s",
                   path->string().c_str());
        return;
    }
    shader->rebuildFromAsset(*asset);
}

void ShaderResourceManager::refreshAsset(const VirtualPath& shaderPath) {
    if (!shaderPath.valid()) {
        Log::error("ShaderResourceManager", "Invalid Shader path: %s", shaderPath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        Log::error("ShaderResourceManager",
                   "Shader path has no AssetId: %s",
                   shaderPath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

Ref<Shader> ShaderResourceManager::insert(const Ref<Shader>& shader) {
    if (!shader)
        return {};
    if (shader->assetId_.valid()) {
        if (Ref<Shader> existing = find(shader->assetId_))
            return existing;
    }
    return insertUnkeyed(shader);
}

Ref<Shader> ShaderResourceManager::insertUnkeyed(const Ref<Shader>& shader) {
    if (!shader || shader->resourceId_)
        return {};
    const RID handle = resources_.insert(shader.get());
    shader->resourceId_ = handle;
    if (shader->assetId_.valid())
        assetIndex_.insert_or_assign(shader->assetId_, handle);
    return shader;
}

Shader* ShaderResourceManager::findRaw(RID handle) const {
    Shader* const* stored = resources_.find(handle);
    return stored ? *stored : nullptr;
}

Ref<Shader> ShaderResourceManager::find(RID handle) const {
    return Ref<Shader>{findRaw(handle)};
}

Ref<Shader> ShaderResourceManager::find(const AssetId& assetId) const {
    const auto found = assetIndex_.find(assetId);
    return found == assetIndex_.end() ? Ref<Shader>{} : find(found->second);
}

Ref<Shader> ShaderResourceManager::find(const VirtualPath& shaderPath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    return assetId ? find(*assetId) : Ref<Shader>{};
}

bool ShaderResourceManager::replace(const VirtualPath& shaderPath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId)
        return true;
    Ref<Shader> shader = find(*assetId);
    if (!shader)
        return true;
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    if (!asset)
        return false;
    shader->rebuildFromAsset(*asset);
    return true;
}

void ShaderResourceManager::unregister(Shader* shader) {
    if (!shader || !shader->resourceId_)
        return;
    const RID handle = shader->resourceId_;
    if (shader->assetId_.valid()) {
        const auto found = assetIndex_.find(shader->assetId_);
        if (found != assetIndex_.end() && found->second == handle)
            assetIndex_.erase(found);
    }
    if (destroyObserver_)
        destroyObserver_(handle);
    shader->resourceId_ = {};
    (void)resources_.release(handle);
}

void ShaderResourceManager::clear() {
    builtinColor_.reset();
    std::vector<std::pair<RID, Shader*>> remaining;
    remaining.reserve(resources_.size());
    resources_.forEachHandle(
        [&remaining](RID handle, Shader* shader) { remaining.emplace_back(handle, shader); });
    for (const auto& [handle, shader] : remaining) {
        if (!findRaw(handle))
            continue;
        if (destroyObserver_)
            destroyObserver_(handle);
        shader->resourceId_ = {};
        (void)resources_.release(handle);
    }
    assetIndex_.clear();
}

} // namespace engine

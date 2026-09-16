#include "render/shader/ShaderManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <limits>
#include <utility>

namespace engine {
namespace {

// The engine's fallback Shader is part of the built-in contract layer, refreshed into
// every project's assets/ on open (syncEngineContractIntoProject), so it resolves through
// the project's assets:// mount like any user asset.
const VirtualPath kBuiltinColorShaderPath{"assets://shaders/builtin_color.shader.json"};

} // namespace

ShaderHandle ShaderManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("ShaderManager", "Invalid Shader AssetId");
        return {};
    }
    if (const ShaderHandle existing = findHandle(assetId); existing) {
        return existing;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error(
            "ShaderManager", "Unknown Shader AssetId: %s", assetId.toString().c_str());
        return {};
    }
    return loadFromPath(*path, assetId);
}

ShaderHandle ShaderManager::load(const VirtualPath& shaderPath) {
    if (!shaderPath.valid()) {
        Log::error("ShaderManager", "Invalid Shader path: %s", shaderPath.string().c_str());
        return {};
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        Log::error(
            "ShaderManager", "Shader path has no AssetId: %s", shaderPath.string().c_str());
        return {};
    }
    if (const ShaderHandle existing = findHandle(*assetId); existing) {
        return existing;
    }
    return loadFromPath(shaderPath, *assetId);
}

ShaderHandle ShaderManager::loadFromPath(const VirtualPath& shaderPath, const AssetId& assetId) {
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    if (!asset) {
        return {};
    }
    Shader shader = asset->instantiate();
    shader.assetId_ = assetId;
    return insert(std::move(shader));
}

ShaderHandle ShaderManager::builtinColor() {
    return load(kBuiltinColorShaderPath);
}

ShaderHandle ShaderManager::clone(ShaderHandle source) {
    Shader* shader = find(source);
    if (!shader) {
        Log::error("ShaderManager", "Cannot clone an invalid Shader");
        return {};
    }
    return insertUnkeyed(shader->clone());
}

void ShaderManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("ShaderManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error(
            "ShaderManager", "Unknown Shader AssetId: %s", assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(*path);
    if (!asset) {
        Log::error(
            "ShaderManager", "Failed to reload shader asset: %s", path->string().c_str());
        return;
    }
    forEach([&assetId, &asset](Shader& shader) {
        if (shader.assetId() == assetId) {
            shader.rebuildFromAsset(*asset);
        }
    });
}

void ShaderManager::refreshAsset(const VirtualPath& shaderPath) {
    if (!shaderPath.valid()) {
        Log::error("ShaderManager", "Invalid Shader path: %s", shaderPath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        Log::error(
            "ShaderManager", "Shader path has no AssetId: %s", shaderPath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

ShaderHandle ShaderManager::findHandle(const VirtualPath& shaderPath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        return {};
    }
    return KeyedHandleRegistry::findHandle(*assetId);
}

bool ShaderManager::replace(ShaderHandle handle, Shader shader) {
    Shader* current = find(handle);
    if (!current) {
        Log::error("ShaderManager", "Cannot replace an invalid ShaderHandle");
        return false;
    }
    if (current->assetPath() != shader.assetPath()) {
        Log::error("ShaderManager",
                   "Replacement Shader path differs: %s",
                   shader.assetPath().string().c_str());
        return false;
    }
    if (current->revision_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::fatal("ShaderManager", "Shader revision overflow");
    }
    const AssetId assetId = current->assetId_;
    shader.revision_ = current->revision_ + 1;
    *current = std::move(shader);
    current->assetId_ = assetId;
    return true;
}

bool ShaderManager::replace(const VirtualPath& shaderPath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(shaderPath);
    if (!assetId) {
        return true;
    }
    const ShaderHandle handle = findHandle(*assetId);
    if (!handle) {
        return true;
    }
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    if (!asset) {
        return false;
    }
    Shader* current = find(handle);
    if (!current) {
        return false;
    }
    current->rebuildFromAsset(*asset);
    return true;
}

} // namespace engine

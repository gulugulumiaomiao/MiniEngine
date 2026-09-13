#include "render/shader/ShaderManager.h"

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

ShaderHandle ShaderManager::load(const VirtualPath& shaderPath) {
    if (!shaderPath.valid()) {
        Log::error("ShaderManager", "Invalid Shader path: %s", shaderPath.string().c_str());
        return {};
    }
    if (const ShaderHandle existing = findHandle(shaderPath); existing)
        return existing;
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    return asset ? insert(asset->instantiate()) : ShaderHandle{};
}

ShaderHandle ShaderManager::builtinColor() {
    return load(kBuiltinColorShaderPath);
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
    shader.revision_ = current->revision_ + 1;
    *current = std::move(shader);
    return true;
}

bool ShaderManager::replace(const VirtualPath& shaderPath) {
    const ShaderHandle handle = findHandle(shaderPath);
    if (!handle)
        return true;
    const std::shared_ptr<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(shaderPath);
    return asset && replace(handle, asset->instantiate());
}

} // namespace engine

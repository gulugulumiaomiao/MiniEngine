#include "asset/runtime/ShaderAssetResolver.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

namespace engine {

Ref<Shader> resolveShaderReference(std::string_view reference) {
    if (reference.empty())
        return {};
    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    if (path.valid() && path.scheme() == "assets") {
        if (const Ref<ShaderAsset> asset = ASSET_MANAGER.loadAsset<ShaderAsset>(path)) {
            if (Ref<Shader> shader = asset->instantiate())
                return shader;
        }
    }
    Log::warn("Shader",
              "Cannot resolve shader reference: %.*s",
              static_cast<int>(reference.size()),
              reference.data());
    return {};
}

Ref<ShaderAsset> findShaderAsset(const Shader& shader) {
    return ASSET_MANAGER.findShaderAsset(shader);
}

VirtualPath shaderAssetPath(const Shader& shader) {
    const auto asset = findShaderAsset(shader);
    return asset ? asset->assetPath() : VirtualPath{};
}

} // namespace engine

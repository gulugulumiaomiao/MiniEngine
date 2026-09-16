#pragma once

#include "asset/base/AssetId.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/shader/Shader.h"

namespace engine {

class ShaderManager final : public Singleton<ShaderManager>,
                            public KeyedHandleRegistry<Shader, ShaderHandle, AssetId> {
public:
    [[nodiscard]] ShaderHandle load(const AssetId& assetId);
    [[nodiscard]] ShaderHandle load(const VirtualPath& shaderPath);
    [[nodiscard]] ShaderHandle builtinColor();
    [[nodiscard]] ShaderHandle clone(ShaderHandle source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& shaderPath);

    using KeyedHandleRegistry<Shader, ShaderHandle, AssetId>::find;
    using KeyedHandleRegistry<Shader, ShaderHandle, AssetId>::findHandle;
    [[nodiscard]] ShaderHandle findHandle(const VirtualPath& shaderPath) const;

    [[nodiscard]] bool replace(ShaderHandle handle, Shader shader);
    [[nodiscard]] bool replace(const VirtualPath& shaderPath);

private:
    friend class Singleton<ShaderManager>;
    ShaderManager() = default;

    [[nodiscard]] AssetId keyOf(const Shader& shader) const override {
        return shader.assetId();
    }
    [[nodiscard]] ShaderHandle loadFromPath(const VirtualPath& path, const AssetId& assetId);
};

} // namespace engine

#define SHADER_MANAGER (::engine::ShaderManager::instance())

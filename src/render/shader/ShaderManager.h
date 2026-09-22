#pragma once

#include "asset/base/AssetId.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/shader/Shader.h"

namespace engine {

class ShaderManager final : public Singleton<ShaderManager>,
                            public KeyedHandleRegistry<Shader, RID, AssetId> {
public:
    [[nodiscard]] RID load(const AssetId& assetId);
    [[nodiscard]] RID load(const VirtualPath& shaderPath);
    [[nodiscard]] RID builtinColor();
    [[nodiscard]] RID clone(RID source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& shaderPath);

    using KeyedHandleRegistry<Shader, RID, AssetId>::find;
    using KeyedHandleRegistry<Shader, RID, AssetId>::findHandle;
    [[nodiscard]] RID findHandle(const VirtualPath& shaderPath) const;

    [[nodiscard]] bool replace(RID handle, Shader shader);
    [[nodiscard]] bool replace(const VirtualPath& shaderPath);

private:
    friend class Singleton<ShaderManager>;
    ShaderManager() = default;

    [[nodiscard]] AssetId keyOf(const Shader& shader) const override {
        return shader.assetId();
    }
    [[nodiscard]] RID loadFromPath(const VirtualPath& path, const AssetId& assetId);
};

} // namespace engine

#define SHADER_MANAGER (::engine::ShaderManager::instance())

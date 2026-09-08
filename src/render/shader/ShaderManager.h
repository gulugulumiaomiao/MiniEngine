#pragma once

#include "core/base/InstanceManager.h"
#include "core/base/Singleton.h"
#include "render/shader/Shader.h"

namespace engine {

class ShaderManager final : public Singleton<ShaderManager>,
                            public InstanceManager<Shader, ShaderHandle> {
public:
    [[nodiscard]] ShaderHandle load(const VirtualPath& shaderPath) override;
    [[nodiscard]] bool replace(ShaderHandle handle, Shader shader);
    [[nodiscard]] bool replace(const VirtualPath& shaderPath);

private:
    friend class Singleton<ShaderManager>;
    ShaderManager() = default;

    [[nodiscard]] const VirtualPath& pathOf(const Shader& shader) const override {
        return shader.assetPath();
    }
};

} // namespace engine

#define SHADER_MANAGER (::engine::ShaderManager::instance())

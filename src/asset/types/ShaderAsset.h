#pragma once

#include "asset/base/Asset.h"
#include "core/base/Ref.h"
#include "render/shader/Shader.h"

#include <string>
#include <vector>

namespace engine {

class ShaderAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Shader; }

    std::string name;
    std::vector<ShaderPropertyDesc> properties;
    std::vector<ShaderPropertyDesc> globalProperties;
    std::vector<SubShaderDesc> subShaders;

    [[nodiscard]] const ShaderPropertyDesc* findProperty(const std::string& name) const;
    [[nodiscard]] const ShaderPropertyDesc* findGlobalProperty(const std::string& name) const;
    [[nodiscard]] Ref<Shader> instantiate() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine

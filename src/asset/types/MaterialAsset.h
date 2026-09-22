#pragma once

#include "asset/base/Asset.h"
#include "core/base/Ref.h"
#include "render/shader/Shader.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class Material;

class MaterialAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Material; }

    std::string name;
    VirtualPath shader;
    std::unordered_map<std::string, ShaderValue> properties;
    std::vector<std::string> keywords;
    std::optional<int> renderQueue;

    [[nodiscard]] Ref<Material> instantiate(const Ref<Shader>& shader) const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine

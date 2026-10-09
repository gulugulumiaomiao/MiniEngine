#pragma once

#include "asset/base/Asset.h"
#include "asset/types/ShaderAssetData.h"
#include "core/base/Ref.h"
#include "render/shader/Shader.h"

#include <optional>

namespace engine {

class ShaderAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Shader; }
    ShaderAssetData data;

    [[nodiscard]] const ShaderPropertyAssetData* findProperty(const std::string& name) const;
    [[nodiscard]] const ShaderPropertyAssetData* findGlobalProperty(const std::string& name) const;
    [[nodiscard]] Ref<Shader> instantiate();
    [[nodiscard]] Ref<Shader> clone() const;
    [[nodiscard]] const Ref<Shader>& runtimeInstance() const { return instance_; }
    [[nodiscard]] std::optional<ShaderCreateInfo> buildRuntimeInfo() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;

private:
    Ref<Shader> instance_;
};

} // namespace engine

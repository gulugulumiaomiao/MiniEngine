#include "asset/types/MaterialAsset.h"
#include "asset/runtime/MaterialAssetResolver.h"
#include "asset/runtime/ShaderAssetResolver.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include <algorithm>
namespace engine {
std::optional<MaterialCreateInfo>
MaterialAsset::buildRuntimeInfo(const Ref<Shader>& overrideShader) const {
    MaterialCreateInfo info;
    info.name = data.name;
    info.shader = overrideShader ? overrideShader : resolveShaderReference(data.shader.string());
    if (!info.shader)
        return {};
    info.renderQueue = data.renderQueue;
    info.keywords = data.keywords;
    info.defaultTextureBinding = [](const ShaderPropertyInfo& property) {
        const auto* reference = std::get_if<std::string>(&property.defaultValue);
        return materialTextureBinding(reference ? *reference : std::string{});
    };
    for (const auto& [name, value] : data.properties) {
        const auto found =
            std::ranges::find(info.shader->properties(), name, &ShaderPropertyInfo::name);
        if (found == info.shader->properties().end())
            return {};
        if (found->type == ShaderPropertyType::Texture2D) {
            const auto* reference = std::get_if<std::string>(&value);
            if (!reference)
                return {};
            info.textures.emplace(name, materialTextureBinding(*reference));
        } else {
            info.properties.emplace(
                name, std::visit([](const auto& v) -> ShaderValue { return v; }, value));
        }
    }
    return validateMaterialCreateInfo(info) ? std::optional<MaterialCreateInfo>{std::move(info)}
                                            : std::nullopt;
}
Ref<Material> MaterialAsset::instantiate() {
    if (!instance_) {
        auto info = buildRuntimeInfo();
        if (info)
            instance_ = makeRef<Material>(std::move(*info));
    }
    return instance_;
}
Ref<Material> MaterialAsset::clone() const {
    auto info = buildRuntimeInfo();
    return info ? makeRef<Material>(std::move(*info)) : Ref<Material>{};
}
bool MaterialAsset::transfer(Transfer& archive) {
    if (archive.writing())
        return data.transfer(archive);
    MaterialAsset decoded;
    if (!decoded.data.transfer(archive))
        return false;
    if (instance_) {
        auto info = decoded.buildRuntimeInfo();
        if (!info || !instance_->rebuild(std::move(*info)))
            return false;
    }
    data = std::move(decoded.data);
    return true;
}
} // namespace engine

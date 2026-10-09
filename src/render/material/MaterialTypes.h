#pragma once
#include "render/shader/Shader.h"
#include "render/texture/Texture.h"
#include <functional>
#include <optional>
#include <unordered_map>
namespace engine {
struct MaterialTextureBinding {
    Ref<Texture> texture;
    Ref<Sampler> sampler;
    std::function<Ref<Texture>()> resolve;
};
struct MaterialCreateInfo {
    std::string name;
    Ref<Shader> shader;
    std::unordered_map<std::string, ShaderValue> properties;
    std::unordered_map<std::string, MaterialTextureBinding> textures;
    std::vector<std::string> keywords;
    std::optional<int> renderQueue;
    std::function<MaterialTextureBinding(const ShaderPropertyInfo&)> defaultTextureBinding;
};
[[nodiscard]] bool validateMaterialCreateInfo(const MaterialCreateInfo& info);
} // namespace engine

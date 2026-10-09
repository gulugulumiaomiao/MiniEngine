#pragma once

#include "asset/types/ShaderAsset.h"

namespace engine {

[[nodiscard]] Ref<Shader> resolveShaderReference(std::string_view reference);
[[nodiscard]] Ref<ShaderAsset> findShaderAsset(const Shader& shader);
[[nodiscard]] VirtualPath shaderAssetPath(const Shader& shader);

} // namespace engine

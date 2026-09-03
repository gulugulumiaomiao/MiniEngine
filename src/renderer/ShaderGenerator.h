#pragma once

#include "renderer/Shader.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::shader_compiler {

struct ShaderGenerationOptions {
    std::uint32_t materialSet{1};
    std::uint32_t uniformBinding{0};
    std::uint32_t firstTextureBinding{1};
    std::string uniformBlockName{"MaterialProperties"};
    std::string uniformInstanceName{"Material"};
};

struct GeneratedTextureBinding {
    std::string propertyName;
    std::uint32_t set{};
    std::uint32_t binding{};
};

struct GeneratedMaterialDeclarations {
    std::string glsl;
    std::uint32_t uniformBlockSize{};
    std::vector<GeneratedTextureBinding> textures;
};

struct GeneratedPassStages {
    std::string vertexGlsl;
    std::string fragmentGlsl;
};

[[nodiscard]] std::shared_ptr<std::string> generateShaderStage(
    const ShaderAsset& shader, const ShaderPassDesc& pass,
    const UniformBlockLayout& layout, ShaderStage stage,
    std::string_view source,
    const ShaderGenerationOptions& options = {});

[[nodiscard]] std::shared_ptr<GeneratedMaterialDeclarations> generateMaterialDeclarations(
    const ShaderAsset& shader, const UniformBlockLayout& layout,
    const ShaderGenerationOptions& options = {});

[[nodiscard]] std::shared_ptr<GeneratedPassStages> generatePassStages(
    const ShaderAsset& shader, const ShaderPassDesc& pass,
    const UniformBlockLayout& layout, std::string_view vertexSource,
    std::string_view fragmentSource,
    const ShaderGenerationOptions& options = {});

} // namespace engine::shader_compiler

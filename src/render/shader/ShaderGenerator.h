#pragma once

#include "render/shader/Shader.h"

#include <memory>
#include <string>
#include <string_view>

namespace engine::shader_compiler {

[[nodiscard]] std::shared_ptr<std::string> generateShaderStage(const Shader& shader,
                                                               const ShaderPass& pass,
                                                               ShaderStage stage,
                                                               std::string_view source);

} // namespace engine::shader_compiler

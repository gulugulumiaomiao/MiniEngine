#pragma once

// .shader.json source format: parse a ShaderLab-JSON document into a ShaderAsset.
// Importers and exporters share this unit; the runtime Shader classes no longer
// carry source-file parsing.

#include "render/shader/Shader.h"

#include <memory>
#include <string_view>

namespace engine::format {

// Returns nullptr on any schema violation; failures are logged with the
// "ShaderAsset" channel and include the JSON path of the offending field.
[[nodiscard]] std::shared_ptr<ShaderAsset> parseShaderAsset(const VirtualPath& path,
                                                            std::string_view source);

} // namespace engine::format

#pragma once

// .material.json source format: parse a material document into a MaterialAsset
// and validate it against the shader it references. Importers and exporters
// share this unit; the runtime Material classes no longer carry source-file
// parsing.

#include "asset/base/GuidResolver.h"
#include "render/material/Material.h"

#include <memory>
#include <string_view>

namespace engine::format {

// Path references resolve inside the mount that owns the material; GUID
// references require a resolver. Returns nullptr on any schema violation.
[[nodiscard]] std::shared_ptr<MaterialAsset> parseMaterialAsset(const VirtualPath& path,
                                                                std::string_view source);
[[nodiscard]] std::shared_ptr<MaterialAsset> parseMaterialAsset(
    const VirtualPath& path, std::string_view source, const GuidResolver& resolver);

// Cross-asset validation: every property and keyword must be declared by the
// referenced shader, and property values must match the declared types.
[[nodiscard]] bool validateMaterialAsset(const MaterialAsset& material,
                                         const ShaderAsset& shader,
                                         const VirtualPath& materialPath);

} // namespace engine::format

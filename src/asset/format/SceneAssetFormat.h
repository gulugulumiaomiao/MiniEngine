#pragma once

// .scene.json source format: parse a scene document into a SceneAsset and
// validate its structure. Parsing, binary transfer and export all share
// validateSceneAsset; the runtime Scene classes no longer carry source-file
// parsing.

#include "asset/base/GuidResolver.h"
#include "scene/scene/SceneAsset.h"

#include <memory>
#include <string_view>

namespace engine::format {

// Mesh and Material component references may be GUIDs (resolver required) or
// virtual paths. Returns nullptr on any schema or validation violation.
[[nodiscard]] std::shared_ptr<SceneAsset> parseSceneAsset(const VirtualPath& path,
                                                          std::string_view source);
[[nodiscard]] std::shared_ptr<SceneAsset> parseSceneAsset(
    const VirtualPath& path, std::string_view source, const GuidResolver& resolver);

// Serializes a SceneAsset back into the source .scene.json format that
// parseSceneAsset reads. Asset paths are written as GUID references when the
// resolver can find them, otherwise as absolute assets:// virtual paths.
[[nodiscard]] std::string writeSceneAssetJson(const SceneAsset& asset,
                                              const GuidResolver& resolver);

// Structural validation: node identity and hierarchy (acyclic, existing
// parents), one Transform per node, no duplicate component types and
// per-component value ranges.
[[nodiscard]] bool validateSceneAsset(const SceneAsset& asset, const VirtualPath& scenePath);

} // namespace engine::format

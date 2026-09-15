#pragma once

#include "core/filesystem/VirtualPath.h"

#include <memory>
#include <string>

namespace engine {

class Scene;
class SceneAsset;

// Converts a runtime Scene back into a SceneAsset suitable for saving as source
// .scene.json. Node ids are assigned deterministically in depth-first order and the
// synthetic scene root is not serialized. error receives a human readable reason on
// failure; a null result means at least one component cannot be represented.
[[nodiscard]] std::unique_ptr<SceneAsset> exportSceneToAsset(const Scene& scene,
                                                             const VirtualPath& targetPath,
                                                             std::string& error);

// Serializes a SceneAsset into the source scene JSON format that
// format::parseSceneAsset reads. Asset paths are written as absolute assets:// virtual paths.
[[nodiscard]] std::string writeSceneAssetJson(const SceneAsset& asset);

// Exports the runtime Scene, validates it and atomically writes the source JSON to
// targetPath. error receives a human readable reason on failure.
[[nodiscard]] bool saveSceneToFile(const Scene& scene,
                                   const VirtualPath& targetPath,
                                   std::string& error);

} // namespace engine

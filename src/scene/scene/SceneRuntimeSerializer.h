#pragma once

// 运行时 Scene -> SceneAsset 提取。这是 scene 模块的运行时序列化辅助，与
// asset/format/SceneAssetFormat 的源格式解析/编码对称。

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

} // namespace engine

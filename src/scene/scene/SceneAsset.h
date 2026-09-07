#pragma once

#include "asset/base/Asset.h"
#include "render/renderer/RenderResources.h"
#include "scene/node/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class Scene;

struct SceneInstantiationContext {
    std::function<MeshHandle(const VirtualPath&)> loadMesh;
    std::function<MaterialHandle(const VirtualPath&)> loadMaterial;
};

class SceneAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Scene; }

    std::string name{"Scene"};
    std::vector<SceneNodeAsset> nodes;

    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] std::unique_ptr<Scene>
    instantiate(const SceneInstantiationContext& context) const;
};

namespace detail {

[[nodiscard]] std::shared_ptr<SceneAsset> parseSceneAsset(const VirtualPath& path,
                                                          std::string_view source);

} // namespace detail

[[nodiscard]] bool validateSceneAsset(const SceneAsset& asset, const VirtualPath& scenePath);

} // namespace engine

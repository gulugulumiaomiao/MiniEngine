#pragma once

#include "asset/base/Asset.h"
#include "render/material/Material.h"
#include "render/mesh/Mesh.h"
#include "render/base/RenderHandle.h"
#include "scene/node/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class Scene;

struct SceneInstantiationContext {
    std::function<Ref<Mesh>(const VirtualPath&)> loadMesh;
    std::function<Ref<Material>(const VirtualPath&)> loadMaterial;
};

class SceneAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Scene; }

    std::string name{"Scene"};
    std::vector<SceneNodeAsset> nodes;

    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Ref<Scene>
    instantiate(const SceneInstantiationContext& context) const;
};
// Source .scene.json parsing and validation live in asset/format/SceneAssetFormat.

} // namespace engine

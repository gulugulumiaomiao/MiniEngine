#pragma once
#include "asset/base/Asset.h"
#include "asset/types/MeshAssetData.h"
#include "render/mesh/Mesh.h"
namespace engine {
class MeshAsset final : public Asset {
public:
    AssetType type() const override { return AssetType::Mesh; }
    MeshAssetData data;
    std::optional<MeshCreateInfo> buildRuntimeInfo() const;
    Ref<Mesh> instantiate();
    Ref<Mesh> clone() const;
    const Ref<Mesh>& runtimeInstance() const { return instance_; }
    bool transfer(Transfer& archive) override;

private:
    Ref<Mesh> instance_;
};
} // namespace engine

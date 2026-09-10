#pragma once

#include "core/serialization/Transferable.h"
#include "core/filesystem/VirtualPath.h"
#include "render/base/RenderHandle.h"
#include "render/mesh/Mesh.h"
#include "scene/components/Component.h"

#include <cstdint>
#include <optional>

namespace engine {

enum class MeshComponentSourceType { Asset, Primitive };

struct MeshComponentAsset final : public Transferable {
    MeshComponentSourceType sourceType{MeshComponentSourceType::Asset};
    VirtualPath mesh;
    MeshBuildRecipe primitiveRecipe;
    bool enabled{true};
    bool visible{true};
    bool castShadow{true};
    bool receiveShadow{true};
    std::uint32_t layerMask{1};

    bool operator==(const MeshComponentAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class MeshComponent final : public Component {
public:
    void setAssetMesh(MeshHandle mesh);
    void setPrimitive(const PlaneGeometry& geometry);
    void setPrimitive(const BoxGeometry& geometry);
    void setPrimitive(const UvSphereGeometry& geometry);
    void setPrimitive(const CylinderGeometry& geometry);
    void setPrimitiveRecipe(MeshBuildRecipe recipe);
    [[nodiscard]] bool applyPrimitiveChanges();

    [[nodiscard]] MeshComponentSourceType sourceType() const { return sourceType_; }
    [[nodiscard]] MeshHandle mesh() const { return mesh_; }
    [[nodiscard]] const MeshBuildRecipe* primitiveRecipe() const {
        return primitiveRecipe_ ? &*primitiveRecipe_ : nullptr;
    }
    [[nodiscard]] MeshBuildRecipe* editPrimitiveRecipe() {
        if (sourceType_ != MeshComponentSourceType::Primitive || !primitiveRecipe_)
            return nullptr;
        primitiveDirty_ = true;
        return &*primitiveRecipe_;
    }

    bool visible{true};
    bool castShadow{true};
    bool receiveShadow{true};
    std::uint32_t layerMask{1};

protected:
    void onAttach() override;
    void onDetach() override;
    void onEnable() override;
    void onUpdate(float deltaTime) override;

private:
    template <typename Geometry> void setSinglePrimitive(const Geometry& geometry);
    void releaseOwnedMesh();

    MeshComponentSourceType sourceType_{MeshComponentSourceType::Asset};
    MeshHandle mesh_;
    std::optional<MeshBuildRecipe> primitiveRecipe_;
    bool ownsRuntimeMesh_{};
    bool primitiveDirty_{};
};

} // namespace engine

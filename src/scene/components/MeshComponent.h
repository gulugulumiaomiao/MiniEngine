#pragma once

#include "core/serialization/Transferable.h"
#include "core/filesystem/VirtualPath.h"
#include "render/base/RenderHandle.h"
#include "scene/components/Component.h"

#include <cstdint>

namespace engine {

struct MeshComponentAsset final : public Transferable {
    VirtualPath mesh;
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
    MeshHandle mesh;
    bool visible{true};
    bool castShadow{true};
    bool receiveShadow{true};
    std::uint32_t layerMask{1};
};

} // namespace engine

#pragma once

#include "core/serialization/Transferable.h"
#include "core/filesystem/VirtualPath.h"
#include "render/base/RenderHandle.h"
#include "scene/components/Component.h"

#include <cstdint>
#include <vector>

namespace engine {

struct MaterialComponentAsset final : public Transferable {
    std::vector<VirtualPath> materials;
    bool enabled{true};

    bool operator==(const MaterialComponentAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class MaterialComponent final : public Component {
public:
    void setMaterial(std::uint32_t slot, MaterialHandle material) {
        if (materials_.size() <= slot)
            materials_.resize(slot + 1);
        materials_[slot] = material;
    }

    [[nodiscard]] MaterialHandle material(std::uint32_t slot) const {
        if (slot < materials_.size() && materials_[slot]) {
            return materials_[slot];
        }
        return !materials_.empty() ? materials_.front() : MaterialHandle{};
    }

    [[nodiscard]] const std::vector<MaterialHandle>& materials() const { return materials_; }

    void clearMaterials() { materials_.clear(); }

private:
    // Handles are non-owning. MaterialManager controls material lifetime.
    std::vector<MaterialHandle> materials_;
};

} // namespace engine

#pragma once

#include "core/serialization/Transferable.h"
#include "core/math/Math.h"
#include "render/renderer/Lighting.h"
#include "scene/components/Component.h"

#include <cstdint>

namespace engine {

struct LightComponentAsset final : public Transferable {
    LightType type{LightType::Directional};
    math::Vec3 color{1.0F};
    float intensity{1.0F};
    float range{10.0F};
    float innerSpotAngle{20.0F};
    float outerSpotAngle{30.0F};
    bool castShadow{};
    std::uint32_t cullingMask{0xFFFFFFFFU};
    bool enabled{true};

    bool operator==(const LightComponentAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class LightComponent final : public Component {
public:
    LightType type{LightType::Directional};
    math::Vec3 color{1.0F};
    float intensity{1.0F};
    float range{10.0F};
    float innerSpotAngle{20.0F};
    float outerSpotAngle{30.0F};
    bool castShadow{};
    std::uint32_t cullingMask{0xFFFFFFFFU};
};

} // namespace engine

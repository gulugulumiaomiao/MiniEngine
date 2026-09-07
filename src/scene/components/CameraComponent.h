#pragma once

#include "core/serialization/Transferable.h"
#include "core/math/Math.h"
#include "scene/components/Component.h"

#include <cstdint>

namespace engine {

enum class CameraProjection { Perspective, Orthographic };

struct CameraComponentAsset final : public Transferable {
    CameraProjection projection{CameraProjection::Perspective};
    float fieldOfView{60.0F};
    float orthographicSize{5.0F};
    float nearPlane{0.1F};
    float farPlane{1000.0F};
    math::Vec4 clearColor{0.025F, 0.055F, 0.10F, 1.0F};
    std::uint32_t cullingMask{0xFFFFFFFFU};
    int priority{};
    bool primary{};
    bool enabled{true};

    bool operator==(const CameraComponentAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class CameraComponent final : public Component {
public:
    [[nodiscard]] math::Mat44 projectionMatrix(float aspectRatio) const {
        if (projection == CameraProjection::Orthographic) {
            const float halfWidth = orthographicSize * aspectRatio;
            return math::orthographic(
                -halfWidth, halfWidth, -orthographicSize, orthographicSize, nearPlane, farPlane);
        }
        return math::perspective(math::radians(fieldOfView), aspectRatio, nearPlane, farPlane);
    }

    CameraProjection projection{CameraProjection::Perspective};
    float fieldOfView{60.0F};
    float orthographicSize{5.0F};
    float nearPlane{0.1F};
    float farPlane{1000.0F};
    math::Vec4 clearColor{0.025F, 0.055F, 0.10F, 1.0F};
    std::uint32_t cullingMask{0xFFFFFFFFU};
    int priority{};
    bool primary{};
};

} // namespace engine

#pragma once

#include "core/serialization/Transferable.h"
#include "core/math/Math.h"
#include "scene/components/Component.h"

namespace engine {

class Scene;

struct TransformComponentAsset final : public Transferable {
    math::Vec3 position{0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F};

    bool operator==(const TransformComponentAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class TransformComponent final : public Component {
public:
    [[nodiscard]] const math::Vec3& localPosition() const { return localPosition_; }
    [[nodiscard]] const math::Quat& localRotation() const { return localRotation_; }
    [[nodiscard]] const math::Vec3& localScale() const { return localScale_; }

    void setLocalPosition(const math::Vec3& position);
    void setLocalRotation(const math::Quat& rotation);
    void setLocalScale(const math::Vec3& scale);

    [[nodiscard]] const math::Mat44& localMatrix() const;
    [[nodiscard]] const math::Mat44& worldMatrix() const;
    [[nodiscard]] math::Vec3 worldPosition() const;

private:
    friend class Node;
    friend class Scene;

    void markLocalDirty();
    void markWorldDirty() { worldDirty_ = true; }
    [[nodiscard]] bool updateWorld(const math::Mat44& parentWorld, bool parentChanged);

    math::Vec3 localPosition_{0.0F};
    math::Quat localRotation_{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 localScale_{1.0F};
    mutable math::Mat44 localMatrix_{1.0F};
    math::Mat44 worldMatrix_{1.0F};
    mutable bool localDirty_{true};
    bool worldDirty_{true};
};

} // namespace engine

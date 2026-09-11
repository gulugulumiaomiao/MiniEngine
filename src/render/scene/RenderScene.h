#pragma once

#include "core/math/Math.h"
#include "render/base/RenderHandle.h"
#include "render/scene/Lighting.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace engine {

struct RenderObject {
    MeshHandle mesh;
    std::vector<MaterialHandle> materials;
    math::Mat44 transform{1.0F};
    std::uint32_t layerMask{1};
    float boundsRadius{0.0F}; // Bounding sphere radius in world units; 0 treats the object as a point.
    bool castShadow{true};
    bool receiveShadow{true};

    [[nodiscard]] MaterialHandle material(std::uint32_t slot) const {
        if (slot < materials.size() && materials[slot])
            return materials[slot];
        return !materials.empty() ? materials.front() : MaterialHandle{};
    }
};

struct RenderCamera {
    math::Mat44 view{1.0F};
    math::Mat44 projection{1.0F};
    math::Vec3 worldPosition{0.0F};
    math::Vec4 clearColor{0.025F, 0.055F, 0.10F, 1.0F};
    std::uint32_t cullingMask{0xFFFFFFFFU};
    int priority{};
    bool primary{};
};

struct RenderLight {
    LightType type{LightType::Directional};
    math::Vec3 position{0.0F};
    math::Vec3 direction{0.0F, 0.0F, -1.0F};
    math::Vec3 color{1.0F};
    float intensity{1.0F};
    float range{10.0F};
    float innerSpotAngle{20.0F};
    float outerSpotAngle{30.0F};
    bool castShadow{};
    std::uint32_t cullingMask{0xFFFFFFFFU};
};

class RenderScene final {
public:
    void submit(RenderObject object) { objects_.push_back(object); }
    void setCamera(RenderCamera camera) { camera_ = std::move(camera); }
    void submit(RenderLight light) { lights_.push_back(light); }
    void clear() {
        objects_.clear();
        camera_.reset();
        lights_.clear();
    }

    [[nodiscard]] const std::vector<RenderObject>& objects() const { return objects_; }
    [[nodiscard]] const std::optional<RenderCamera>& camera() const { return camera_; }
    [[nodiscard]] const std::vector<RenderLight>& lights() const { return lights_; }

private:
    std::vector<RenderObject> objects_;
    std::optional<RenderCamera> camera_;
    std::vector<RenderLight> lights_;
};

} // namespace engine

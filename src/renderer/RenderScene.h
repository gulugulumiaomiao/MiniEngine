#pragma once

#include "math/Math.h"
#include "renderer/RenderResources.h"

#include <vector>

namespace engine {

struct RenderObject {
    MeshHandle mesh;
    MaterialHandle material;
    math::Mat44 transform{1.0F};
};

class RenderScene final {
public:
    void submit(RenderObject object) { objects_.push_back(object); }
    void clear() { objects_.clear(); }

    [[nodiscard]] const std::vector<RenderObject>& objects() const { return objects_; }

private:
    std::vector<RenderObject> objects_;
};

} // namespace engine

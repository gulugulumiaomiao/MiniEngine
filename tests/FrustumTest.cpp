#include "core/math/Frustum.h"
#include "core/math/Math.h"

#include <cassert>
#include <cmath>

int main() {
    using namespace engine::math;

    // Build a perspective frustum looking down -Z.
    {
        const Mat44 view =
            lookAt(Vec3{0.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, -1.0F}, Vec3{0.0F, 1.0F, 0.0F});
        const Mat44 projection = perspective(radians(60.0F), 16.0F / 9.0F, 1.0F, 100.0F);
        const Frustum frustum = extractFrustum(projection * view);

        assert(intersects(frustum, Vec3{0.0F, 0.0F, -5.0F}, 0.0F));
        assert(intersects(frustum, Vec3{0.0F, 0.0F, -50.0F}, 0.0F));

        assert(!intersects(frustum, Vec3{0.0F, 0.0F, 0.5F}, 0.0F));
        assert(!intersects(frustum, Vec3{20.0F, 0.0F, -5.0F}, 0.0F));
        assert(!intersects(frustum, Vec3{0.0F, 20.0F, -5.0F}, 0.0F));

        // A sphere that straddles the near plane is still considered intersecting.
        assert(intersects(frustum, Vec3{0.0F, 0.0F, -2.0F}, 1.5F));
        // A sphere fully in front of the near plane is rejected.
        assert(!intersects(frustum, Vec3{0.0F, 0.0F, -0.5F}, 0.4F));
    }

    // Use an orthographic projection to exercise the far plane extraction path.
    {
        const Mat44 view =
            lookAt(Vec3{0.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, -1.0F}, Vec3{0.0F, 1.0F, 0.0F});
        const Mat44 projection = orthographic(-10.0F, 10.0F, -10.0F, 10.0F, 0.1F, 100.0F);
        const Frustum frustum = extractFrustum(projection * view);

        assert(intersects(frustum, Vec3{0.0F, 0.0F, -50.0F}, 0.0F));

        assert(!intersects(frustum, Vec3{0.0F, 0.0F, 0.5F}, 0.0F));
        assert(!intersects(frustum, Vec3{0.0F, 0.0F, -150.0F}, 0.0F));
        assert(!intersects(frustum, Vec3{15.0F, 0.0F, -50.0F}, 0.0F));
    }

    // Plane signed distance sanity check.
    {
        const Plane plane{.normal = Vec3{0.0F, 0.0F, 1.0F}, .distance = -5.0F};
        assert(std::abs(plane.signedDistance(Vec3{0.0F, 0.0F, 5.0F}) - 0.0F) < kEpsilon);
        assert(std::abs(plane.signedDistance(Vec3{0.0F, 0.0F, 6.0F}) - 1.0F) < kEpsilon);
    }

    return 0;
}

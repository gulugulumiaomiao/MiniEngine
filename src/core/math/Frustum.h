#pragma once

#include "core/math/Math.h"

#include <array>
#include <cstdint>

namespace engine::math {

// Plane in Hessian normal form: dot(normal, point) + distance = 0.
// The normal points towards the inside of the frustum.
struct Plane {
    Vec3 normal{0.0F, 0.0F, 1.0F};
    float distance{0.0F};

    [[nodiscard]] float signedDistance(const Vec3& point) const {
        return dot(normal, point) + distance;
    }
};

// View frustum composed of six inward-facing planes.
// Plane order: Left, Right, Bottom, Top, Near, Far.
struct Frustum {
    std::array<Plane, 6> planes;

    enum PlaneIndex : std::size_t {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
    };
};

// Extracts a frustum from a view-projection matrix. The matrix should map world
// space to clip space (projection * view). Planes are normalized.
[[nodiscard]] inline Frustum extractFrustum(const Mat44& viewProjection) {
    Frustum frustum;
    const Mat44& m = viewProjection;

    // GLM stores matrices in column-major order, so m[c][r] is column c, row r.
    // For a column-vector clip-space transform clip = M * v, each clip component
    // is the dot product of a matrix row with v:
    //   clip.x = dot(row0, v)  row0 = (m[0][0], m[1][0], m[2][0], m[3][0])
    //   clip.y = dot(row1, v)  row1 = (m[0][1], m[1][1], m[2][1], m[3][1])
    //   clip.z = dot(row2, v)  row2 = (m[0][2], m[1][2], m[2][2], m[3][2])
    //   clip.w = dot(row3, v)  row3 = (m[0][3], m[1][3], m[2][3], m[3][3])
    //
    // Left:   clip.x >= -clip.w  => dot(row0 + row3, v) >= 0
    // Right:  clip.x <=  clip.w  => dot(row3 - row0, v) >= 0
    // Bottom: clip.y >= -clip.w  => dot(row1 + row3, v) >= 0
    // Top:    clip.y <=  clip.w  => dot(row3 - row1, v) >= 0
    // Near:   clip.z >= 0        => dot(row2, v) >= 0 (zero-to-one depth)
    // Far:    clip.z <= clip.w   => dot(row3 - row2, v) >= 0
    //
    // To store a plane in Hessian normal form, both the spatial normal and the
    // distance term must be divided by the length of the raw spatial normal.
    const auto makePlane = [](const Vec3& spatial, float distance) -> Plane {
        const float len = length(spatial);
        if (len < kEpsilon) {
            return Plane{.normal = Vec3{0.0F}, .distance = 0.0F};
        }
        return Plane{
            .normal = spatial / len,
            .distance = distance / len,
        };
    };

    frustum.planes[Frustum::Left] =
        makePlane(Vec3{m[0][0] + m[0][3], m[1][0] + m[1][3], m[2][0] + m[2][3]},
                  m[3][0] + m[3][3]);
    frustum.planes[Frustum::Right] =
        makePlane(Vec3{m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0]},
                  m[3][3] - m[3][0]);
    frustum.planes[Frustum::Bottom] =
        makePlane(Vec3{m[0][1] + m[0][3], m[1][1] + m[1][3], m[2][1] + m[2][3]},
                  m[3][1] + m[3][3]);
    frustum.planes[Frustum::Top] =
        makePlane(Vec3{m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1]},
                  m[3][3] - m[3][1]);
    frustum.planes[Frustum::Near] =
        makePlane(Vec3{m[0][2], m[1][2], m[2][2]}, m[3][2]);
    frustum.planes[Frustum::Far] =
        makePlane(Vec3{m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]},
                  m[3][3] - m[3][2]);

    return frustum;
}

// Sphere-frustum intersection using the conservative "inside any single plane" test.
// A sphere is considered inside if its center is farther than -radius from every plane.
// Planes with a zero normal are ignored (degenerate / unused planes).
[[nodiscard]] inline bool intersects(const Frustum& frustum, const Vec3& center, float radius) {
    for (const Plane& plane : frustum.planes) {
        if (lengthSquared(plane.normal) < kEpsilon * kEpsilon) {
            continue;
        }
        if (plane.signedDistance(center) < -radius) {
            return false;
        }
    }
    return true;
}

} // namespace engine::math

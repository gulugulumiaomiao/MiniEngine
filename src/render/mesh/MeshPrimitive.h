#pragma once

#include "core/math/Math.h"

#include <cstdint>
#include <variant>

namespace engine {

class Transfer;

enum class MeshPrimitiveType : std::uint8_t {
    Plane = 1,
    Box = 2,
    UvSphere = 3,
    Cylinder = 4,
};

enum class MeshIndexPolicy : std::uint8_t { Auto, UInt16, UInt32 };
enum class PrimitiveVertexLayout : std::uint8_t {
    Position,
    PositionNormalUv,
    PositionNormalTangentUv,
};

struct PlaneGeometry {
    math::Vec2 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsZ{1};
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct BoxGeometry {
    math::Vec3 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsY{1};
    std::uint32_t segmentsZ{1};
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct UvSphereGeometry {
    float radius{0.5F};
    std::uint32_t longitudeSegments{32};
    std::uint32_t latitudeSegments{16};
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct CylinderGeometry {
    float bottomRadius{0.5F};
    float topRadius{0.5F};
    float height{1.0F};
    std::uint32_t radialSegments{32};
    std::uint32_t heightSegments{1};
    bool capBottom{true};
    bool capTop{true};
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct MeshPrimitive {
    using Value = std::variant<PlaneGeometry, BoxGeometry, UvSphereGeometry,
                               CylinderGeometry>;

    Value value{PlaneGeometry{}};

    MeshPrimitive() = default;
    MeshPrimitive(PlaneGeometry geometry) : value(geometry) {}
    MeshPrimitive(BoxGeometry geometry) : value(geometry) {}
    MeshPrimitive(UvSphereGeometry geometry) : value(geometry) {}
    MeshPrimitive(CylinderGeometry geometry) : value(geometry) {}

    [[nodiscard]] MeshPrimitiveType type() const;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct MeshPrimitivePart {
    MeshPrimitive primitive;
    math::Vec3 translation{0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F};
    std::uint32_t materialSlot{};

    [[nodiscard]] bool transfer(Transfer& archive);
};

} // namespace engine

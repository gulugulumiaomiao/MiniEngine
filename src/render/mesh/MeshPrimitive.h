#pragma once

#include "core/math/Math.h"
#include "core/serialization/Transferable.h"

#include <cstdint>
#include <utility>
#include <variant>

namespace engine {

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

struct PlaneGeometry final : public Transferable {
    PlaneGeometry() = default;
    PlaneGeometry(math::Vec2 size, std::uint32_t segmentsX, std::uint32_t segmentsZ)
        : size(size), segmentsX(segmentsX), segmentsZ(segmentsZ) {}

    math::Vec2 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsZ{1};
    bool operator==(const PlaneGeometry&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct BoxGeometry final : public Transferable {
    BoxGeometry() = default;
    BoxGeometry(math::Vec3 size,
                std::uint32_t segmentsX,
                std::uint32_t segmentsY,
                std::uint32_t segmentsZ)
        : size(size), segmentsX(segmentsX), segmentsY(segmentsY), segmentsZ(segmentsZ) {}

    math::Vec3 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsY{1};
    std::uint32_t segmentsZ{1};
    bool operator==(const BoxGeometry&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct UvSphereGeometry final : public Transferable {
    UvSphereGeometry() = default;
    UvSphereGeometry(float radius, std::uint32_t longitudeSegments, std::uint32_t latitudeSegments)
        : radius(radius), longitudeSegments(longitudeSegments), latitudeSegments(latitudeSegments) {
    }

    float radius{0.5F};
    std::uint32_t longitudeSegments{32};
    std::uint32_t latitudeSegments{16};
    bool operator==(const UvSphereGeometry&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct CylinderGeometry final : public Transferable {
    CylinderGeometry() = default;
    CylinderGeometry(float bottomRadius,
                     float topRadius,
                     float height,
                     std::uint32_t radialSegments,
                     std::uint32_t heightSegments,
                     bool capBottom,
                     bool capTop)
        : bottomRadius(bottomRadius), topRadius(topRadius), height(height),
          radialSegments(radialSegments), heightSegments(heightSegments), capBottom(capBottom),
          capTop(capTop) {}

    float bottomRadius{0.5F};
    float topRadius{0.5F};
    float height{1.0F};
    std::uint32_t radialSegments{32};
    std::uint32_t heightSegments{1};
    bool capBottom{true};
    bool capTop{true};
    bool operator==(const CylinderGeometry&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshPrimitive final : public Transferable {
    using Value = std::variant<PlaneGeometry, BoxGeometry, UvSphereGeometry, CylinderGeometry>;

    Value value{PlaneGeometry{}};

    MeshPrimitive() = default;
    MeshPrimitive(PlaneGeometry geometry) : value(geometry) {}
    MeshPrimitive(BoxGeometry geometry) : value(geometry) {}
    MeshPrimitive(UvSphereGeometry geometry) : value(geometry) {}
    MeshPrimitive(CylinderGeometry geometry) : value(geometry) {}

    bool operator==(const MeshPrimitive&) const = default;
    [[nodiscard]] MeshPrimitiveType type() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshPrimitivePart final : public Transferable {
    MeshPrimitivePart() = default;
    MeshPrimitivePart(MeshPrimitive primitive,
                      math::Vec3 translation = math::Vec3{0.0F},
                      math::Quat rotation = math::Quat{1.0F, 0.0F, 0.0F, 0.0F},
                      math::Vec3 scale = math::Vec3{1.0F},
                      std::uint32_t materialSlot = 0)
        : primitive(std::move(primitive)), translation(translation), rotation(rotation),
          scale(scale), materialSlot(materialSlot) {}

    MeshPrimitive primitive;
    math::Vec3 translation{0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F};
    std::uint32_t materialSlot{};

    bool operator==(const MeshPrimitivePart&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine

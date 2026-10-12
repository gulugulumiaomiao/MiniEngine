#pragma once
#include "core/math/Math.h"
#include "core/serialization/Transferable.h"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
namespace engine {
enum class MeshPrimitiveTypeAssetData : std::uint8_t {
    Plane = 1,
    Box = 2,
    UvSphere = 3,
    Cylinder = 4,
};

enum class MeshIndexPolicyAssetData : std::uint8_t { Auto, UInt16, UInt32 };
enum class MeshAssetPrimitiveVertexLayout : std::uint8_t {
    Position,
    PositionNormalUv,
    PositionNormalTangentUv,
};

struct MeshAssetPlaneGeometry final : public Transferable {
    MeshAssetPlaneGeometry() = default;
    MeshAssetPlaneGeometry(math::Vec2 size, std::uint32_t segmentsX, std::uint32_t segmentsZ)
        : size(size), segmentsX(segmentsX), segmentsZ(segmentsZ) {}

    math::Vec2 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsZ{1};
    bool operator==(const MeshAssetPlaneGeometry& other) const {
        return size == other.size && segmentsX == other.segmentsX && segmentsZ == other.segmentsZ;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetBoxGeometry final : public Transferable {
    MeshAssetBoxGeometry() = default;
    MeshAssetBoxGeometry(math::Vec3 size,
                         std::uint32_t segmentsX,
                         std::uint32_t segmentsY,
                         std::uint32_t segmentsZ)
        : size(size), segmentsX(segmentsX), segmentsY(segmentsY), segmentsZ(segmentsZ) {}

    math::Vec3 size{1.0F};
    std::uint32_t segmentsX{1};
    std::uint32_t segmentsY{1};
    std::uint32_t segmentsZ{1};
    bool operator==(const MeshAssetBoxGeometry& other) const {
        return size == other.size && segmentsX == other.segmentsX && segmentsY == other.segmentsY &&
               segmentsZ == other.segmentsZ;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetUvSphereGeometry final : public Transferable {
    MeshAssetUvSphereGeometry() = default;
    MeshAssetUvSphereGeometry(float radius,
                              std::uint32_t longitudeSegments,
                              std::uint32_t latitudeSegments)
        : radius(radius), longitudeSegments(longitudeSegments), latitudeSegments(latitudeSegments) {
    }

    float radius{0.5F};
    std::uint32_t longitudeSegments{32};
    std::uint32_t latitudeSegments{16};
    bool operator==(const MeshAssetUvSphereGeometry& other) const {
        return radius == other.radius && longitudeSegments == other.longitudeSegments &&
               latitudeSegments == other.latitudeSegments;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetCylinderGeometry final : public Transferable {
    MeshAssetCylinderGeometry() = default;
    MeshAssetCylinderGeometry(float bottomRadius,
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
    bool operator==(const MeshAssetCylinderGeometry& other) const {
        return bottomRadius == other.bottomRadius && topRadius == other.topRadius &&
               height == other.height && radialSegments == other.radialSegments &&
               heightSegments == other.heightSegments && capBottom == other.capBottom &&
               capTop == other.capTop;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshPrimitiveAssetData final : public Transferable {
    using Value = std::variant<MeshAssetPlaneGeometry,
                               MeshAssetBoxGeometry,
                               MeshAssetUvSphereGeometry,
                               MeshAssetCylinderGeometry>;

    Value value{MeshAssetPlaneGeometry{}};

    MeshPrimitiveAssetData() = default;
    MeshPrimitiveAssetData(MeshAssetPlaneGeometry geometry) : value(geometry) {}
    MeshPrimitiveAssetData(MeshAssetBoxGeometry geometry) : value(geometry) {}
    MeshPrimitiveAssetData(MeshAssetUvSphereGeometry geometry) : value(geometry) {}
    MeshPrimitiveAssetData(MeshAssetCylinderGeometry geometry) : value(geometry) {}

    bool operator==(const MeshPrimitiveAssetData& other) const { return value == other.value; }
    [[nodiscard]] MeshPrimitiveTypeAssetData type() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshPrimitivePartAssetData final : public Transferable {
    MeshPrimitivePartAssetData() = default;
    MeshPrimitivePartAssetData(MeshPrimitiveAssetData primitive,
                               math::Vec3 translation = math::Vec3{0.0F},
                               math::Quat rotation = math::Quat{1.0F, 0.0F, 0.0F, 0.0F},
                               math::Vec3 scale = math::Vec3{1.0F},
                               std::uint32_t materialSlot = 0)
        : primitive(std::move(primitive)), translation(translation), rotation(rotation),
          scale(scale), materialSlot(materialSlot) {}

    MeshPrimitiveAssetData primitive;
    math::Vec3 translation{0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F};
    std::uint32_t materialSlot{};

    bool operator==(const MeshPrimitivePartAssetData& other) const {
        return primitive == other.primitive && translation == other.translation &&
               rotation == other.rotation && scale == other.scale &&
               materialSlot == other.materialSlot;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

enum class MeshAssetVertexSemanticType {
    Position,
    Normal,
    Tangent,
    Color,
    TexCoord,
    JointIndices,
    JointWeights,
    Custom,
};

struct MeshAssetVertexSemantic final : public Transferable {
    MeshAssetVertexSemantic() = default;
    MeshAssetVertexSemantic(MeshAssetVertexSemanticType type, std::uint8_t index)
        : type(type), index(index) {}

    MeshAssetVertexSemanticType type{MeshAssetVertexSemanticType::Position};
    std::uint8_t index{};

    [[nodiscard]] auto operator<=>(const MeshAssetVertexSemantic& other) const {
        if (const auto order = type <=> other.type; order != 0)
            return order;
        return index <=> other.index;
    }
    bool operator==(const MeshAssetVertexSemantic& other) const {
        return type == other.type && index == other.index;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

enum class MeshAssetVertexFormat {
    Float32,
    Vec2Float32,
    Vec3Float32,
    Vec4Float32,
    UInt16x4,
    UInt8x4Normalized,
};

enum class MeshAssetVertexInputRate { Vertex, Instance };
enum class MeshAssetIndexType { UInt16, UInt32 };
enum class MeshAssetUsage { Static, Dynamic, Stream };
enum class MeshAssetTopology { TriangleList, LineList };

struct MeshBuildRecipeAssetData final : public Transferable {
    std::string name;
    std::vector<MeshPrimitivePartAssetData> parts;
    MeshAssetPrimitiveVertexLayout vertexLayout{
        MeshAssetPrimitiveVertexLayout::PositionNormalTangentUv};
    MeshIndexPolicyAssetData indexPolicy{MeshIndexPolicyAssetData::Auto};
    MeshAssetUsage usage{MeshAssetUsage::Static};
    bool keepCpuCopy{};

    bool operator==(const MeshBuildRecipeAssetData& other) const {
        return name == other.name && parts == other.parts && vertexLayout == other.vertexLayout &&
               indexPolicy == other.indexPolicy && usage == other.usage &&
               keepCpuCopy == other.keepCpuCopy;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetVertexStreamLayout final : public Transferable {
    MeshAssetVertexStreamLayout() = default;
    MeshAssetVertexStreamLayout(
        MeshAssetVertexSemantic semantic,
        MeshAssetVertexFormat format,
        std::uint32_t binding,
        std::uint32_t location,
        MeshAssetVertexInputRate inputRate = MeshAssetVertexInputRate::Vertex)
        : semantic(semantic), format(format), binding(binding), location(location),
          inputRate(inputRate) {}

    MeshAssetVertexSemantic semantic;
    MeshAssetVertexFormat format{MeshAssetVertexFormat::Vec3Float32};
    std::uint32_t binding{};
    std::uint32_t location{};
    MeshAssetVertexInputRate inputRate{MeshAssetVertexInputRate::Vertex};

    bool operator==(const MeshAssetVertexStreamLayout& other) const {
        return semantic == other.semantic && format == other.format && binding == other.binding &&
               location == other.location && inputRate == other.inputRate;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetVertexLayout final : public Transferable {
    std::vector<MeshAssetVertexStreamLayout> streams;

    bool operator==(const MeshAssetVertexLayout& other) const { return streams == other.streams; }

    [[nodiscard]] const MeshAssetVertexStreamLayout* find(std::uint32_t binding) const;
    [[nodiscard]] const MeshAssetVertexStreamLayout* find(MeshAssetVertexSemantic semantic) const;
    [[nodiscard]] bool validate() const;
    [[nodiscard]] std::uint64_t hash() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetAabb final : public Transferable {
    MeshAssetAabb() = default;
    MeshAssetAabb(math::Vec3 minimum, math::Vec3 maximum) : minimum(minimum), maximum(maximum) {}

    math::Vec3 minimum{0.0F};
    math::Vec3 maximum{0.0F};

    [[nodiscard]] math::Vec3 center() const { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] math::Vec3 extent() const { return (maximum - minimum) * 0.5F; }

    bool operator==(const MeshAssetAabb& other) const {
        return minimum == other.minimum && maximum == other.maximum;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetBoundingSphere final : public Transferable {
    MeshAssetBoundingSphere() = default;
    MeshAssetBoundingSphere(math::Vec3 center, float radius) : center(center), radius(radius) {}

    math::Vec3 center{0.0F};
    float radius{};

    bool operator==(const MeshAssetBoundingSphere& other) const {
        return center == other.center && radius == other.radius;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetBounds final : public Transferable {
    MeshAssetBounds() = default;
    MeshAssetBounds(MeshAssetAabb aabb, MeshAssetBoundingSphere sphere)
        : aabb(std::move(aabb)), sphere(std::move(sphere)) {}

    MeshAssetAabb aabb;
    MeshAssetBoundingSphere sphere;

    bool operator==(const MeshAssetBounds& other) const {
        return aabb == other.aabb && sphere == other.sphere;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetSubMesh final : public Transferable {
    MeshAssetSubMesh() = default;
    MeshAssetSubMesh(std::uint32_t firstIndex,
                     std::uint32_t indexCount,
                     std::int32_t vertexOffset,
                     std::uint32_t materialSlot,
                     MeshAssetBounds bounds)
        : firstIndex(firstIndex), indexCount(indexCount), vertexOffset(vertexOffset),
          materialSlot(materialSlot), bounds(std::move(bounds)) {}

    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t vertexOffset{};
    std::uint32_t materialSlot{};
    MeshAssetBounds bounds;

    bool operator==(const MeshAssetSubMesh& other) const {
        return firstIndex == other.firstIndex && indexCount == other.indexCount &&
               vertexOffset == other.vertexOffset && materialSlot == other.materialSlot &&
               bounds == other.bounds;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetDesc final : public Transferable {
    std::string debugName;
    MeshAssetVertexLayout vertexLayout;
    MeshAssetIndexType indexType{MeshAssetIndexType::UInt32};
    MeshAssetUsage usage{MeshAssetUsage::Static};
    MeshAssetTopology topology{MeshAssetTopology::TriangleList};
    std::vector<MeshAssetSubMesh> subMeshes;
    MeshAssetBounds bounds;
    bool keepCpuCopy{};

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetVertexStream final : public Transferable {
    std::uint32_t binding{};
    std::uint32_t vertexCount{};
    std::vector<std::byte> bytes;

    bool operator==(const MeshAssetVertexStream& other) const {
        return binding == other.binding && vertexCount == other.vertexCount && bytes == other.bytes;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshAssetBufferData final : public Transferable {
    std::vector<MeshAssetVertexStream> vertexStreams;
    std::vector<std::byte> indices;
    std::uint32_t indexCount{};

    [[nodiscard]] const MeshAssetVertexStream* findVertexStream(std::uint32_t binding) const;
    [[nodiscard]] MeshAssetVertexStream* findVertexStream(std::uint32_t binding);

    bool setVertexData(std::uint32_t binding,
                       std::uint32_t vertexCount,
                       std::span<const std::byte> source);
    bool setIndexData(std::uint32_t count, std::span<const std::byte> source);
    [[nodiscard]] bool transfer(Transfer& archive) override;

    template <typename Vertex, std::size_t Extent>
        requires std::is_trivially_copyable_v<std::remove_cv_t<Vertex>>
    bool setVertexData(std::uint32_t binding, std::span<Vertex, Extent> vertices) {
        return setVertexData(
            binding, static_cast<std::uint32_t>(vertices.size()), std::as_bytes(vertices));
    }

    template <typename Index, std::size_t Extent>
        requires(std::is_same_v<std::remove_cv_t<Index>, std::uint16_t> ||
                 std::is_same_v<std::remove_cv_t<Index>, std::uint32_t>)
    bool setIndexData(std::span<Index, Extent> values) {
        return setIndexData(static_cast<std::uint32_t>(values.size()), std::as_bytes(values));
    }
};

std::uint32_t vertexFormatSize(MeshAssetVertexFormat format);
std::uint32_t indexTypeSize(MeshAssetIndexType type);

struct MeshAssetData final : Transferable {
    MeshAssetDesc desc;
    MeshAssetBufferData meshData;
    std::optional<MeshBuildRecipeAssetData> buildRecipe;
    bool transfer(Transfer& archive) override;
};
} // namespace engine

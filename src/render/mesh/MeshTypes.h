#pragma once
#include "core/math/Math.h"
#include "render/mesh/MeshPrimitive.h"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
namespace engine {
enum class VertexSemanticType {
    Position,
    Normal,
    Tangent,
    Color,
    TexCoord,
    JointIndices,
    JointWeights,
    Custom,
};

struct VertexSemantic final {
    VertexSemantic() = default;
    VertexSemantic(VertexSemanticType type, std::uint8_t index) : type(type), index(index) {}

    VertexSemanticType type{VertexSemanticType::Position};
    std::uint8_t index{};

    [[nodiscard]] auto operator<=>(const VertexSemantic& other) const {
        if (const auto order = type <=> other.type; order != 0)
            return order;
        return index <=> other.index;
    }
    bool operator==(const VertexSemantic& other) const {
        return type == other.type && index == other.index;
    }
};

enum class VertexFormat {
    Float32,
    Vec2Float32,
    Vec3Float32,
    Vec4Float32,
    UInt16x4,
    UInt8x4Normalized,
};

enum class VertexInputRate { Vertex, Instance };
enum class IndexType { UInt16, UInt32 };
enum class MeshUsage { Static, Dynamic, Stream };
enum class MeshTopology { TriangleList, LineList };

struct MeshBuildRecipe final {
    std::string name;
    std::vector<MeshPrimitivePart> parts;
    PrimitiveVertexLayout vertexLayout{PrimitiveVertexLayout::PositionNormalTangentUv};
    MeshIndexPolicy indexPolicy{MeshIndexPolicy::Auto};
    MeshUsage usage{MeshUsage::Static};
    bool keepCpuCopy{};

    bool operator==(const MeshBuildRecipe& other) const {
        return name == other.name && parts == other.parts && vertexLayout == other.vertexLayout &&
               indexPolicy == other.indexPolicy && usage == other.usage &&
               keepCpuCopy == other.keepCpuCopy;
    }
};

struct VertexStreamLayout final {
    VertexStreamLayout() = default;
    VertexStreamLayout(VertexSemantic semantic,
                       VertexFormat format,
                       std::uint32_t binding,
                       std::uint32_t location,
                       VertexInputRate inputRate = VertexInputRate::Vertex)
        : semantic(semantic), format(format), binding(binding), location(location),
          inputRate(inputRate) {}

    VertexSemantic semantic;
    VertexFormat format{VertexFormat::Vec3Float32};
    std::uint32_t binding{};
    std::uint32_t location{};
    VertexInputRate inputRate{VertexInputRate::Vertex};

    bool operator==(const VertexStreamLayout& other) const {
        return semantic == other.semantic && format == other.format && binding == other.binding &&
               location == other.location && inputRate == other.inputRate;
    }
};

struct VertexLayout final {
    std::vector<VertexStreamLayout> streams;

    bool operator==(const VertexLayout& other) const { return streams == other.streams; }

    [[nodiscard]] const VertexStreamLayout* find(std::uint32_t binding) const;
    [[nodiscard]] const VertexStreamLayout* find(VertexSemantic semantic) const;
    [[nodiscard]] bool validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

struct Aabb final {
    Aabb() = default;
    Aabb(math::Vec3 minimum, math::Vec3 maximum) : minimum(minimum), maximum(maximum) {}

    math::Vec3 minimum{0.0F};
    math::Vec3 maximum{0.0F};

    [[nodiscard]] math::Vec3 center() const { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] math::Vec3 extent() const { return (maximum - minimum) * 0.5F; }

    bool operator==(const Aabb& other) const {
        return minimum == other.minimum && maximum == other.maximum;
    }
};

struct BoundingSphere final {
    BoundingSphere() = default;
    BoundingSphere(math::Vec3 center, float radius) : center(center), radius(radius) {}

    math::Vec3 center{0.0F};
    float radius{};

    bool operator==(const BoundingSphere& other) const {
        return center == other.center && radius == other.radius;
    }
};

struct MeshBounds final {
    MeshBounds() = default;
    MeshBounds(Aabb aabb, BoundingSphere sphere)
        : aabb(std::move(aabb)), sphere(std::move(sphere)) {}

    Aabb aabb;
    BoundingSphere sphere;

    bool operator==(const MeshBounds& other) const {
        return aabb == other.aabb && sphere == other.sphere;
    }
};

struct SubMesh final {
    SubMesh() = default;
    SubMesh(std::uint32_t firstIndex,
            std::uint32_t indexCount,
            std::int32_t vertexOffset,
            std::uint32_t materialSlot,
            MeshBounds bounds)
        : firstIndex(firstIndex), indexCount(indexCount), vertexOffset(vertexOffset),
          materialSlot(materialSlot), bounds(std::move(bounds)) {}

    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t vertexOffset{};
    std::uint32_t materialSlot{};
    MeshBounds bounds;

    bool operator==(const SubMesh& other) const {
        return firstIndex == other.firstIndex && indexCount == other.indexCount &&
               vertexOffset == other.vertexOffset && materialSlot == other.materialSlot &&
               bounds == other.bounds;
    }
};

struct VertexStream final {
    std::uint32_t binding{};
    std::uint32_t vertexCount{};
    std::vector<std::byte> bytes;

    bool operator==(const VertexStream& other) const {
        return binding == other.binding && vertexCount == other.vertexCount && bytes == other.bytes;
    }
};

struct MeshData final {
    std::vector<VertexStream> vertexStreams;
    std::vector<std::byte> indices;
    std::uint32_t indexCount{};

    [[nodiscard]] const VertexStream* findVertexStream(std::uint32_t binding) const;
    [[nodiscard]] VertexStream* findVertexStream(std::uint32_t binding);

    bool setVertexData(std::uint32_t binding,
                       std::uint32_t vertexCount,
                       std::span<const std::byte> source);
    bool setIndexData(std::uint32_t count, std::span<const std::byte> source);

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

struct MeshCreateInfo {
    std::string debugName;
    VertexLayout vertexLayout;
    IndexType indexType{IndexType::UInt32};
    MeshUsage usage{MeshUsage::Static};
    MeshTopology topology{MeshTopology::TriangleList};
    std::vector<SubMesh> subMeshes;
    MeshBounds bounds;
    bool keepCpuCopy{};
    MeshData data;
};
std::uint32_t vertexFormatSize(VertexFormat format);
std::uint32_t indexTypeSize(IndexType type);
MeshBounds calculateBounds(std::span<const math::Vec3> positions);
bool validateMesh(const MeshCreateInfo& info);
// Validate replacement bytes against existing metadata without copying the byte payload.
bool validateMesh(const MeshCreateInfo& info, const MeshData& data);
} // namespace engine

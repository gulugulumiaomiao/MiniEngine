#pragma once

#include "math/Math.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine {

enum class VertexSemantic {
    Position,
    Normal,
    Tangent,
    Color0,
    TexCoord0,
    TexCoord1,
    JointIndices,
    JointWeights,
};

enum class VertexFormat {
    Float32,
    Vec2Float32,
    Vec3Float32,
    Vec4Float32,
    UInt16x4,
    UInt8x4Normalized,
};

enum class IndexType { UInt16, UInt32 };
enum class MeshUsage { Static, Dynamic, Stream };
enum class MeshTopology { TriangleList, LineList };

struct VertexAttribute {
    VertexSemantic semantic{VertexSemantic::Position};
    VertexFormat format{VertexFormat::Vec3Float32};
    std::uint32_t offset{};
    std::uint32_t location{};

    bool operator==(const VertexAttribute&) const = default;
};

struct VertexLayout {
    std::uint32_t stride{};
    std::vector<VertexAttribute> attributes;

    bool operator==(const VertexLayout&) const = default;

    [[nodiscard]] const VertexAttribute* find(VertexSemantic semantic) const;
    void validate() const;
};

struct Aabb {
    math::Vec3 minimum{0.0F};
    math::Vec3 maximum{0.0F};

    [[nodiscard]] math::Vec3 center() const { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] math::Vec3 extent() const { return (maximum - minimum) * 0.5F; }
};

struct BoundingSphere {
    math::Vec3 center{0.0F};
    float radius{};
};

struct MeshBounds {
    Aabb aabb;
    BoundingSphere sphere;
};

struct SubMesh {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t vertexOffset{};
    std::uint32_t materialSlot{};
    MeshBounds bounds;
};

struct MeshDesc {
    std::string debugName;
    VertexLayout vertexLayout;
    IndexType indexType{IndexType::UInt32};
    MeshUsage usage{MeshUsage::Static};
    MeshTopology topology{MeshTopology::TriangleList};
    std::vector<SubMesh> subMeshes;
    MeshBounds bounds;
    bool keepCpuCopy{};
};

struct MeshData {
    std::span<const std::byte> vertices;
    std::span<const std::byte> indices;
    std::uint32_t vertexCount{};
    std::uint32_t indexCount{};
};

class MeshAsset final {
public:
    MeshAsset(MeshDesc desc, std::vector<std::byte> vertices,
              std::vector<std::byte> indices);

    [[nodiscard]] const MeshDesc& desc() const { return desc_; }
    [[nodiscard]] MeshData data() const;

private:
    MeshDesc desc_;
    std::vector<std::byte> vertices_;
    std::vector<std::byte> indices_;
};

[[nodiscard]] std::uint32_t vertexFormatSize(VertexFormat format);
[[nodiscard]] std::uint32_t indexTypeSize(IndexType type);
[[nodiscard]] MeshBounds calculateBounds(std::span<const math::Vec3> positions);
void validateMesh(const MeshDesc& desc, const MeshData& data);

} // namespace engine

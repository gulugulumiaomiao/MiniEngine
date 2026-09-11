#pragma once

#include "asset/base/Asset.h"
#include "core/math/Math.h"
#include "core/serialization/Transferable.h"
#include "render/mesh/MeshPrimitive.h"
#include "render/base/RenderHandle.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace engine {

class MeshManager;

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

struct VertexSemantic final : public Transferable {
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
    [[nodiscard]] bool transfer(Transfer& archive) override;
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

struct MeshBuildRecipe final : public Transferable {
    std::string name;
    std::vector<MeshPrimitivePart> parts;
    PrimitiveVertexLayout vertexLayout{PrimitiveVertexLayout::PositionNormalTangentUv};
    MeshIndexPolicy indexPolicy{MeshIndexPolicy::Auto};
    MeshUsage usage{MeshUsage::Static};
    bool keepCpuCopy{};

    bool operator==(const MeshBuildRecipe&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexBinding final : public Transferable {
    VertexBinding() = default;
    VertexBinding(std::uint32_t binding, std::uint32_t stride, VertexInputRate inputRate)
        : binding(binding), stride(stride), inputRate(inputRate) {}

    std::uint32_t binding{};
    std::uint32_t stride{};
    VertexInputRate inputRate{VertexInputRate::Vertex};

    bool operator==(const VertexBinding&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexAttribute final : public Transferable {
    VertexAttribute() = default;
    VertexAttribute(VertexSemantic semantic,
                    VertexFormat format,
                    std::uint32_t location,
                    std::uint32_t binding,
                    std::uint32_t offset)
        : semantic(semantic), format(format), location(location), binding(binding), offset(offset) {
    }

    VertexSemantic semantic;
    VertexFormat format{VertexFormat::Vec3Float32};
    std::uint32_t location{};
    std::uint32_t binding{};
    std::uint32_t offset{};

    bool operator==(const VertexAttribute&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexLayout final : public Transferable {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;

    bool operator==(const VertexLayout&) const = default;

    [[nodiscard]] const VertexBinding* findBinding(std::uint32_t binding) const;
    [[nodiscard]] const VertexAttribute* find(VertexSemantic semantic) const;
    [[nodiscard]] bool validate() const;
    [[nodiscard]] std::uint64_t hash() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct Aabb final : public Transferable {
    Aabb() = default;
    Aabb(math::Vec3 minimum, math::Vec3 maximum) : minimum(minimum), maximum(maximum) {}

    math::Vec3 minimum{0.0F};
    math::Vec3 maximum{0.0F};

    [[nodiscard]] math::Vec3 center() const { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] math::Vec3 extent() const { return (maximum - minimum) * 0.5F; }

    bool operator==(const Aabb&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct BoundingSphere final : public Transferable {
    BoundingSphere() = default;
    BoundingSphere(math::Vec3 center, float radius) : center(center), radius(radius) {}

    math::Vec3 center{0.0F};
    float radius{};

    bool operator==(const BoundingSphere&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshBounds final : public Transferable {
    MeshBounds() = default;
    MeshBounds(Aabb aabb, BoundingSphere sphere)
        : aabb(std::move(aabb)), sphere(std::move(sphere)) {}

    Aabb aabb;
    BoundingSphere sphere;

    bool operator==(const MeshBounds&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct SubMesh final : public Transferable {
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

    bool operator==(const SubMesh&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshDesc final : public Transferable {
    std::string debugName;
    VertexLayout vertexLayout;
    IndexType indexType{IndexType::UInt32};
    MeshUsage usage{MeshUsage::Static};
    MeshTopology topology{MeshTopology::TriangleList};
    std::vector<SubMesh> subMeshes;
    MeshBounds bounds;
    bool keepCpuCopy{};

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexStream final : public Transferable {
    std::uint32_t binding{};
    std::uint32_t vertexCount{};
    std::vector<std::byte> bytes;

    bool operator==(const VertexStream&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshData final : public Transferable {
    std::vector<VertexStream> vertexStreams;
    std::vector<std::byte> indices;
    std::uint32_t indexCount{};

    [[nodiscard]] const VertexStream* findVertexStream(std::uint32_t binding) const;
    [[nodiscard]] VertexStream* findVertexStream(std::uint32_t binding);

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

class Mesh final {
public:
    Mesh() = default;
    Mesh(MeshDesc desc, MeshData data, std::optional<MeshBuildRecipe> buildRecipe = std::nullopt);

    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] const MeshDesc& desc() const { return desc_; }
    [[nodiscard]] const MeshData& data() const { return data_; }
    [[nodiscard]] const std::optional<MeshBuildRecipe>& buildRecipe() const { return buildRecipe_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    // Hash of desc().vertexLayout, computed once because the layout is fixed for the
    // lifetime of the Mesh; data updates never reshape it. Pipeline cache keys use this
    // instead of walking the bindings and attributes on every draw item.
    [[nodiscard]] std::uint64_t vertexLayoutHash() const { return vertexLayoutHash_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

    [[nodiscard]] bool updateVertexData(std::uint32_t binding,
                                        std::uint32_t firstVertex,
                                        std::span<const std::byte> source);
    [[nodiscard]] bool updateIndexData(std::uint32_t firstIndex, std::span<const std::byte> source);

private:
    friend class MeshAsset;
    friend class MeshManager;

    void markChanged();
    void cacheVertexLayoutHash() { vertexLayoutHash_ = desc_.vertexLayout.hash(); }

    VirtualPath assetPath_;
    MeshDesc desc_;
    MeshData data_;
    std::optional<MeshBuildRecipe> buildRecipe_;
    std::uint64_t vertexLayoutHash_{};
    std::uint64_t version_{1};
    bool dirty_{true};
};

class MeshAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Mesh; }

    MeshDesc desc;
    MeshData meshData;
    std::optional<MeshBuildRecipe> buildRecipe;

    [[nodiscard]] Mesh instantiate() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

[[nodiscard]] std::uint32_t vertexFormatSize(VertexFormat format);
[[nodiscard]] std::uint32_t indexTypeSize(IndexType type);
[[nodiscard]] MeshBounds calculateBounds(std::span<const math::Vec3> positions);
[[nodiscard]] bool validateMesh(const MeshDesc& desc, const MeshData& data);

} // namespace engine

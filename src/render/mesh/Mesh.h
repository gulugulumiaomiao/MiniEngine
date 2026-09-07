#pragma once

#include "asset/base/Asset.h"
#include "core/base/InstanceManager.h"
#include "core/base/Singleton.h"
#include "core/math/Math.h"
#include "render/mesh/MeshPrimitive.h"
#include "render/renderer/RenderResources.h"

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

struct VertexSemantic {
    VertexSemanticType type{VertexSemanticType::Position};
    std::uint8_t index{};

    auto operator<=>(const VertexSemantic&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
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

struct MeshBuildRecipe {
    std::string name;
    std::vector<MeshPrimitivePart> parts;
    PrimitiveVertexLayout vertexLayout{PrimitiveVertexLayout::PositionNormalTangentUv};
    MeshIndexPolicy indexPolicy{MeshIndexPolicy::Auto};
    MeshUsage usage{MeshUsage::Static};
    bool keepCpuCopy{};

    [[nodiscard]] bool transfer(Transfer& archive);
};

struct VertexBinding {
    std::uint32_t binding{};
    std::uint32_t stride{};
    VertexInputRate inputRate{VertexInputRate::Vertex};

    bool operator==(const VertexBinding&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct VertexAttribute {
    VertexSemantic semantic;
    VertexFormat format{VertexFormat::Vec3Float32};
    std::uint32_t location{};
    std::uint32_t binding{};
    std::uint32_t offset{};

    bool operator==(const VertexAttribute&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct VertexLayout {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;

    bool operator==(const VertexLayout&) const = default;

    [[nodiscard]] const VertexBinding* findBinding(std::uint32_t binding) const;
    [[nodiscard]] const VertexAttribute* find(VertexSemantic semantic) const;
    [[nodiscard]] bool validate() const;
    [[nodiscard]] std::uint64_t hash() const;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct Aabb {
    math::Vec3 minimum{0.0F};
    math::Vec3 maximum{0.0F};

    [[nodiscard]] math::Vec3 center() const { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] math::Vec3 extent() const { return (maximum - minimum) * 0.5F; }

    bool operator==(const Aabb&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct BoundingSphere {
    math::Vec3 center{0.0F};
    float radius{};

    bool operator==(const BoundingSphere&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct MeshBounds {
    Aabb aabb;
    BoundingSphere sphere;

    bool operator==(const MeshBounds&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct SubMesh {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t vertexOffset{};
    std::uint32_t materialSlot{};
    MeshBounds bounds;

    bool operator==(const SubMesh&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
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

    [[nodiscard]] bool transfer(Transfer& archive);
};

struct VertexStream {
    std::uint32_t binding{};
    std::uint32_t vertexCount{};
    std::vector<std::byte> bytes;

    bool operator==(const VertexStream&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive);
};

struct MeshData {
    std::vector<VertexStream> vertexStreams;
    std::vector<std::byte> indices;
    std::uint32_t indexCount{};

    [[nodiscard]] const VertexStream* findVertexStream(std::uint32_t binding) const;
    [[nodiscard]] VertexStream* findVertexStream(std::uint32_t binding);

    bool setVertexData(std::uint32_t binding,
                       std::uint32_t vertexCount,
                       std::span<const std::byte> source);
    bool setIndexData(std::uint32_t count, std::span<const std::byte> source);
    [[nodiscard]] bool transfer(Transfer& archive);

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

    VirtualPath assetPath_;
    MeshDesc desc_;
    MeshData data_;
    std::optional<MeshBuildRecipe> buildRecipe_;
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

class MeshManager final : public Singleton<MeshManager>, public InstanceManager<Mesh, MeshHandle> {
public:
    [[nodiscard]] MeshHandle load(const VirtualPath& meshPath) override;
    [[nodiscard]] bool replace(MeshHandle handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] const VirtualPath& pathOf(const Mesh& mesh) const override {
        return mesh.assetPath();
    }
    [[nodiscard]] bool validate(const Mesh& mesh) const override;
};

[[nodiscard]] std::uint32_t vertexFormatSize(VertexFormat format);
[[nodiscard]] std::uint32_t indexTypeSize(IndexType type);
[[nodiscard]] MeshBounds calculateBounds(std::span<const math::Vec3> positions);
[[nodiscard]] bool validateMesh(const MeshDesc& desc, const MeshData& data);

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())

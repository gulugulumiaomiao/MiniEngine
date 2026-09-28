#pragma once

#include "asset/base/Asset.h"
#include "asset/base/AssetId.h"
#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/math/Math.h"
#include "core/serialization/Transferable.h"
#include "render/mesh/MeshPrimitive.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID / rhi::IndexFormat

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine {

class MeshAsset;

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

    bool operator==(const MeshBuildRecipe& other) const {
        return name == other.name && parts == other.parts && vertexLayout == other.vertexLayout &&
               indexPolicy == other.indexPolicy && usage == other.usage &&
               keepCpuCopy == other.keepCpuCopy;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexStreamLayout final : public Transferable {
    VertexStreamLayout() = default;
    VertexStreamLayout(VertexSemantic semantic,
                       VertexFormat format,
                       std::uint32_t binding,
                       std::uint32_t location,
                       VertexInputRate inputRate = VertexInputRate::Vertex)
        : semantic(semantic), format(format), binding(binding), location(location), inputRate(inputRate) {}

    VertexSemantic semantic;
    VertexFormat format{VertexFormat::Vec3Float32};
    std::uint32_t binding{};
    std::uint32_t location{};
    VertexInputRate inputRate{VertexInputRate::Vertex};

    bool operator==(const VertexStreamLayout& other) const {
        return semantic == other.semantic && format == other.format && binding == other.binding &&
               location == other.location && inputRate == other.inputRate;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct VertexLayout final : public Transferable {
    std::vector<VertexStreamLayout> streams;

    bool operator==(const VertexLayout& other) const { return streams == other.streams; }

    [[nodiscard]] const VertexStreamLayout* find(std::uint32_t binding) const;
    [[nodiscard]] const VertexStreamLayout* find(VertexSemantic semantic) const;
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

    bool operator==(const Aabb& other) const {
        return minimum == other.minimum && maximum == other.maximum;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct BoundingSphere final : public Transferable {
    BoundingSphere() = default;
    BoundingSphere(math::Vec3 center, float radius) : center(center), radius(radius) {}

    math::Vec3 center{0.0F};
    float radius{};

    bool operator==(const BoundingSphere& other) const {
        return center == other.center && radius == other.radius;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct MeshBounds final : public Transferable {
    MeshBounds() = default;
    MeshBounds(Aabb aabb, BoundingSphere sphere)
        : aabb(std::move(aabb)), sphere(std::move(sphere)) {}

    Aabb aabb;
    BoundingSphere sphere;

    bool operator==(const MeshBounds& other) const {
        return aabb == other.aabb && sphere == other.sphere;
    }
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

    bool operator==(const SubMesh& other) const {
        return firstIndex == other.firstIndex && indexCount == other.indexCount &&
               vertexOffset == other.vertexOffset && materialSlot == other.materialSlot &&
               bounds == other.bounds;
    }
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

    bool operator==(const VertexStream& other) const {
        return binding == other.binding && vertexCount == other.vertexCount && bytes == other.bytes;
    }
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

/// 一个顶点缓冲槽：每个 binding 对应一个层3 buffer RID。
struct MeshVertexBuffer {
    std::uint32_t binding{};
    rhi::RID buffer;

    [[nodiscard]] bool operator==(const MeshVertexBuffer&) const = default;
};

/// 层2 运行时网格：GPU 资源持有者（对齐 Texture）。持有 vertex/index buffer 的层3 RID 与
/// 绘制必要属性（vertexLayout/indexFormat/topology/subMeshes/bounds/usage），不持有 MeshDesc/
/// MeshData/buildRecipe/version/assetPath/assetId/device。
///
/// 三步创建：构造按 desc+data 分配每个 buffer 的 RID（步1）与 GPU 内存（步2），`upload` 上传
/// 数据（步3）。两步销毁：`~Mesh` 对每个 buffer 先 free memory 再 release rid。
/// MeshUsage 决定 GPU 策略：Static→DeviceLocal 持久缓冲（staging 拷贝上传）；Dynamic→host-visible
/// 持久缓冲（就地 memcpy 更新）；Stream→host-visible 瞬态缓冲（每次 upload 经
/// `IDevice::buffer_acquire_transient` 获取新缓冲并 orphan 旧的，由设备按帧 fence 自动回收，
/// `~Mesh` 不释放）。Stream 语义要求每帧重新 upload；引擎尚无自动每帧生产者，属前瞻基础设施。
/// 由 `MeshAsset::instantiate()`/`clone()` 创建；析构经 `rhi::IDevice::active()` 释放（asset-backed
/// 网格在其设备仍 active 时销毁）。
class Mesh final : public RefCounted {
public:
    ~Mesh() override;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    // 资产身份委托给所链接的 MeshAsset（.cpp 定义，因此处 MeshAsset 尚不完整）；
    // 内建/克隆网格无 asset，返回空路径 / 无效 id。
    [[nodiscard]] const VirtualPath& assetPath() const;
    [[nodiscard]] AssetId assetId() const;
    [[nodiscard]] bool isAssetBacked() const { return asset_ != nullptr; }

    // 绘制必要属性 + GPU 句柄（供渲染侧直接读取，取代已删除的 MeshStorage）。
    [[nodiscard]] const std::vector<MeshVertexBuffer>& vertexBuffers() const { return vertexBuffers_; }
    [[nodiscard]] rhi::RID indexBuffer() const { return indexBuffer_; }
    [[nodiscard]] rhi::IndexFormat indexFormat() const { return indexFormat_; }
    [[nodiscard]] MeshTopology topology() const { return topology_; }
    [[nodiscard]] const std::vector<SubMesh>& subMeshes() const { return subMeshes_; }
    [[nodiscard]] const VertexLayout& vertexLayout() const { return vertexLayout_; }
    [[nodiscard]] const MeshBounds& bounds() const { return bounds_; }
    [[nodiscard]] MeshUsage usage() const { return usage_; }
    /// 构造是否成功（设备可用且 buffer RID 已分配）。
    [[nodiscard]] bool isValid() const { return indexBuffer_ && !vertexBuffers_.empty(); }

    /// 步3：把 CPU 数据上传到 GPU 缓冲（按 binding 匹配 vertex stream）。Static/Dynamic 写入
    /// 构造期分配的持久缓冲；Stream 每次获取新瞬态缓冲并 orphan 旧的（见类注释）。
    void upload(const MeshData& data);

    /// 克隆一个脱离 asset 的独立网格（与 `MeshAsset::clone` 同义，需 asset-backed）。
    [[nodiscard]] Ref<Mesh> clone() const;

private:
    friend class MeshAsset;

    /// 步1+2：按 desc/data 分配 vertex/index buffer 的 RID 与 GPU 内存（不上传）。
    /// Static/Dynamic 在此分配持久缓冲；Stream 推迟到 upload（瞬态获取），构造仅记录属性。
    Mesh(const MeshDesc& desc, const MeshData& data);
    /// 建立与 asset 的活链接（仅 instantiate 实例调用）。asset 为非拥有裸指针。
    void bindAsset(MeshAsset* asset) { asset_ = asset; }
    /// 释放 GPU 缓冲：Static/Dynamic 两步销毁（free memory 后 release rid）；Stream 的瞬态
    /// 缓冲由设备 fence 池拥有，仅弃置引用不释放。
    void releaseGpuResources();

    std::vector<MeshVertexBuffer> vertexBuffers_;
    rhi::RID indexBuffer_;
    rhi::IndexFormat indexFormat_{rhi::IndexFormat::UInt32};
    MeshTopology topology_{MeshTopology::TriangleList};
    std::vector<SubMesh> subMeshes_;
    VertexLayout vertexLayout_;
    MeshBounds bounds_;
    MeshUsage usage_{MeshUsage::Static};
    bool constructed_{}; // 构造是否拿到 active device（且持久分配成功）；供 MeshAsset 校验

    MeshAsset* asset_{}; // 非拥有活链接（asset 由 AssetManager 常驻）；仅 instantiate 实例设置
};

/// 层1 网格资产：序列化源（desc + meshData + buildRecipe）。可 instantiate 出唯一运行时 Mesh，
/// 或 clone 出多个脱离实例。
class MeshAsset final : public Asset {
public:
    ~MeshAsset() override;
    [[nodiscard]] AssetType type() const override { return AssetType::Mesh; }

    MeshDesc desc;
    MeshData meshData;
    std::optional<MeshBuildRecipe> buildRecipe;

    [[nodiscard]] bool transfer(Transfer& archive) override;

    /// 唯一运行时实例：首次创建并登记，之后复用同一实例（asset 就地重传会推送到它）。
    [[nodiscard]] Ref<Mesh> instantiate();
    /// 克隆一个脱离 asset 的独立运行时网格（每次新建，不随 asset 变化）。
    [[nodiscard]] Ref<Mesh> clone();

private:
    friend class Mesh;

    /// 把当前 meshData 重新上传给唯一实例（存在时）。就地重传（热重载）后调用。
    void syncInstance();

    Mesh* instance_{}; // 裸观察者指针；~Mesh 反注册置空
};

/// 解析网格引用为运行时 Mesh：`assets://` 或裸路径 → 经 AssetManager 加载 `MeshAsset` 并
/// `instantiate`（按 asset 天然去重）；失败 → 空 Ref（调用方跳过）。取代已删除的
/// `MeshResourceManager::load`。
[[nodiscard]] Ref<Mesh> resolveMeshReference(std::string_view reference);

[[nodiscard]] std::uint32_t vertexFormatSize(VertexFormat format);
[[nodiscard]] std::uint32_t indexTypeSize(IndexType type);
[[nodiscard]] MeshBounds calculateBounds(std::span<const math::Vec3> positions);
[[nodiscard]] bool validateMesh(const MeshDesc& desc, const MeshData& data);

} // namespace engine

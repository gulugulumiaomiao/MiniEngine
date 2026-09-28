#include "render/mesh/Mesh.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"
#include "render/mesh/MeshBuilder.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace {

class FakeDevice final : public engine::rhi::IDevice {
public:
    struct BufferRecord {
        engine::rhi::BufferDesc desc;
        std::vector<std::byte> bytes;
        bool alive{true};
    };

    engine::rhi::RID buffer_allocate_rid(const engine::rhi::BufferDesc& desc) override {
        buffers.push_back({desc, std::vector<std::byte>(desc.size), true});
        ++createdBuffers;
        return {static_cast<std::uint32_t>(buffers.size() - 1), 1};
    }

    void buffer_allocate_memory(engine::rhi::RID) override {}

    void buffer_free_memory(engine::rhi::RID) override {}

    void buffer_release_rid(engine::rhi::RID handle) override {
        if (handle.index() >= buffers.size() || !buffers[handle.index()].alive)
            return;
        buffers[handle.index()].alive = false;
        ++destroyedBuffers;
    }

    engine::rhi::RID buffer_create(const engine::rhi::BufferDesc& desc) override {
        const engine::rhi::RID handle = buffer_allocate_rid(desc);
        buffer_allocate_memory(handle);
        return handle;
    }

    void buffer_destroy(engine::rhi::RID handle) override {
        buffer_free_memory(handle);
        buffer_release_rid(handle);
    }

    void buffer_upload(engine::rhi::RID destination,
                       std::span<const std::byte> data,
                       std::uint64_t offset) override {
        if (destination.index() >= buffers.size())
            return;
        BufferRecord& target = buffers[destination.index()];
        std::ranges::copy(data, target.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        ++uploads;
    }

    engine::rhi::RID buffer_acquire_transient(const engine::rhi::BufferDesc& desc) override {
        ++transientBuffers;
        return buffer_create(desc);
    }

    engine::rhi::RID texture_create(const engine::rhi::TextureDesc&) override {
        return {};
    }
    void texture_destroy(engine::rhi::RID) override {}
    void texture_upload(engine::rhi::RID,
                        std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::RID
    texture_view_create(engine::rhi::RID, const engine::rhi::TextureViewDesc&) override {
        return {};
    }
    engine::rhi::RID texture_default_view(engine::rhi::RID) override {
        return {};
    }
    void texture_view_destroy(engine::rhi::RID) override {}
    engine::rhi::RID sampler_create(const engine::rhi::SamplerDesc&) override {
        return {};
    }
    void sampler_destroy(engine::rhi::RID) override {}

    engine::rhi::RID shader_create(const engine::rhi::ShaderDesc&) override { return {}; }
    void shader_destroy(engine::rhi::RID) override {}
    engine::rhi::RID
    pipeline_create(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void pipeline_destroy(engine::rhi::RID) override {}
    engine::rhi::RID
    bind_group_layout_create(const engine::rhi::BindGroupLayoutDesc&) override {
        return {};
    }
    void bind_group_layout_destroy(engine::rhi::RID) override {}
    engine::rhi::RID bind_group_create(const engine::rhi::BindGroupDesc&) override {
        return {};
    }
    void bind_group_destroy(engine::rhi::RID) override {}
    void waitIdle() override { ++waits; }

    std::vector<BufferRecord> buffers;
    std::uint32_t createdBuffers{};
    std::uint32_t destroyedBuffers{};
    std::uint32_t uploads{};
    std::uint32_t transientBuffers{};
    std::uint32_t waits{};
};

template <typename Value> Value readAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    Value value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(Value));
    return value;
}

} // namespace

int main() {
    using namespace engine;

    // Mesh 现为 GPU 资源持有者：构造/上传/销毁均经 active device。FakeDevice 注册为进程级
    // active 设备并记录 buffer 的分配/上传/释放，使 headless 测试能验证三步创建与两步销毁。
    // 必须先于任何 Mesh 声明，保证其生命周期长于所有 Mesh。
    FakeDevice fakeDevice;

    constexpr std::array positions{
        math::Vec3{-1.0F, -1.0F, 0.0F},
        math::Vec3{1.0F, -1.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
    };
    constexpr std::array colors{
        math::Vec4{1.0F, 0.0F, 0.0F, 1.0F},
        math::Vec4{0.0F, 1.0F, 0.0F, 1.0F},
        math::Vec4{0.0F, 0.0F, 1.0F, 1.0F},
    };
    constexpr std::array<std::uint16_t, 3> indices{0, 1, 2};

    MeshAsset source;
    source.setAssetPath(VirtualPath{"assets://meshes/test.mesh"});
    source.desc.debugName = "MultiStreamTriangle";
    source.desc.vertexLayout.streams = {
        {{VertexSemanticType::Position, 0}, VertexFormat::Vec3Float32, 0, 0},
        {{VertexSemanticType::Color, 0}, VertexFormat::Vec4Float32, 1, 1},
    };
    source.desc.indexType = IndexType::UInt16;
    source.desc.usage = MeshUsage::Dynamic;
    source.desc.bounds = calculateBounds(positions);
    source.desc.subMeshes.push_back({0, 3, 0, 0, source.desc.bounds});
    if (!source.meshData.setVertexData(0, std::span{positions}) ||
        !source.meshData.setVertexData(1, std::span{colors}) ||
        !source.meshData.setIndexData(std::span{indices}) ||
        !validateMesh(source.desc, source.meshData)) {
        return 1;
    }

    // --- 序列化（binary + json）：与 GPU 无关，验证 MeshAsset 持久化 ---
    BinaryWriter writer;
    if (!source.transfer(writer))
        return 2;
    const std::vector<std::byte> binary = writer.takeBytes();
    if (binary.empty())
        return 2;

    MeshAsset decoded;
    decoded.setAssetPath(source.assetPath());
    Asset& asset = decoded;
    BinaryReader reader{binary};
    if (!asset.transfer(reader) || !reader.finished() || decoded.type() != AssetType::Mesh ||
        decoded.desc.debugName != source.desc.debugName ||
        decoded.desc.vertexLayout != source.desc.vertexLayout ||
        decoded.meshData.vertexStreams != source.meshData.vertexStreams ||
        decoded.meshData.indices != source.meshData.indices ||
        decoded.meshData.indexCount != source.meshData.indexCount) {
        return 3;
    }

    JsonWriter jsonWriter;
    if (!source.transfer(jsonWriter))
        return 10;
    MeshAsset jsonDecoded;
    jsonDecoded.setAssetPath(source.assetPath());
    JsonReader jsonReader{jsonWriter.toString()};
    if (!jsonDecoded.transfer(jsonReader) || jsonDecoded.desc.debugName != source.desc.debugName ||
        jsonDecoded.desc.vertexLayout != source.desc.vertexLayout ||
        jsonDecoded.meshData.vertexStreams != source.meshData.vertexStreams ||
        jsonDecoded.meshData.indices != source.meshData.indices) {
        return 10;
    }

    // --- instantiate：唯一运行时实例，三步创建（分配RID + 分配内存 + 上传）---
    // decoded 有 2 个 vertex stream + 1 个 index buffer = 3 个 buffer，各上传一次。
    const std::uint32_t buffersBefore = fakeDevice.createdBuffers;
    const std::uint32_t uploadsBefore = fakeDevice.uploads;
    Ref<Mesh> runtime = decoded.instantiate();
    if (!runtime || !runtime->isValid() || runtime->assetPath() != source.assetPath() ||
        !runtime->isAssetBacked() || runtime->vertexBuffers().size() != 2 ||
        !runtime->indexBuffer() || runtime->indexFormat() != rhi::IndexFormat::UInt16 ||
        runtime->subMeshes().size() != 1 || runtime->usage() != MeshUsage::Dynamic ||
        runtime->topology() != MeshTopology::TriangleList) {
        return 4;
    }
    if (fakeDevice.createdBuffers != buffersBefore + 3 ||
        fakeDevice.uploads != uploadsBefore + 3) {
        return 4;
    }
    // 复用唯一实例：再次 instantiate 返回同一对象，不再新建 buffer。
    if (decoded.instantiate() != runtime || fakeDevice.createdBuffers != buffersBefore + 3) {
        return 5;
    }

    // --- 修改监控：就地重传（热重载）→ transfer 读取分支调 syncInstance 重新上传唯一实例 ---
    const std::uint32_t uploadsBeforeSync = fakeDevice.uploads;
    BinaryReader syncReader{binary};
    if (!decoded.transfer(syncReader) || fakeDevice.uploads != uploadsBeforeSync + 3) {
        return 6;
    }

    // --- clone：脱离 asset 的独立实例（新建 buffer，不登记、不链接 asset）---
    const std::uint32_t buffersBeforeClone = fakeDevice.createdBuffers;
    Ref<Mesh> cloned = runtime->clone();
    if (!cloned || cloned == runtime || !cloned->isValid() || cloned->isAssetBacked() ||
        cloned->assetPath().valid() || fakeDevice.createdBuffers != buffersBeforeClone + 3) {
        return 7;
    }

    // --- 无效 payload 不得破坏 asset，也不得触发上传 ---
    std::vector<std::byte> invalid = binary;
    invalid.front() = std::byte{0};
    const std::string originalName = decoded.desc.debugName;
    const std::uint32_t uploadsBeforeInvalid = fakeDevice.uploads;
    BinaryReader invalidReader{invalid};
    if (decoded.transfer(invalidReader) || decoded.desc.debugName != originalName ||
        fakeDevice.uploads != uploadsBeforeInvalid) {
        return 8;
    }

    // --- 两步销毁：~Mesh 释放 GPU 内存并回收 RID（克隆网格的 3 个 buffer）---
    const std::uint32_t destroyedBefore = fakeDevice.destroyedBuffers;
    cloned.reset();
    if (fakeDevice.destroyedBuffers != destroyedBefore + 3) {
        return 9;
    }

    // --- MeshBuilder::build：几何生成（与 GPU 无关）---
    MeshBuildRecipe planeRecipe;
    planeRecipe.name = "Plane";
    planeRecipe.parts.push_back({PlaneGeometry{{2.0F, 4.0F}, 1, 1}});
    const auto plane = MeshBuilder::build(planeRecipe);
    if (!plane || plane->data.vertexStreams[0].vertexCount != 4 || plane->data.indexCount != 6 ||
        plane->desc.indexType != IndexType::UInt16 ||
        !math::nearlyEqual(plane->desc.bounds.aabb.minimum.x, -1.0F) ||
        !math::nearlyEqual(plane->desc.bounds.aabb.maximum.z, 2.0F)) {
        return 11;
    }

    // 运行时图元经 buildAsset → instantiate（取代已删除的 MeshResourceManager::createRuntime）。
    const Ref<MeshAsset> planeAsset = MeshBuilder::buildAsset(planeRecipe);
    Ref<Mesh> runtimePlane = planeAsset ? planeAsset->instantiate() : Ref<Mesh>{};
    if (!runtimePlane || !runtimePlane->isValid()) {
        return 20;
    }

    // --- 多部件图元组装 + 序列化 + instantiate ---
    MeshBuildRecipe primitives;
    primitives.name = "Primitive Assembly";
    MeshPrimitivePart boxPart;
    boxPart.primitive = BoxGeometry{};
    boxPart.translation = {-2.0F, 0.0F, 0.0F};
    boxPart.materialSlot = 2;
    MeshPrimitivePart spherePart;
    spherePart.primitive = UvSphereGeometry{0.5F, 8, 4};
    spherePart.materialSlot = 3;
    MeshPrimitivePart cylinderPart;
    cylinderPart.primitive = CylinderGeometry{0.5F, 0.25F, 1.0F, 8, 1, true, true};
    cylinderPart.translation = {2.0F, 0.0F, 0.0F};
    cylinderPart.materialSlot = 4;
    primitives.parts = {boxPart, spherePart, cylinderPart};
    auto combined = MeshBuilder::buildAsset(primitives);
    if (!combined || !combined->buildRecipe || combined->desc.subMeshes.size() != 3 ||
        combined->desc.subMeshes[0].materialSlot != 2 ||
        combined->desc.subMeshes[1].indexCount != 144 ||
        combined->desc.subMeshes[2].indexCount != 96 ||
        combined->meshData.vertexStreams[0].vertexCount != 24 + 45 + 38 ||
        combined->desc.bounds.aabb.minimum.x > -2.49F ||
        combined->desc.bounds.aabb.maximum.x < 2.49F) {
        return 12;
    }

    BinaryWriter proceduralWriter;
    if (!combined->transfer(proceduralWriter))
        return 13;
    MeshAsset proceduralDecoded;
    BinaryReader proceduralReader{proceduralWriter.bytes()};
    if (!proceduralDecoded.transfer(proceduralReader) || !proceduralReader.finished() ||
        !proceduralDecoded.buildRecipe || proceduralDecoded.buildRecipe->parts.size() != 3 ||
        proceduralDecoded.buildRecipe->parts[1].primitive.type() != MeshPrimitiveType::UvSphere) {
        return 13;
    }
    // instantiate 后 Mesh 暴露 subMeshes（取代已删除的 Mesh::buildRecipe()）。
    Ref<Mesh> proceduralRuntime = proceduralDecoded.instantiate();
    if (!proceduralRuntime || !proceduralRuntime->isValid() ||
        proceduralRuntime->subMeshes().size() != 3) {
        return 14;
    }

    // --- 镜像切线 handedness ---
    MeshBuildRecipe mirroredRecipe;
    MeshPrimitivePart mirroredPart;
    mirroredPart.primitive = PlaneGeometry{};
    mirroredPart.scale = {-1.0F, 1.0F, 1.0F};
    mirroredRecipe.parts.push_back(mirroredPart);
    const auto mirrored = MeshBuilder::build(mirroredRecipe);
    if (!mirrored)
        return 15;
    const math::Vec4 mirroredTangent = readAt<math::Vec4>(mirrored->data.vertexStreams[2].bytes, 0);
    if (!math::nearlyEqual(mirroredTangent.w, -1.0F))
        return 15;

    // --- 索引类型策略：Auto 大体量升 UInt32，强制 UInt16 超界则失败 ---
    MeshBuildRecipe largeRecipe;
    largeRecipe.parts.push_back({PlaneGeometry{{1.0F, 1.0F}, 256, 256}});
    const auto large = MeshBuilder::build(largeRecipe);
    if (!large || large->desc.indexType != IndexType::UInt32)
        return 16;
    largeRecipe.indexPolicy = MeshIndexPolicy::UInt16;
    if (MeshBuilder::build(largeRecipe))
        return 16;

    MeshBuildRecipe invalidRecipe;
    invalidRecipe.parts.push_back({UvSphereGeometry{0.0F, 8, 4}});
    if (MeshBuilder::build(invalidRecipe))
        return 17;

    // --- 无 build recipe 的 payload 仍可读 ---
    BinaryWriter v4Writer;
    if (!source.transfer(v4Writer))
        return 18;
    MeshAsset v4Decoded;
    BinaryReader v4Reader{v4Writer.bytes()};
    if (!v4Decoded.transfer(v4Reader) || !v4Reader.finished() || v4Decoded.buildRecipe ||
        v4Decoded.desc.debugName != source.desc.debugName) {
        return 18;
    }

    // --- Stream：瞬态 orphan。每次 upload 获取新瞬态缓冲，旧的不被 Mesh 释放（设备 fence 池拥有）---
    MeshBuildRecipe streamRecipe;
    streamRecipe.name = "Stream";
    streamRecipe.usage = MeshUsage::Stream;
    streamRecipe.parts.push_back({PlaneGeometry{}});
    const Ref<MeshAsset> streamAsset = MeshBuilder::buildAsset(streamRecipe);
    Ref<Mesh> streamMesh = streamAsset ? streamAsset->instantiate() : Ref<Mesh>{};
    if (!streamMesh || !streamMesh->isValid() || streamMesh->usage() != MeshUsage::Stream ||
        fakeDevice.transientBuffers == 0) {
        return 22;
    }
    const std::uint32_t transientBefore = fakeDevice.transientBuffers;
    const std::uint32_t createdBeforeStream = fakeDevice.createdBuffers;
    const std::uint32_t destroyedBeforeStream = fakeDevice.destroyedBuffers;
    // 模拟每帧生产者：再次 upload → 获取新瞬态缓冲（orphan 旧的，Mesh 不释放）。
    streamMesh->upload(streamAsset->meshData);
    if (fakeDevice.transientBuffers == transientBefore ||
        fakeDevice.createdBuffers == createdBeforeStream ||
        fakeDevice.destroyedBuffers != destroyedBeforeStream) {
        return 23;
    }
    // ~Mesh 对 Stream 不释放瞬态缓冲（池拥有）：重置后 destroyedBuffers 不变。
    streamMesh.reset();
    if (fakeDevice.destroyedBuffers != destroyedBeforeStream) {
        return 24;
    }

    return 0;
}

#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"
#include "render/gpu/mesh/MeshGpuCache.h"
#include "render/gpu/mesh/MeshGpuFactory.h"
#include "render/mesh/MeshBuilder.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

    engine::rhi::BufferHandle createBuffer(const engine::rhi::BufferDesc& desc) override {
        buffers.push_back({desc, std::vector<std::byte>(desc.size), true});
        ++createdBuffers;
        return {static_cast<std::uint32_t>(buffers.size() - 1), 1};
    }

    void destroyBuffer(engine::rhi::BufferHandle handle) override {
        if (handle.index >= buffers.size() || !buffers[handle.index].alive)
            return;
        buffers[handle.index].alive = false;
        ++destroyedBuffers;
    }

    void uploadBuffer(engine::rhi::BufferHandle destination,
                      std::span<const std::byte> data,
                      std::uint64_t offset) override {
        if (destination.index >= buffers.size())
            return;
        BufferRecord& target = buffers[destination.index];
        std::ranges::copy(data, target.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        ++uploads;
    }

    engine::rhi::TextureHandle createTexture(const engine::rhi::TextureDesc&) override {
        return {};
    }
    void destroyTexture(engine::rhi::TextureHandle) override {}
    void uploadTexture(engine::rhi::TextureHandle,
                       std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::TextureViewHandle createTextureView(const engine::rhi::TextureViewDesc&) override {
        return {};
    }
    void destroyTextureView(engine::rhi::TextureViewHandle) override {}
    engine::rhi::SamplerHandle createSampler(const engine::rhi::SamplerDesc&) override {
        return {};
    }
    void destroySampler(engine::rhi::SamplerHandle) override {}

    engine::rhi::ShaderHandle createShader(const engine::rhi::ShaderDesc&) override { return {}; }
    void destroyShader(engine::rhi::ShaderHandle) override {}
    engine::rhi::GraphicsPipelineHandle
    createGraphicsPipeline(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void destroyGraphicsPipeline(engine::rhi::GraphicsPipelineHandle) override {}
    engine::rhi::BindGroupLayoutHandle
    createBindGroupLayout(const engine::rhi::BindGroupLayoutDesc&) override {
        return {};
    }
    void destroyBindGroupLayout(engine::rhi::BindGroupLayoutHandle) override {}
    engine::rhi::BindGroupHandle createBindGroup(const engine::rhi::BindGroupDesc&) override {
        return {};
    }
    void destroyBindGroup(engine::rhi::BindGroupHandle) override {}
    VkDevice device() const override { return VK_NULL_HANDLE; }
    VkBuffer resolveBuffer(engine::rhi::BufferHandle) const override { return VK_NULL_HANDLE; }
    VkImage resolveTexture(engine::rhi::TextureHandle) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(engine::rhi::TextureViewHandle) const override {
        return VK_NULL_HANDLE;
    }
    engine::rhi::ResolvedPipeline
    resolvePipeline(engine::rhi::GraphicsPipelineHandle) const override {
        return {};
    }
    VkDescriptorSet resolveBindGroup(engine::rhi::BindGroupHandle) const override {
        return VK_NULL_HANDLE;
    }
    void waitIdle() override { ++waits; }

    std::vector<BufferRecord> buffers;
    std::uint32_t createdBuffers{};
    std::uint32_t destroyedBuffers{};
    std::uint32_t uploads{};
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
    source.setAssetPath(VirtualPath{"asset://meshes/test.mesh"});
    source.desc.debugName = "MultiStreamTriangle";
    source.desc.vertexLayout.bindings = {
        {0, sizeof(math::Vec3), VertexInputRate::Vertex},
        {1, sizeof(math::Vec4), VertexInputRate::Vertex},
    };
    source.desc.vertexLayout.attributes = {
        {{VertexSemanticType::Position, 0}, VertexFormat::Vec3Float32, 0, 0, 0},
        {{VertexSemanticType::Color, 0}, VertexFormat::Vec4Float32, 1, 1, 0},
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

    Mesh runtime = decoded.instantiate();
    if (runtime.assetPath() != source.assetPath() || runtime.version() != 1 || !runtime.dirty() ||
        runtime.data().vertexStreams.size() != 2) {
        return 4;
    }
    runtime.markClean();
    const math::Vec3 replacement{2.0F, 3.0F, 4.0F};
    if (!runtime.updateVertexData(0, 1, std::as_bytes(std::span{&replacement, 1})) ||
        runtime.version() != 2 || !runtime.dirty()) {
        return 5;
    }
    const std::uint16_t replacementIndex = 1;
    if (!runtime.updateIndexData(2, std::as_bytes(std::span{&replacementIndex, 1})) ||
        runtime.version() != 3) {
        return 6;
    }

    std::vector<std::byte> invalid = binary;
    invalid.front() = std::byte{0};
    const std::string originalName = decoded.desc.debugName;
    BinaryReader invalidReader{invalid};
    if (decoded.transfer(invalidReader) || decoded.desc.debugName != originalName) {
        return 7;
    }
    const MeshHandle handle = MESH_MANAGER.insert(decoded.instantiate());
    if (!handle || MESH_MANAGER.find(handle) == nullptr ||
        MESH_MANAGER.find(source.assetPath()) != MESH_MANAGER.find(handle) ||
        MESH_MANAGER.size() != 1) {
        return 8;
    }
    if (!MESH_MANAGER.destroy(handle) || MESH_MANAGER.find(handle) != nullptr ||
        MESH_MANAGER.find(source.assetPath()) != nullptr) {
        return 9;
    }

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

    const MeshHandle runtimePlane = MESH_MANAGER.createRuntime(planeRecipe);
    const MeshHandle secondRuntimePlane = MESH_MANAGER.createRuntime(planeRecipe);
    const Mesh* runtimePlaneMesh = MESH_MANAGER.find(runtimePlane);
    if (!runtimePlane || !secondRuntimePlane || runtimePlane == secondRuntimePlane ||
        !runtimePlaneMesh || runtimePlaneMesh->assetPath().valid() || MESH_MANAGER.size() != 2) {
        return 20;
    }
    const std::uint64_t runtimeVersion = runtimePlaneMesh->version();
    std::vector<MeshHandle> destroyedRuntimeMeshes;
    MESH_MANAGER.setDestroyObserver(
        [&destroyedRuntimeMeshes](MeshHandle handle) { destroyedRuntimeMeshes.push_back(handle); });
    planeRecipe.parts[0].primitive = PlaneGeometry{{4.0F, 4.0F}, 2, 2};
    if (!MESH_MANAGER.rebuildRuntime(runtimePlane, planeRecipe) ||
        MESH_MANAGER.find(runtimePlane)->version() != runtimeVersion + 1 ||
        MESH_MANAGER.find(runtimePlane)->data().indexCount != 24 ||
        !MESH_MANAGER.destroyRuntime(runtimePlane) || MESH_MANAGER.find(runtimePlane) ||
        !MESH_MANAGER.destroyRuntime(secondRuntimePlane) || MESH_MANAGER.size() != 0 ||
        destroyedRuntimeMeshes != std::vector<MeshHandle>{runtimePlane, secondRuntimePlane}) {
        return 21;
    }
    MESH_MANAGER.setDestroyObserver({});

    MeshBuildRecipe primitives;
    primitives.name = "PrimitiveAssembly";
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
    Mesh proceduralRuntime = proceduralDecoded.instantiate();
    if (!proceduralRuntime.buildRecipe() ||
        proceduralRuntime.buildRecipe()->name != "PrimitiveAssembly")
        return 14;

    MeshBuildRecipe mirroredRecipe;
    MeshPrimitivePart mirroredPart;
    mirroredPart.primitive = PlaneGeometry{};
    mirroredPart.scale = {-1.0F, 1.0F, 1.0F};
    mirroredRecipe.parts.push_back(mirroredPart);
    const auto mirrored = MeshBuilder::build(mirroredRecipe);
    if (!mirrored)
        return 15;
    const math::Vec4 mirroredTangent =
        readAt<math::Vec4>(mirrored->data.vertexStreams[0].bytes, 24);
    if (!math::nearlyEqual(mirroredTangent.w, -1.0F))
        return 15;

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

    // A v2 payload has no recipe field and must remain readable.
    BinaryWriter v2Writer;
    std::uint32_t v2Magic = 0x4853454dU;
    std::uint16_t v2Version = 2;
    MeshDesc v2Desc = source.desc;
    MeshData v2Data = source.meshData;
    if (!v2Writer.beginObject({}) || !v2Writer.transfer("magic", v2Magic) ||
        !v2Writer.transfer("version", v2Version) || !v2Writer.transfer("description", v2Desc) ||
        !v2Writer.transfer("mesh_data", v2Data) || !v2Writer.endObject()) {
        return 18;
    }
    MeshAsset v2Decoded;
    BinaryReader v2Reader{v2Writer.bytes()};
    if (!v2Decoded.transfer(v2Reader) || !v2Reader.finished() || v2Decoded.buildRecipe ||
        v2Decoded.desc.debugName != source.desc.debugName) {
        return 18;
    }
    // The render-side cache is testable without Vulkan and uploads only when
    // the Mesh version changes.
    FakeDevice fakeDevice;
    MeshGpuCache gpuCache;
    MeshGpuFactory gpuFactory{fakeDevice};
    Mesh gpuMesh = source.instantiate();
    const MeshHandle gpuHandle{7, 1};
    MeshGpuResource firstResource;
    if (!gpuFactory.create({gpuMesh}, firstResource))
        return 19;
    const MeshGpuCacheKey firstKey = MeshGpuCache::key(gpuHandle, gpuMesh.version());
    (void)gpuCache.put(firstKey, std::move(firstResource));
    gpuMesh.markClean();
    const MeshDrawInfo firstDraw = gpuCache.find(firstKey)->drawInfo;
    if (firstDraw.vertexBuffers.size() != 2 || !firstDraw.indexBuffer ||
        fakeDevice.createdBuffers != 3 || fakeDevice.uploads != 3 || gpuMesh.dirty()) {
        return 19;
    }
    const MeshDrawInfo cachedDraw = gpuCache.find(firstKey)->drawInfo;
    if (cachedDraw.indexBuffer != firstDraw.indexBuffer || fakeDevice.createdBuffers != 3 ||
        fakeDevice.uploads != 3) {
        return 19;
    }
    const math::Vec3 gpuReplacement{5.0F, 6.0F, 7.0F};
    if (!gpuMesh.updateVertexData(0, 0, std::as_bytes(std::span{&gpuReplacement, 1}))) {
        return 19;
    }
    MeshGpuResource rebuiltResource;
    if (!gpuFactory.create({gpuMesh}, rebuiltResource))
        return 19;
    auto oldResources = gpuCache.extractIf([sourceKey = MeshGpuCache::sourceKey(gpuHandle)](
                                               const MeshGpuCacheKey& key, const MeshGpuResource&) {
        return key.source == sourceKey;
    });
    fakeDevice.waitIdle();
    for (auto& [unused, resource] : oldResources) {
        (void)unused;
        gpuFactory.release(resource);
    }
    const MeshGpuCacheKey rebuiltKey = MeshGpuCache::key(gpuHandle, gpuMesh.version());
    (void)gpuCache.put(rebuiltKey, std::move(rebuiltResource));
    gpuMesh.markClean();
    const MeshDrawInfo rebuiltDraw = gpuCache.find(rebuiltKey)->drawInfo;
    if (!rebuiltDraw.indexBuffer || rebuiltDraw.indexBuffer == firstDraw.indexBuffer ||
        fakeDevice.createdBuffers != 6 || fakeDevice.destroyedBuffers != 3 ||
        fakeDevice.uploads != 6 || fakeDevice.waits != 1 || gpuMesh.dirty()) {
        return 19;
    }
    auto removed = gpuCache.extractAll();
    fakeDevice.waitIdle();
    for (auto& [unused, resource] : removed) {
        (void)unused;
        gpuFactory.release(resource);
    }
    if (fakeDevice.destroyedBuffers != 6 || fakeDevice.waits != 2 || gpuCache.size() != 0) {
        return 19;
    }
    return 0;
}

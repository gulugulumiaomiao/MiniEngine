#include "render/mesh/Mesh.h"

#include "asset/manager/AssetManager.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "core/math/hash.h"
#include "core/serialization/Transfer.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kMeshMagic = 0x4853454dU;
constexpr std::uint16_t kMeshVersion = 4;
constexpr std::uint16_t kMinimumMeshVersion = 4;

bool fail(std::string_view message) {
    Log::error("Mesh", "%.*s", static_cast<int>(message.size()), message.data());
    return false;
}

bool valid(VertexSemanticType value) {
    return value >= VertexSemanticType::Position && value <= VertexSemanticType::Custom;
}

bool valid(VertexInputRate value) {
    return value == VertexInputRate::Vertex || value == VertexInputRate::Instance;
}

bool valid(IndexType value) {
    return value == IndexType::UInt16 || value == IndexType::UInt32;
}

bool valid(MeshUsage value) {
    return value >= MeshUsage::Static && value <= MeshUsage::Stream;
}

bool valid(MeshTopology value) {
    return value == MeshTopology::TriangleList || value == MeshTopology::LineList;
}

// MeshUsage 决定 GPU 内存策略：Static 走设备本地显存（上传经 staging 拷贝），
// Dynamic/Stream 走 host-visible 内存（上传直接 memcpy）。
[[nodiscard]] rhi::MemoryUsage toRhiMemoryUsage(MeshUsage usage) {
    return usage == MeshUsage::Static ? rhi::MemoryUsage::DeviceLocal : rhi::MemoryUsage::Upload;
}

} // namespace

bool VertexSemantic::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("type", type) &&
           archive.transfer("index", index) && archive.endObject();
}

bool VertexStreamLayout::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("semantic", semantic) &&
           archive.transfer("format", format) && archive.transfer("binding", binding) &&
           archive.transfer("location", location) && archive.transfer("input_rate", inputRate) &&
           archive.endObject();
}

bool VertexLayout::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("streams", streams) && archive.endObject();
}

bool Aabb::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("minimum", minimum) &&
           archive.transfer("maximum", maximum) && archive.endObject();
}

bool BoundingSphere::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("center", center) &&
           archive.transfer("radius", radius) && archive.endObject();
}

bool MeshBounds::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("aabb", aabb) &&
           archive.transfer("sphere", sphere) && archive.endObject();
}

bool SubMesh::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("first_index", firstIndex) &&
           archive.transfer("index_count", indexCount) &&
           archive.transfer("vertex_offset", vertexOffset) &&
           archive.transfer("material_slot", materialSlot) && archive.transfer("bounds", bounds) &&
           archive.endObject();
}

bool MeshDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("debug_name", debugName) &&
           archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_type", indexType) && archive.transfer("usage", usage) &&
           archive.transfer("topology", topology) && archive.transfer("sub_meshes", subMeshes) &&
           archive.transfer("bounds", bounds) && archive.transfer("keep_cpu_copy", keepCpuCopy) &&
           archive.endObject();
}

bool MeshBuildRecipe::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("parts", parts) && archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_policy", indexPolicy) && archive.transfer("usage", usage) &&
           archive.transfer("keep_cpu_copy", keepCpuCopy) && archive.endObject();
}

bool VertexStream::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("binding", binding) &&
           archive.transfer("vertex_count", vertexCount) && archive.transfer("bytes", bytes) &&
           archive.endObject();
}

bool MeshData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("vertex_streams", vertexStreams) &&
           archive.transfer("indices", indices) && archive.transfer("index_count", indexCount) &&
           archive.endObject();
}

bool transferMeshAsset(Transfer& archive, MeshAsset& value) {
    std::uint32_t magic = kMeshMagic;
    std::uint16_t version = kMeshVersion;
    if (!archive.transfer("magic", magic) || magic != kMeshMagic ||
        !archive.transfer("version", version) || version < kMinimumMeshVersion ||
        version > kMeshVersion) {
        return false;
    }
    if (!archive.transfer("build_recipe", value.buildRecipe)) {
        return false;
    }
    return archive.transfer("description", value.desc) &&
           archive.transfer("mesh_data", value.meshData);
}

std::uint32_t vertexFormatSize(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32: return 4;
    case VertexFormat::Vec2Float32: return 8;
    case VertexFormat::Vec3Float32: return 12;
    case VertexFormat::Vec4Float32: return 16;
    case VertexFormat::UInt16x4: return 8;
    case VertexFormat::UInt8x4Normalized: return 4;
    }
    return 0;
}

std::uint32_t indexTypeSize(IndexType type) {
    switch (type) {
    case IndexType::UInt16: return 2;
    case IndexType::UInt32: return 4;
    }
    return 0;
}

const VertexStreamLayout* VertexLayout::find(std::uint32_t binding) const {
    const auto found = std::ranges::find(streams, binding, &VertexStreamLayout::binding);
    return found == streams.end() ? nullptr : &*found;
}

const VertexStreamLayout* VertexLayout::find(VertexSemantic semantic) const {
    const auto found = std::ranges::find(streams, semantic, &VertexStreamLayout::semantic);
    return found == streams.end() ? nullptr : &*found;
}

bool VertexLayout::validate() const {
    if (streams.empty())
        return fail("VertexLayout has no streams");

    std::set<VertexSemantic> semantics;
    std::set<std::uint32_t> bindingIndices;
    std::set<std::uint32_t> locations;
    for (const VertexStreamLayout& stream : streams) {
        const std::uint32_t formatSize = vertexFormatSize(stream.format);
        if (!valid(stream.semantic.type) || formatSize == 0 || !valid(stream.inputRate)) {
            return fail("VertexLayout contains an invalid stream");
        }
        if (!semantics.insert(stream.semantic).second) {
            return fail("VertexLayout contains a duplicate semantic");
        }
        if (!bindingIndices.insert(stream.binding).second) {
            return fail("VertexLayout contains a duplicate binding");
        }
        if (!locations.insert(stream.location).second) {
            return fail("VertexLayout contains a duplicate location");
        }
    }
    return true;
}

std::uint64_t VertexLayout::hash() const {
    Hash64 result = kFnv1a64OffsetBasis;
    hashAppend(result, static_cast<std::uint32_t>(streams.size()));
    for (const VertexStreamLayout& stream : streams) {
        hashAppend(result, stream.semantic.type);
        hashAppend(result, stream.semantic.index);
        hashAppend(result, stream.format);
        hashAppend(result, stream.binding);
        hashAppend(result, stream.location);
        hashAppend(result, stream.inputRate);
    }
    return result;
}

const VertexStream* MeshData::findVertexStream(std::uint32_t binding) const {
    const auto found = std::ranges::find(vertexStreams, binding, &VertexStream::binding);
    return found == vertexStreams.end() ? nullptr : &*found;
}

VertexStream* MeshData::findVertexStream(std::uint32_t binding) {
    return const_cast<VertexStream*>(std::as_const(*this).findVertexStream(binding));
}

bool MeshData::setVertexData(std::uint32_t binding,
                             std::uint32_t vertexCount,
                             std::span<const std::byte> source) {
    if (vertexCount == 0 || source.empty()) {
        return fail("Vertex stream data must not be empty");
    }
    VertexStream* stream = findVertexStream(binding);
    if (!stream) {
        vertexStreams.push_back({});
        stream = &vertexStreams.back();
        stream->binding = binding;
    }
    stream->vertexCount = vertexCount;
    stream->bytes.assign(source.begin(), source.end());
    return true;
}

bool MeshData::setIndexData(std::uint32_t count, std::span<const std::byte> source) {
    if (count == 0 || source.empty())
        return fail("Index data must not be empty");
    indexCount = count;
    indices.assign(source.begin(), source.end());
    return true;
}

MeshBounds calculateBounds(std::span<const math::Vec3> positions) {
    if (positions.empty())
        return {};

    math::Vec3 minimum{std::numeric_limits<float>::max()};
    math::Vec3 maximum{std::numeric_limits<float>::lowest()};
    for (const math::Vec3& position : positions) {
        minimum = math::min(minimum, position);
        maximum = math::max(maximum, position);
    }

    const math::Vec3 center = (minimum + maximum) * 0.5F;
    float radiusSquared = 0.0F;
    for (const math::Vec3& position : positions) {
        radiusSquared = std::max(radiusSquared, math::lengthSquared(position - center));
    }
    return {{minimum, maximum}, {center, std::sqrt(radiusSquared)}};
}

bool validateMesh(const MeshDesc& desc, const MeshData& data) {
    if (!desc.vertexLayout.validate())
        return false;
    const VertexStreamLayout* position = desc.vertexLayout.find({VertexSemanticType::Position, 0});
    if (!position)
        return fail("VertexLayout requires POSITION0");
    if (position->inputRate != VertexInputRate::Vertex) {
        return fail("POSITION0 must use a per-vertex stream");
    }
    if (!valid(desc.indexType) || !valid(desc.usage) || !valid(desc.topology)) {
        return fail("MeshDesc contains an invalid enum value");
    }
    if (data.vertexStreams.size() != desc.vertexLayout.streams.size()) {
        return fail("MeshData does not provide every vertex stream");
    }

    std::uint32_t vertexCount{};
    for (const VertexStreamLayout& layout : desc.vertexLayout.streams) {
        const VertexStream* stream = data.findVertexStream(layout.binding);
        if (!stream || stream->vertexCount == 0) {
            return fail("MeshData is missing a vertex stream");
        }
        const std::uint32_t formatSize = vertexFormatSize(layout.format);
        const std::size_t expected = static_cast<std::size_t>(formatSize) * stream->vertexCount;
        if (stream->bytes.size() != expected) {
            return fail("Vertex stream byte size does not match its layout");
        }
        if (layout.inputRate == VertexInputRate::Vertex) {
            if (vertexCount != 0 && vertexCount != stream->vertexCount) {
                return fail("Per-vertex streams have different vertex counts");
            }
            vertexCount = stream->vertexCount;
        }
    }
    if (vertexCount == 0)
        return fail("Mesh has no per-vertex data");

    const std::uint32_t indexSize = indexTypeSize(desc.indexType);
    if (data.indexCount == 0 || indexSize == 0 ||
        data.indices.size() != static_cast<std::size_t>(indexSize) * data.indexCount) {
        return fail("Index data does not match IndexType and indexCount");
    }
    if (desc.subMeshes.empty())
        return fail("Mesh has no SubMesh");

    for (const SubMesh& subMesh : desc.subMeshes) {
        if (subMesh.indexCount == 0 || subMesh.firstIndex > data.indexCount ||
            subMesh.indexCount > data.indexCount - subMesh.firstIndex) {
            return fail("SubMesh index range is outside the index buffer");
        }
        for (std::uint32_t offset = 0; offset < subMesh.indexCount; ++offset) {
            const std::size_t index = subMesh.firstIndex + offset;
            std::uint32_t vertexIndex{};
            if (desc.indexType == IndexType::UInt16) {
                std::uint16_t value{};
                std::memcpy(&value, data.indices.data() + index * sizeof(value), sizeof(value));
                vertexIndex = value;
            } else {
                std::memcpy(&vertexIndex,
                            data.indices.data() + index * sizeof(vertexIndex),
                            sizeof(vertexIndex));
            }
            const std::int64_t finalVertex =
                static_cast<std::int64_t>(vertexIndex) + subMesh.vertexOffset;
            if (finalVertex < 0 || finalVertex >= vertexCount) {
                return fail("SubMesh references a vertex outside the mesh");
            }
        }
    }
    return true;
}

// ---- Mesh ----

Mesh::Mesh(const MeshDesc& desc, const MeshData& data)
    : indexFormat_(desc.indexType == IndexType::UInt16 ? rhi::IndexFormat::UInt16
                                                       : rhi::IndexFormat::UInt32),
      topology_(desc.topology), subMeshes_(desc.subMeshes), vertexLayout_(desc.vertexLayout),
      bounds_(desc.bounds), usage_(desc.usage) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device) {
        Log::error("Mesh", "No active device for mesh creation");
        return;
    }
    // Stream：瞬态缓冲在每次 upload 时获取（orphan），构造仅记录属性，不分配持久缓冲。
    if (desc.usage == MeshUsage::Stream) {
        constructed_ = true;
        return;
    }
    const rhi::MemoryUsage memory = toRhiMemoryUsage(desc.usage);

    // 步1：为每个 vertex stream 与 index buffer 分配 RID（仅句柄，暂不分配 GPU 内存）。
    vertexBuffers_.reserve(data.vertexStreams.size());
    for (const VertexStream& stream : data.vertexStreams) {
        const rhi::RID rid = device->buffer_allocate_rid(
            {.size = stream.bytes.size(),
             .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
             .memoryUsage = memory,
             .debugName = desc.debugName + ".vertex." + std::to_string(stream.binding)});
        if (!rid) {
            Log::error("Mesh", "Failed to allocate a vertex buffer RID");
            releaseGpuResources();
            return;
        }
        vertexBuffers_.push_back({stream.binding, rid});
    }
    indexBuffer_ = device->buffer_allocate_rid(
        {.size = data.indices.size(),
         .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
         .memoryUsage = memory,
         .debugName = desc.debugName + ".index"});
    if (!indexBuffer_) {
        Log::error("Mesh", "Failed to allocate the index buffer RID");
        releaseGpuResources();
        return;
    }

    // 步2：为每个已分配句柄分配 GPU 内存。
    for (const MeshVertexBuffer& vertexBuffer : vertexBuffers_)
        device->buffer_allocate_memory(vertexBuffer.buffer);
    device->buffer_allocate_memory(indexBuffer_);
    constructed_ = true;
}

Mesh::~Mesh() {
    // 双向观察者：若本实例是 asset 的唯一实例，先清除 asset 的回指，避免 ~MeshAsset 悬垂。
    if (asset_ && asset_->instance_ == this)
        asset_->instance_ = nullptr;
    releaseGpuResources();
}

void Mesh::releaseGpuResources() {
    // Stream 的瞬态缓冲由设备 fence 池拥有，仅弃置引用；Static/Dynamic 两步销毁（先销毁 GPU
    // 内存，再回收 RID）。设备已亡（active 为空）时仅弃置句柄。
    if (usage_ != MeshUsage::Stream) {
        if (rhi::IDevice* device = rhi::IDevice::active()) {
            for (const MeshVertexBuffer& vertexBuffer : vertexBuffers_) {
                if (vertexBuffer.buffer) {
                    device->buffer_free_memory(vertexBuffer.buffer);
                    device->buffer_release_rid(vertexBuffer.buffer);
                }
            }
            if (indexBuffer_) {
                device->buffer_free_memory(indexBuffer_);
                device->buffer_release_rid(indexBuffer_);
            }
        }
    }
    vertexBuffers_.clear();
    indexBuffer_ = {};
}

void Mesh::upload(const MeshData& data) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device || !constructed_) {
        Log::error("Mesh", "Cannot upload to an unconstructed mesh");
        return;
    }
    if (usage_ == MeshUsage::Stream) {
        // 瞬态 orphan：本帧获取全新 host-visible 缓冲；上一帧的瞬态缓冲由设备 fence 池回收，
        // 故此处不释放旧的（直接替换引用）。要求每帧重新 upload。
        vertexBuffers_.clear();
        vertexBuffers_.reserve(data.vertexStreams.size());
        for (const VertexStream& stream : data.vertexStreams) {
            const rhi::RID rid = device->buffer_acquire_transient(
                {.size = stream.bytes.size(),
                 .usage = rhi::BufferUsage::Vertex,
                 .memoryUsage = rhi::MemoryUsage::Upload,
                 .debugName = "mesh.stream.vertex"});
            if (!rid) {
                Log::error("Mesh", "Failed to acquire a transient vertex buffer");
                return;
            }
            device->buffer_upload(rid, stream.bytes);
            vertexBuffers_.push_back({stream.binding, rid});
        }
        indexBuffer_ = device->buffer_acquire_transient(
            {.size = data.indices.size(),
             .usage = rhi::BufferUsage::Index,
             .memoryUsage = rhi::MemoryUsage::Upload,
             .debugName = "mesh.stream.index"});
        if (!indexBuffer_) {
            Log::error("Mesh", "Failed to acquire the transient index buffer");
            return;
        }
        device->buffer_upload(indexBuffer_, data.indices);
        return;
    }
    // Static/Dynamic：写入构造期分配的持久缓冲（按 binding 匹配 vertex stream）。
    if (!isValid()) {
        Log::error("Mesh", "Cannot upload to an invalid mesh");
        return;
    }
    for (const MeshVertexBuffer& vertexBuffer : vertexBuffers_) {
        const VertexStream* stream = data.findVertexStream(vertexBuffer.binding);
        if (!stream) {
            Log::error("Mesh", "Missing vertex stream for binding %u", vertexBuffer.binding);
            continue;
        }
        device->buffer_upload(vertexBuffer.buffer, stream->bytes);
    }
    device->buffer_upload(indexBuffer_, data.indices);
}

const VirtualPath& Mesh::assetPath() const {
    static const VirtualPath kEmpty;
    return asset_ ? asset_->assetPath() : kEmpty;
}

AssetId Mesh::assetId() const {
    return asset_ ? asset_->assetId() : AssetId{};
}

Ref<Mesh> Mesh::clone() const {
    if (!asset_) {
        Log::error("Mesh", "clone() requires an asset-backed mesh");
        return {};
    }
    return asset_->clone();
}

// ---- MeshAsset ----

MeshAsset::~MeshAsset() {
    // 双向观察者：若 asset 先于其唯一实例销毁，必须清除实例的回指指针，否则 ~Mesh 会
    // 解引用悬垂的 asset_（UAF）。
    if (instance_)
        instance_->asset_ = nullptr;
}

bool MeshAsset::transfer(Transfer& archive) {
    MeshAsset decoded;
    MeshAsset& target = archive.reading() ? decoded : *this;
    if ((archive.writing() && !validateMesh(desc, meshData)) || !archive.beginObject({}) ||
        !transferMeshAsset(archive, target) || !archive.endObject() ||
        (archive.reading() && !validateMesh(decoded.desc, decoded.meshData))) {
        return fail("Invalid MeshAsset payload");
    }
    if (archive.reading()) {
        desc = std::move(decoded.desc);
        meshData = std::move(decoded.meshData);
        buildRecipe = std::move(decoded.buildRecipe);
        // 就地重传（热重载）后把新数据推送给唯一实例，保持活链接。
        syncInstance();
    }
    return true;
}

Ref<Mesh> MeshAsset::instantiate() {
    if (instance_)
        return Ref<Mesh>(instance_); // 复用唯一实例（addRef）
    if (!validateMesh(desc, meshData))
        return {};
    Ref<Mesh> mesh(new Mesh(desc, meshData));
    if (!mesh->constructed_) {
        Log::error(
            "MeshAsset", "Failed to instantiate mesh for %s", assetPath().string().c_str());
        return {};
    }
    mesh->bindAsset(this);
    mesh->upload(meshData);
    if (!mesh->isValid()) {
        Log::error(
            "MeshAsset", "Mesh upload left an invalid mesh for %s", assetPath().string().c_str());
        return {};
    }
    instance_ = mesh.get();
    return mesh;
}

Ref<Mesh> MeshAsset::clone() {
    if (!validateMesh(desc, meshData))
        return {};
    Ref<Mesh> mesh(new Mesh(desc, meshData));
    if (!mesh->constructed_) {
        Log::error("MeshAsset", "Failed to clone mesh for %s", assetPath().string().c_str());
        return {};
    }
    mesh->upload(meshData); // 脱离实例：不登记、不链接 asset
    if (!mesh->isValid()) {
        Log::error(
            "MeshAsset", "Mesh upload left an invalid clone for %s", assetPath().string().c_str());
        return {};
    }
    return mesh;
}

void MeshAsset::syncInstance() {
    if (instance_)
        instance_->upload(meshData);
}

Ref<Mesh> resolveMeshReference(std::string_view reference) {
    if (reference.empty())
        return {};
    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    if (path.valid() && path.scheme() == "assets") {
        if (const Ref<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(path)) {
            if (Ref<Mesh> mesh = asset->instantiate())
                return mesh;
        }
    }
    Log::warn("Mesh",
              "Cannot resolve mesh reference: %.*s",
              static_cast<int>(reference.size()),
              reference.data());
    return {};
}

} // namespace engine

#include "render/mesh/Mesh.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kMeshMagic = 0x4853454dU;
constexpr std::uint16_t kMeshVersion = 3;
constexpr std::uint16_t kMinimumMeshVersion = 2;

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

template <typename Value> void appendHash(std::uint64_t& hash, Value value) {
    const auto bytes = std::as_bytes(std::span{&value, 1});
    for (const std::byte byte : bytes) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
}

} // namespace

bool VertexSemantic::transfer(Transfer& archive) {
    return archive.transfer("type", type) && archive.transfer("index", index);
}

bool VertexBinding::transfer(Transfer& archive) {
    return archive.transfer("binding", binding) && archive.transfer("stride", stride) &&
           archive.transfer("input_rate", inputRate);
}

bool VertexAttribute::transfer(Transfer& archive) {
    return archive.transfer("semantic", semantic) && archive.transfer("format", format) &&
           archive.transfer("location", location) && archive.transfer("binding", binding) &&
           archive.transfer("offset", offset);
}

bool VertexLayout::transfer(Transfer& archive) {
    return archive.transfer("bindings", bindings) && archive.transfer("attributes", attributes);
}

bool Aabb::transfer(Transfer& archive) {
    return archive.transfer("minimum", minimum) && archive.transfer("maximum", maximum);
}

bool BoundingSphere::transfer(Transfer& archive) {
    return archive.transfer("center", center) && archive.transfer("radius", radius);
}

bool MeshBounds::transfer(Transfer& archive) {
    return archive.transfer("aabb", aabb) && archive.transfer("sphere", sphere);
}

bool SubMesh::transfer(Transfer& archive) {
    return archive.transfer("first_index", firstIndex) &&
           archive.transfer("index_count", indexCount) &&
           archive.transfer("vertex_offset", vertexOffset) &&
           archive.transfer("material_slot", materialSlot) && archive.transfer("bounds", bounds);
}

bool MeshDesc::transfer(Transfer& archive) {
    return archive.transfer("debug_name", debugName) &&
           archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_type", indexType) && archive.transfer("usage", usage) &&
           archive.transfer("topology", topology) && archive.transfer("sub_meshes", subMeshes) &&
           archive.transfer("bounds", bounds) && archive.transfer("keep_cpu_copy", keepCpuCopy);
}

bool MeshBuildRecipe::transfer(Transfer& archive) {
    return archive.transfer("name", name) && archive.transfer("parts", parts) &&
           archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_policy", indexPolicy) && archive.transfer("usage", usage) &&
           archive.transfer("keep_cpu_copy", keepCpuCopy);
}

bool VertexStream::transfer(Transfer& archive) {
    return archive.transfer("binding", binding) && archive.transfer("vertex_count", vertexCount) &&
           archive.transfer("bytes", bytes);
}

bool MeshData::transfer(Transfer& archive) {
    return archive.transfer("vertex_streams", vertexStreams) &&
           archive.transfer("indices", indices) && archive.transfer("index_count", indexCount);
}

bool transferMeshAsset(Transfer& archive, MeshAsset& value) {
    std::uint32_t magic = kMeshMagic;
    std::uint16_t version = kMeshVersion;
    if (!archive.transfer("magic", magic) || magic != kMeshMagic ||
        !archive.transfer("version", version) || version < kMinimumMeshVersion ||
        version > kMeshVersion) {
        return false;
    }
    if (version >= 3 && !archive.transfer("build_recipe", value.buildRecipe)) {
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

const VertexBinding* VertexLayout::findBinding(std::uint32_t binding) const {
    const auto found = std::ranges::find(bindings, binding, &VertexBinding::binding);
    return found == bindings.end() ? nullptr : &*found;
}

const VertexAttribute* VertexLayout::find(VertexSemantic semantic) const {
    const auto found = std::ranges::find(attributes, semantic, &VertexAttribute::semantic);
    return found == attributes.end() ? nullptr : &*found;
}

bool VertexLayout::validate() const {
    if (bindings.empty())
        return fail("VertexLayout has no bindings");
    if (attributes.empty())
        return fail("VertexLayout has no attributes");

    std::set<std::uint32_t> bindingIndices;
    for (const VertexBinding& binding : bindings) {
        if (binding.stride == 0 || !valid(binding.inputRate)) {
            return fail("VertexLayout contains an invalid binding");
        }
        if (!bindingIndices.insert(binding.binding).second) {
            return fail("VertexLayout contains a duplicate binding");
        }
    }

    std::set<VertexSemantic> semantics;
    std::set<std::uint32_t> locations;
    for (const VertexAttribute& attribute : attributes) {
        const VertexBinding* binding = findBinding(attribute.binding);
        const std::uint32_t formatSize = vertexFormatSize(attribute.format);
        if (!valid(attribute.semantic.type) || !binding || formatSize == 0) {
            return fail("VertexLayout contains an invalid attribute");
        }
        if (!semantics.insert(attribute.semantic).second) {
            return fail("VertexLayout contains a duplicate semantic");
        }
        if (!locations.insert(attribute.location).second) {
            return fail("VertexLayout contains a duplicate location");
        }
        if (attribute.offset > binding->stride || formatSize > binding->stride - attribute.offset) {
            return fail("Vertex attribute exceeds its binding stride");
        }
    }

    for (std::size_t left = 0; left < attributes.size(); ++left) {
        for (std::size_t right = left + 1; right < attributes.size(); ++right) {
            if (attributes[left].binding != attributes[right].binding)
                continue;
            const std::uint32_t leftBegin = attributes[left].offset;
            const std::uint32_t leftEnd = leftBegin + vertexFormatSize(attributes[left].format);
            const std::uint32_t rightBegin = attributes[right].offset;
            const std::uint32_t rightEnd = rightBegin + vertexFormatSize(attributes[right].format);
            if (leftBegin < rightEnd && rightBegin < leftEnd) {
                return fail("Vertex attributes overlap in one binding");
            }
        }
    }
    return true;
}

std::uint64_t VertexLayout::hash() const {
    std::uint64_t result = 1469598103934665603ULL;
    appendHash(result, static_cast<std::uint32_t>(bindings.size()));
    for (const VertexBinding& binding : bindings) {
        appendHash(result, binding.binding);
        appendHash(result, binding.stride);
        appendHash(result, binding.inputRate);
    }
    appendHash(result, static_cast<std::uint32_t>(attributes.size()));
    for (const VertexAttribute& attribute : attributes) {
        appendHash(result, attribute.semantic.type);
        appendHash(result, attribute.semantic.index);
        appendHash(result, attribute.format);
        appendHash(result, attribute.location);
        appendHash(result, attribute.binding);
        appendHash(result, attribute.offset);
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

Mesh::Mesh(MeshDesc desc, MeshData data, std::optional<MeshBuildRecipe> buildRecipe)
    : desc_(std::move(desc)), data_(std::move(data)), buildRecipe_(std::move(buildRecipe)) {
    if (!validateMesh(desc_, data_)) {
        desc_ = {};
        data_ = {};
    }
}

bool validateMesh(const MeshDesc& desc, const MeshData& data) {
    if (!desc.vertexLayout.validate())
        return false;
    const VertexAttribute* position = desc.vertexLayout.find({VertexSemanticType::Position, 0});
    if (!position)
        return fail("VertexLayout requires POSITION0");
    const VertexBinding* positionBinding = desc.vertexLayout.findBinding(position->binding);
    if (!positionBinding || positionBinding->inputRate != VertexInputRate::Vertex) {
        return fail("POSITION0 must use a per-vertex binding");
    }
    if (!valid(desc.indexType) || !valid(desc.usage) || !valid(desc.topology)) {
        return fail("MeshDesc contains an invalid enum value");
    }
    if (data.vertexStreams.size() != desc.vertexLayout.bindings.size()) {
        return fail("MeshData does not provide every vertex binding");
    }

    std::uint32_t vertexCount{};
    for (const VertexBinding& binding : desc.vertexLayout.bindings) {
        const VertexStream* stream = data.findVertexStream(binding.binding);
        if (!stream || stream->vertexCount == 0) {
            return fail("MeshData is missing a vertex stream");
        }
        const std::size_t expected = static_cast<std::size_t>(binding.stride) * stream->vertexCount;
        if (stream->bytes.size() != expected) {
            return fail("Vertex stream byte size does not match its layout");
        }
        if (binding.inputRate == VertexInputRate::Vertex) {
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

Mesh MeshAsset::instantiate() const {
    Mesh result;
    if (!validateMesh(desc, meshData))
        return result;
    result.assetPath_ = assetPath();
    result.desc_ = desc;
    result.data_ = meshData;
    result.buildRecipe_ = buildRecipe;
    return result;
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
    }
    return true;
}

bool Mesh::updateVertexData(std::uint32_t binding,
                            std::uint32_t firstVertex,
                            std::span<const std::byte> source) {
    if (desc_.usage == MeshUsage::Static) {
        return fail("Cannot update a Static Mesh");
    }
    const VertexBinding* layout = desc_.vertexLayout.findBinding(binding);
    VertexStream* stream = data_.findVertexStream(binding);
    if (!layout || !stream || source.empty() || source.size() % layout->stride != 0) {
        return fail("Invalid vertex update");
    }
    const std::size_t offset = static_cast<std::size_t>(firstVertex) * layout->stride;
    if (offset > stream->bytes.size() || source.size() > stream->bytes.size() - offset) {
        return fail("Vertex update exceeds the stream");
    }
    std::ranges::copy(source, stream->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    markChanged();
    return true;
}

bool Mesh::updateIndexData(std::uint32_t firstIndex, std::span<const std::byte> source) {
    if (desc_.usage == MeshUsage::Static) {
        return fail("Cannot update a Static Mesh");
    }
    const std::uint32_t stride = indexTypeSize(desc_.indexType);
    if (stride == 0 || source.empty() || source.size() % stride != 0) {
        return fail("Invalid index update");
    }
    const std::size_t offset = static_cast<std::size_t>(firstIndex) * stride;
    if (offset > data_.indices.size() || source.size() > data_.indices.size() - offset) {
        return fail("Index update exceeds the index buffer");
    }
    std::ranges::copy(source, data_.indices.begin() + static_cast<std::ptrdiff_t>(offset));
    markChanged();
    return true;
}

void Mesh::markChanged() {
    if (version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::error("Mesh", "Mesh version overflow");
        return;
    }
    ++version_;
    dirty_ = true;
}

MeshHandle MeshManager::load(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return {};
    }
    if (const MeshHandle existing = handleFor(meshPath); existing) {
        return existing;
    }
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    return asset ? insert(asset->instantiate()) : MeshHandle{};
}

bool MeshManager::replace(MeshHandle handle, Mesh mesh) {
    Mesh* current = find(handle);
    if (!current) {
        Log::error("MeshManager", "Cannot replace an invalid MeshHandle");
        return false;
    }
    if (current->assetPath() != mesh.assetPath() || !validate(mesh)) {
        Log::error(
            "MeshManager", "Replacement Mesh is invalid: %s", mesh.assetPath().string().c_str());
        return false;
    }
    if (current->version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::error("MeshManager", "Mesh version overflow");
        return false;
    }
    mesh.version_ = current->version_ + 1;
    mesh.dirty_ = true;
    *current = std::move(mesh);
    return true;
}

bool MeshManager::replace(const VirtualPath& meshPath) {
    const MeshHandle handle = handleFor(meshPath);
    if (!handle)
        return true;
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    return asset && replace(handle, asset->instantiate());
}

bool MeshManager::validate(const Mesh& mesh) const {
    return validateMesh(mesh.desc(), mesh.data());
}

} // namespace engine

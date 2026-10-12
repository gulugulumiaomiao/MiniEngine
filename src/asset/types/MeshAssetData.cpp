#include "asset/types/MeshAssetData.h"
#include "core/serialization/Transfer.h"
#include "core/math/hash.h"
#include <algorithm>
#include <set>
#include <utility>
namespace engine {
namespace {
bool fail(std::string_view) {
    return false;
}
bool valid(MeshAssetVertexSemanticType value) {
    return value >= MeshAssetVertexSemanticType::Position &&
           value <= MeshAssetVertexSemanticType::Custom;
}
bool valid(MeshAssetVertexInputRate value) {
    return value == MeshAssetVertexInputRate::Vertex || value == MeshAssetVertexInputRate::Instance;
}
} // namespace
bool MeshAssetVertexSemantic::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("type", type) &&
           archive.transfer("index", index) && archive.endObject();
}

bool MeshAssetVertexStreamLayout::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("semantic", semantic) &&
           archive.transfer("format", format) && archive.transfer("binding", binding) &&
           archive.transfer("location", location) && archive.transfer("input_rate", inputRate) &&
           archive.endObject();
}

bool MeshAssetVertexLayout::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("streams", streams) && archive.endObject();
}

bool MeshAssetAabb::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("minimum", minimum) &&
           archive.transfer("maximum", maximum) && archive.endObject();
}

bool MeshAssetBoundingSphere::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("center", center) &&
           archive.transfer("radius", radius) && archive.endObject();
}

bool MeshAssetBounds::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("aabb", aabb) &&
           archive.transfer("sphere", sphere) && archive.endObject();
}

bool MeshAssetSubMesh::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("first_index", firstIndex) &&
           archive.transfer("index_count", indexCount) &&
           archive.transfer("vertex_offset", vertexOffset) &&
           archive.transfer("material_slot", materialSlot) && archive.transfer("bounds", bounds) &&
           archive.endObject();
}

bool MeshAssetDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("debug_name", debugName) &&
           archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_type", indexType) && archive.transfer("usage", usage) &&
           archive.transfer("topology", topology) && archive.transfer("sub_meshes", subMeshes) &&
           archive.transfer("bounds", bounds) && archive.transfer("keep_cpu_copy", keepCpuCopy) &&
           archive.endObject();
}

bool MeshBuildRecipeAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("parts", parts) && archive.transfer("vertex_layout", vertexLayout) &&
           archive.transfer("index_policy", indexPolicy) && archive.transfer("usage", usage) &&
           archive.transfer("keep_cpu_copy", keepCpuCopy) && archive.endObject();
}

bool MeshAssetVertexStream::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("binding", binding) &&
           archive.transfer("vertex_count", vertexCount) && archive.transfer("bytes", bytes) &&
           archive.endObject();
}

bool MeshAssetBufferData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("vertex_streams", vertexStreams) &&
           archive.transfer("indices", indices) && archive.transfer("index_count", indexCount) &&
           archive.endObject();
}

bool MeshAssetPlaneGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("size", size) &&
           archive.transfer("segments_x", segmentsX) && archive.transfer("segments_z", segmentsZ) &&
           archive.endObject();
}

bool MeshAssetBoxGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("size", size) &&
           archive.transfer("segments_x", segmentsX) && archive.transfer("segments_y", segmentsY) &&
           archive.transfer("segments_z", segmentsZ) && archive.endObject();
}

bool MeshAssetUvSphereGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("radius", radius) &&
           archive.transfer("longitude_segments", longitudeSegments) &&
           archive.transfer("latitude_segments", latitudeSegments) && archive.endObject();
}

bool MeshAssetCylinderGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("bottom_radius", bottomRadius) &&
           archive.transfer("top_radius", topRadius) && archive.transfer("height", height) &&
           archive.transfer("radial_segments", radialSegments) &&
           archive.transfer("height_segments", heightSegments) &&
           archive.transfer("cap_bottom", capBottom) && archive.transfer("cap_top", capTop) &&
           archive.endObject();
}

MeshPrimitiveTypeAssetData MeshPrimitiveAssetData::type() const {
    return static_cast<MeshPrimitiveTypeAssetData>(value.index() + 1U);
}

bool MeshPrimitiveAssetData::transfer(Transfer& archive) {
    if (!archive.beginObject({}))
        return false;
    MeshPrimitiveTypeAssetData primitiveType = type();
    if (!archive.transfer("primitive_type", primitiveType))
        return false;
    if (archive.reading()) {
        switch (primitiveType) {
        case MeshPrimitiveTypeAssetData::Plane: value.emplace<MeshAssetPlaneGeometry>(); break;
        case MeshPrimitiveTypeAssetData::Box: value.emplace<MeshAssetBoxGeometry>(); break;
        case MeshPrimitiveTypeAssetData::UvSphere:
            value.emplace<MeshAssetUvSphereGeometry>();
            break;
        case MeshPrimitiveTypeAssetData::Cylinder:
            value.emplace<MeshAssetCylinderGeometry>();
            break;
        default: return false;
        }
    }
    return std::visit(
               [&archive](auto& geometry) { return archive.transfer("parameters", geometry); },
               value) &&
           archive.endObject();
}

bool MeshPrimitivePartAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("primitive", primitive) &&
           archive.transfer("translation", translation) && archive.transfer("rotation", rotation) &&
           archive.transfer("scale", scale) && archive.transfer("material_slot", materialSlot) &&
           archive.endObject();
}

std::uint32_t vertexFormatSize(MeshAssetVertexFormat format) {
    switch (format) {
    case MeshAssetVertexFormat::Float32: return 4;
    case MeshAssetVertexFormat::Vec2Float32: return 8;
    case MeshAssetVertexFormat::Vec3Float32: return 12;
    case MeshAssetVertexFormat::Vec4Float32: return 16;
    case MeshAssetVertexFormat::UInt16x4: return 8;
    case MeshAssetVertexFormat::UInt8x4Normalized: return 4;
    }
    return 0;
}

std::uint32_t indexTypeSize(MeshAssetIndexType type) {
    switch (type) {
    case MeshAssetIndexType::UInt16: return 2;
    case MeshAssetIndexType::UInt32: return 4;
    }
    return 0;
}

const MeshAssetVertexStreamLayout* MeshAssetVertexLayout::find(std::uint32_t binding) const {
    const auto found = std::ranges::find(streams, binding, &MeshAssetVertexStreamLayout::binding);
    return found == streams.end() ? nullptr : &*found;
}

const MeshAssetVertexStreamLayout*
MeshAssetVertexLayout::find(MeshAssetVertexSemantic semantic) const {
    const auto found = std::ranges::find(streams, semantic, &MeshAssetVertexStreamLayout::semantic);
    return found == streams.end() ? nullptr : &*found;
}

bool MeshAssetVertexLayout::validate() const {
    if (streams.empty())
        return fail("MeshAssetVertexLayout has no streams");

    std::set<MeshAssetVertexSemantic> semantics;
    std::set<std::uint32_t> bindingIndices;
    std::set<std::uint32_t> locations;
    for (const MeshAssetVertexStreamLayout& stream : streams) {
        const std::uint32_t formatSize = vertexFormatSize(stream.format);
        if (!valid(stream.semantic.type) || formatSize == 0 || !valid(stream.inputRate)) {
            return fail("MeshAssetVertexLayout contains an invalid stream");
        }
        if (!semantics.insert(stream.semantic).second) {
            return fail("MeshAssetVertexLayout contains a duplicate semantic");
        }
        if (!bindingIndices.insert(stream.binding).second) {
            return fail("MeshAssetVertexLayout contains a duplicate binding");
        }
        if (!locations.insert(stream.location).second) {
            return fail("MeshAssetVertexLayout contains a duplicate location");
        }
    }
    return true;
}

std::uint64_t MeshAssetVertexLayout::hash() const {
    Hash64 result = kFnv1a64OffsetBasis;
    hashAppend(result, static_cast<std::uint32_t>(streams.size()));
    for (const MeshAssetVertexStreamLayout& stream : streams) {
        hashAppend(result, stream.semantic.type);
        hashAppend(result, stream.semantic.index);
        hashAppend(result, stream.format);
        hashAppend(result, stream.binding);
        hashAppend(result, stream.location);
        hashAppend(result, stream.inputRate);
    }
    return result;
}

const MeshAssetVertexStream* MeshAssetBufferData::findVertexStream(std::uint32_t binding) const {
    const auto found = std::ranges::find(vertexStreams, binding, &MeshAssetVertexStream::binding);
    return found == vertexStreams.end() ? nullptr : &*found;
}

MeshAssetVertexStream* MeshAssetBufferData::findVertexStream(std::uint32_t binding) {
    return const_cast<MeshAssetVertexStream*>(std::as_const(*this).findVertexStream(binding));
}

bool MeshAssetBufferData::setVertexData(std::uint32_t binding,
                                        std::uint32_t vertexCount,
                                        std::span<const std::byte> source) {
    if (vertexCount == 0 || source.empty()) {
        return fail("Vertex stream data must not be empty");
    }
    MeshAssetVertexStream* stream = findVertexStream(binding);
    if (!stream) {
        vertexStreams.push_back({});
        stream = &vertexStreams.back();
        stream->binding = binding;
    }
    stream->vertexCount = vertexCount;
    stream->bytes.assign(source.begin(), source.end());
    return true;
}

bool MeshAssetBufferData::setIndexData(std::uint32_t count, std::span<const std::byte> source) {
    if (count == 0 || source.empty())
        return fail("Index data must not be empty");
    indexCount = count;
    indices.assign(source.begin(), source.end());
    return true;
}

bool MeshAssetData::transfer(Transfer& archive) {
    MeshAssetData decoded;
    auto& target = archive.reading() ? decoded : *this;
    std::uint32_t magic = 0x4853454dU;
    std::uint16_t version = 4;
    if (!archive.beginObject({}) || !archive.transfer("magic", magic) || magic != 0x4853454dU ||
        !archive.transfer("version", version) || version != 4 ||
        !archive.transfer("build_recipe", target.buildRecipe) ||
        !archive.transfer("description", target.desc) ||
        !archive.transfer("mesh_data", target.meshData) || !archive.endObject())
        return false;
    if (archive.reading())
        *this = std::move(decoded);
    return true;
}
} // namespace engine

#include "render/mesh/MeshTypes.h"
#include "core/logging/Log.h"
#include "core/math/hash.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
namespace engine {
namespace {
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

} // namespace
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

bool validateMesh(const MeshCreateInfo& info, const MeshData& data) {
    if (!info.vertexLayout.validate())
        return false;
    const VertexStreamLayout* position = info.vertexLayout.find({VertexSemanticType::Position, 0});
    if (!position)
        return fail("VertexLayout requires POSITION0");
    if (position->inputRate != VertexInputRate::Vertex) {
        return fail("POSITION0 must use a per-vertex stream");
    }
    if (!valid(info.indexType) || !valid(info.usage) || !valid(info.topology)) {
        return fail("MeshCreateInfo contains an invalid enum value");
    }
    if (data.vertexStreams.size() != info.vertexLayout.streams.size()) {
        return fail("MeshData does not provide every vertex stream");
    }

    std::uint32_t vertexCount{};
    for (const VertexStreamLayout& layout : info.vertexLayout.streams) {
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

    const std::uint32_t indexSize = indexTypeSize(info.indexType);
    if (data.indexCount == 0 || indexSize == 0 ||
        data.indices.size() != static_cast<std::size_t>(indexSize) * data.indexCount) {
        return fail("Index data does not match IndexType and indexCount");
    }
    if (info.subMeshes.empty())
        return fail("Mesh has no SubMesh");

    for (const SubMesh& subMesh : info.subMeshes) {
        if (subMesh.indexCount == 0 || subMesh.firstIndex > data.indexCount ||
            subMesh.indexCount > data.indexCount - subMesh.firstIndex) {
            return fail("SubMesh index range is outside the index buffer");
        }
        for (std::uint32_t offset = 0; offset < subMesh.indexCount; ++offset) {
            const std::size_t index = subMesh.firstIndex + offset;
            std::uint32_t vertexIndex{};
            if (info.indexType == IndexType::UInt16) {
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

bool validateMesh(const MeshCreateInfo& info) {
    return validateMesh(info, info.data);
}

} // namespace engine

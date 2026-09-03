#include "renderer/Mesh.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace engine {

std::uint32_t vertexFormatSize(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32: return 4;
    case VertexFormat::Vec2Float32: return 8;
    case VertexFormat::Vec3Float32: return 12;
    case VertexFormat::Vec4Float32: return 16;
    case VertexFormat::UInt16x4: return 8;
    case VertexFormat::UInt8x4Normalized: return 4;
    }
    Log::fatal("Mesh", "Unknown VertexFormat");
}

std::uint32_t indexTypeSize(IndexType type) {
    return type == IndexType::UInt16 ? 2U : 4U;
}

const VertexAttribute* VertexLayout::find(VertexSemantic semantic) const {
    const auto found = std::ranges::find_if(attributes, [semantic](const VertexAttribute& attribute) {
        return attribute.semantic == semantic;
    });
    return found == attributes.end() ? nullptr : &*found;
}

void VertexLayout::validate() const {
    if (stride == 0) {
        Log::fatal("VertexLayout", "Stride must be greater than zero");
    }
    if (attributes.empty()) {
        Log::fatal("VertexLayout", "At least one attribute is required");
    }

    std::set<VertexSemantic> semantics;
    std::set<std::uint32_t> locations;
    for (const VertexAttribute& attribute : attributes) {
        if (!semantics.insert(attribute.semantic).second) {
            Log::fatal("VertexLayout", "Contains a duplicate semantic");
        }
        if (!locations.insert(attribute.location).second) {
            Log::fatal("VertexLayout", "Contains a duplicate location");
        }
        const std::uint32_t formatSize = vertexFormatSize(attribute.format);
        if (attribute.offset > stride || formatSize > stride - attribute.offset) {
            Log::fatal("VertexLayout", "Vertex attribute exceeds stride");
        }
    }

    for (std::size_t left = 0; left < attributes.size(); ++left) {
        const std::uint32_t leftBegin = attributes[left].offset;
        const std::uint32_t leftEnd = leftBegin + vertexFormatSize(attributes[left].format);
        for (std::size_t right = left + 1; right < attributes.size(); ++right) {
            const std::uint32_t rightBegin = attributes[right].offset;
            const std::uint32_t rightEnd = rightBegin + vertexFormatSize(attributes[right].format);
            if (leftBegin < rightEnd && rightBegin < leftEnd) {
                Log::fatal("VertexLayout", "Attributes overlap");
            }
        }
    }
}

MeshBounds calculateBounds(std::span<const math::Vec3> positions) {
    if (positions.empty()) {
        return {};
    }

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

void validateMesh(const MeshDesc& desc, const MeshData& data) {
    desc.vertexLayout.validate();
    if (!desc.vertexLayout.find(VertexSemantic::Position)) {
        Log::fatal("Mesh", "VertexLayout requires a Position attribute");
    }
    if (data.vertexCount == 0) {
        Log::fatal("Mesh", "At least one vertex is required");
    }
    const std::size_t expectedVertexBytes =
        static_cast<std::size_t>(desc.vertexLayout.stride) * data.vertexCount;
    if (data.vertices.size_bytes() != expectedVertexBytes) {
        Log::fatal("Mesh", "Vertex byte size does not match stride and vertexCount");
    }
    const std::size_t expectedIndexBytes =
        static_cast<std::size_t>(indexTypeSize(desc.indexType)) * data.indexCount;
    if (data.indices.size_bytes() != expectedIndexBytes) {
        Log::fatal("Mesh", "Index byte size does not match IndexType and indexCount");
    }
    if (data.indexCount == 0) {
        Log::fatal("Mesh", "At least one index is required");
    }
    if (desc.subMeshes.empty()) {
        Log::fatal("Mesh", "At least one SubMesh is required");
    }
    for (const SubMesh& subMesh : desc.subMeshes) {
        if (subMesh.indexCount == 0 || subMesh.firstIndex > data.indexCount ||
            subMesh.indexCount > data.indexCount - subMesh.firstIndex) {
            Log::fatal("Mesh", "SubMesh index range is outside the index buffer");
        }
        for (std::uint32_t indexOffset = 0; indexOffset < subMesh.indexCount; ++indexOffset) {
            const std::size_t index = subMesh.firstIndex + indexOffset;
            std::uint32_t vertexIndex = 0;
            if (desc.indexType == IndexType::UInt16) {
                std::uint16_t value = 0;
                std::memcpy(&value, data.indices.data() + index * sizeof(value), sizeof(value));
                vertexIndex = value;
            } else {
                std::memcpy(&vertexIndex,
                            data.indices.data() + index * sizeof(vertexIndex),
                            sizeof(vertexIndex));
            }
            const std::int64_t finalVertex =
                static_cast<std::int64_t>(vertexIndex) + subMesh.vertexOffset;
            if (finalVertex < 0 || finalVertex >= data.vertexCount) {
                Log::fatal("Mesh", "SubMesh references a vertex outside the vertex buffer");
            }
        }
    }
}

MeshAsset::MeshAsset(MeshDesc desc, std::vector<std::byte> vertices,
                     std::vector<std::byte> indices)
    : desc_(std::move(desc)), vertices_(std::move(vertices)), indices_(std::move(indices)) {
    const MeshData meshData = data();
    validateMesh(desc_, meshData);
}

MeshData MeshAsset::data() const {
    const std::uint32_t vertexCount = desc_.vertexLayout.stride == 0
        ? 0U
        : static_cast<std::uint32_t>(vertices_.size() / desc_.vertexLayout.stride);
    const std::uint32_t indexCount = static_cast<std::uint32_t>(
        indices_.size() / indexTypeSize(desc_.indexType));
    return {vertices_, indices_, vertexCount, indexCount};
}

} // namespace engine

#include "render/mesh/MeshBuilder.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kMaxSegments = 512;
constexpr std::size_t kMaxGeneratedVertices = 4U * 1024U * 1024U;

struct GeneratedVertex {
    math::Vec3 position{};
    math::Vec3 normal{0.0F, 1.0F, 0.0F};
    math::Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
    math::Vec2 uv{};
};

struct GeneratedMesh {
    std::vector<GeneratedVertex> vertices;
    std::vector<std::uint32_t> indices;
};

bool finite(float value) {
    return std::isfinite(value);
}
bool finite(const math::Vec2& value) {
    return finite(value.x) && finite(value.y);
}
bool finite(const math::Vec3& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}
bool finite(const math::Quat& value) {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}
bool validSegments(std::uint32_t value, std::uint32_t minimum = 1) {
    return value >= minimum && value <= kMaxSegments;
}

void orientClockwise(GeneratedMesh& mesh) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const GeneratedVertex& a = mesh.vertices[mesh.indices[i]];
        const GeneratedVertex& b = mesh.vertices[mesh.indices[i + 1]];
        const GeneratedVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const math::Vec3 face = math::cross(b.position - a.position, c.position - a.position);
        const math::Vec3 normal = a.normal + b.normal + c.normal;
        if (math::dot(face, normal) > 0.0F) {
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
}

GeneratedMesh makePlane(const PlaneGeometry& geometry) {
    GeneratedMesh mesh;
    const std::uint32_t width = geometry.segmentsX + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(width) * (geometry.segmentsZ + 1));
    for (std::uint32_t z = 0; z <= geometry.segmentsZ; ++z) {
        const float v = static_cast<float>(z) / geometry.segmentsZ;
        for (std::uint32_t x = 0; x <= geometry.segmentsX; ++x) {
            const float u = static_cast<float>(x) / geometry.segmentsX;
            mesh.vertices.push_back(
                {{(u - 0.5F) * geometry.size.x, 0.0F, (v - 0.5F) * geometry.size.y},
                 {0.0F, 1.0F, 0.0F},
                 {1.0F, 0.0F, 0.0F, 1.0F},
                 {u, v}});
        }
    }
    for (std::uint32_t z = 0; z < geometry.segmentsZ; ++z) {
        for (std::uint32_t x = 0; x < geometry.segmentsX; ++x) {
            const std::uint32_t a = z * width + x;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + width;
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    orientClockwise(mesh);
    return mesh;
}

void appendFace(GeneratedMesh& mesh,
                const math::Vec3& center,
                const math::Vec3& uAxis,
                const math::Vec3& vAxis,
                const math::Vec3& normal,
                std::uint32_t uSegments,
                std::uint32_t vSegments) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    const std::uint32_t width = uSegments + 1;
    const math::Vec3 tangent = math::normalize(uAxis, math::Vec3{1, 0, 0});
    for (std::uint32_t y = 0; y <= vSegments; ++y) {
        const float v = static_cast<float>(y) / vSegments;
        for (std::uint32_t x = 0; x <= uSegments; ++x) {
            const float u = static_cast<float>(x) / uSegments;
            mesh.vertices.push_back({center + (u - 0.5F) * uAxis + (v - 0.5F) * vAxis,
                                     normal,
                                     {tangent, 1.0F},
                                     {u, v}});
        }
    }
    for (std::uint32_t y = 0; y < vSegments; ++y) {
        for (std::uint32_t x = 0; x < uSegments; ++x) {
            const std::uint32_t a = base + y * width + x;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + width;
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
}

GeneratedMesh makeBox(const BoxGeometry& geometry) {
    GeneratedMesh mesh;
    const math::Vec3 half = geometry.size * 0.5F;
    appendFace(mesh,
               {half.x, 0, 0},
               {0, 0, -geometry.size.z},
               {0, geometry.size.y, 0},
               {1, 0, 0},
               geometry.segmentsZ,
               geometry.segmentsY);
    appendFace(mesh,
               {-half.x, 0, 0},
               {0, 0, geometry.size.z},
               {0, geometry.size.y, 0},
               {-1, 0, 0},
               geometry.segmentsZ,
               geometry.segmentsY);
    appendFace(mesh,
               {0, half.y, 0},
               {geometry.size.x, 0, 0},
               {0, 0, geometry.size.z},
               {0, 1, 0},
               geometry.segmentsX,
               geometry.segmentsZ);
    appendFace(mesh,
               {0, -half.y, 0},
               {geometry.size.x, 0, 0},
               {0, 0, -geometry.size.z},
               {0, -1, 0},
               geometry.segmentsX,
               geometry.segmentsZ);
    appendFace(mesh,
               {0, 0, half.z},
               {geometry.size.x, 0, 0},
               {0, geometry.size.y, 0},
               {0, 0, 1},
               geometry.segmentsX,
               geometry.segmentsY);
    appendFace(mesh,
               {0, 0, -half.z},
               {-geometry.size.x, 0, 0},
               {0, geometry.size.y, 0},
               {0, 0, -1},
               geometry.segmentsX,
               geometry.segmentsY);
    orientClockwise(mesh);
    return mesh;
}

GeneratedMesh makeSphere(const UvSphereGeometry& geometry) {
    GeneratedMesh mesh;
    const std::uint32_t width = geometry.longitudeSegments + 1;
    for (std::uint32_t latitude = 0; latitude <= geometry.latitudeSegments; ++latitude) {
        const float v = static_cast<float>(latitude) / geometry.latitudeSegments;
        const float theta = v * math::kPi;
        const float ring = std::sin(theta);
        const float y = std::cos(theta);
        for (std::uint32_t longitude = 0; longitude <= geometry.longitudeSegments; ++longitude) {
            const float u = static_cast<float>(longitude) / geometry.longitudeSegments;
            const float phi = u * math::kPi * 2.0F;
            const math::Vec3 normal{ring * std::cos(phi), y, ring * std::sin(phi)};
            const math::Vec3 tangent = math::normalize(
                math::Vec3{-std::sin(phi), 0.0F, std::cos(phi)}, math::Vec3{1, 0, 0});
            mesh.vertices.push_back({normal * geometry.radius, normal, {tangent, 1.0F}, {u, v}});
        }
    }
    for (std::uint32_t latitude = 0; latitude < geometry.latitudeSegments; ++latitude) {
        for (std::uint32_t longitude = 0; longitude < geometry.longitudeSegments; ++longitude) {
            const std::uint32_t a = latitude * width + longitude;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + width;
            const std::uint32_t d = c + 1;
            if (latitude != 0)
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            if (latitude + 1 != geometry.latitudeSegments)
                mesh.indices.insert(mesh.indices.end(), {b, d, c});
        }
    }
    orientClockwise(mesh);
    return mesh;
}

void appendCap(GeneratedMesh& mesh, float radius, float y, std::uint32_t segments, bool top) {
    if (radius <= math::kEpsilon)
        return;
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    const math::Vec3 normal = top ? math::Vec3{0, 1, 0} : math::Vec3{0, -1, 0};
    mesh.vertices.push_back({{0, y, 0}, normal, {1, 0, 0, 1}, {0.5F, 0.5F}});
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / segments;
        const float angle = u * math::kPi * 2.0F;
        const float x = std::cos(angle);
        const float z = std::sin(angle);
        mesh.vertices.push_back({{x * radius, y, z * radius},
                                 normal,
                                 {1, 0, 0, 1},
                                 {x * 0.5F + 0.5F, z * 0.5F + 0.5F}});
    }
    for (std::uint32_t i = 0; i < segments; ++i) {
        mesh.indices.insert(mesh.indices.end(), {base, base + i + 1, base + i + 2});
    }
}

GeneratedMesh makeCylinder(const CylinderGeometry& geometry) {
    GeneratedMesh mesh;
    const std::uint32_t width = geometry.radialSegments + 1;
    const float halfHeight = geometry.height * 0.5F;
    const float slope = (geometry.bottomRadius - geometry.topRadius) / geometry.height;
    for (std::uint32_t height = 0; height <= geometry.heightSegments; ++height) {
        const float v = static_cast<float>(height) / geometry.heightSegments;
        const float y = -halfHeight + v * geometry.height;
        const float radius = math::lerp(geometry.bottomRadius, geometry.topRadius, v);
        for (std::uint32_t radial = 0; radial <= geometry.radialSegments; ++radial) {
            const float u = static_cast<float>(radial) / geometry.radialSegments;
            const float angle = u * math::kPi * 2.0F;
            const float x = std::cos(angle);
            const float z = std::sin(angle);
            const math::Vec3 normal = math::normalize(math::Vec3{x, slope, z});
            const math::Vec3 tangent{-z, 0, x};
            mesh.vertices.push_back({{x * radius, y, z * radius}, normal, {tangent, 1.0F}, {u, v}});
        }
    }
    for (std::uint32_t height = 0; height < geometry.heightSegments; ++height) {
        for (std::uint32_t radial = 0; radial < geometry.radialSegments; ++radial) {
            const std::uint32_t a = height * width + radial;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + width;
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    if (geometry.capBottom)
        appendCap(mesh, geometry.bottomRadius, -halfHeight, geometry.radialSegments, false);
    if (geometry.capTop)
        appendCap(mesh, geometry.topRadius, halfHeight, geometry.radialSegments, true);
    orientClockwise(mesh);
    return mesh;
}

std::optional<GeneratedMesh> generate(const MeshPrimitive& primitive) {
    return std::visit(
        [](const auto& geometry) -> std::optional<GeneratedMesh> {
            using Geometry = std::decay_t<decltype(geometry)>;
            if constexpr (std::is_same_v<Geometry, PlaneGeometry>) {
                if (!finite(geometry.size) || geometry.size.x <= 0 || geometry.size.y <= 0 ||
                    !validSegments(geometry.segmentsX) || !validSegments(geometry.segmentsZ))
                    return std::nullopt;
                return makePlane(geometry);
            } else if constexpr (std::is_same_v<Geometry, BoxGeometry>) {
                if (!finite(geometry.size) || geometry.size.x <= 0 || geometry.size.y <= 0 ||
                    geometry.size.z <= 0 || !validSegments(geometry.segmentsX) ||
                    !validSegments(geometry.segmentsY) || !validSegments(geometry.segmentsZ))
                    return std::nullopt;
                return makeBox(geometry);
            } else if constexpr (std::is_same_v<Geometry, UvSphereGeometry>) {
                if (!finite(geometry.radius) || geometry.radius <= 0 ||
                    !validSegments(geometry.longitudeSegments, 3) ||
                    !validSegments(geometry.latitudeSegments, 2))
                    return std::nullopt;
                return makeSphere(geometry);
            } else {
                if (!finite(geometry.bottomRadius) || !finite(geometry.topRadius) ||
                    !finite(geometry.height) || geometry.bottomRadius < 0 ||
                    geometry.topRadius < 0 ||
                    (geometry.bottomRadius <= math::kEpsilon &&
                     geometry.topRadius <= math::kEpsilon) ||
                    geometry.height <= 0 || !validSegments(geometry.radialSegments, 3) ||
                    !validSegments(geometry.heightSegments))
                    return std::nullopt;
                return makeCylinder(geometry);
            }
        },
        primitive.value);
}

VertexLayout layoutFor(PrimitiveVertexLayout preset) {
    VertexLayout layout;
    std::uint32_t stride = 12;
    layout.attributes.push_back(
        {{VertexSemanticType::Position, 0}, VertexFormat::Vec3Float32, 0, 0, 0});
    if (preset != PrimitiveVertexLayout::Position) {
        layout.attributes.push_back(
            {{VertexSemanticType::Normal, 0}, VertexFormat::Vec3Float32, 1, 0, 12});
        if (preset == PrimitiveVertexLayout::PositionNormalTangentUv) {
            layout.attributes.push_back(
                {{VertexSemanticType::Tangent, 0}, VertexFormat::Vec4Float32, 2, 0, 24});
            layout.attributes.push_back(
                {{VertexSemanticType::TexCoord, 0}, VertexFormat::Vec2Float32, 3, 0, 40});
            stride = 48;
        } else {
            layout.attributes.push_back(
                {{VertexSemanticType::TexCoord, 0}, VertexFormat::Vec2Float32, 2, 0, 24});
            stride = 32;
        }
    }
    layout.bindings.push_back({0, stride, VertexInputRate::Vertex});
    return layout;
}

void writeValue(std::vector<std::byte>& bytes,
                std::size_t offset,
                const void* value,
                std::size_t size) {
    std::memcpy(bytes.data() + offset, value, size);
}

} // namespace

std::optional<MeshBuildResult> MeshBuilder::build(const MeshBuildRecipe& recipe) {
    if (recipe.parts.empty() || recipe.parts.size() > 4096) {
        Log::error("MeshBuilder", "A build recipe must contain between 1 and 4096 parts");
        return std::nullopt;
    }
    if (recipe.vertexLayout < PrimitiveVertexLayout::Position ||
        recipe.vertexLayout > PrimitiveVertexLayout::PositionNormalTangentUv ||
        recipe.indexPolicy < MeshIndexPolicy::Auto ||
        recipe.indexPolicy > MeshIndexPolicy::UInt32 || recipe.usage < MeshUsage::Static ||
        recipe.usage > MeshUsage::Stream) {
        Log::error("MeshBuilder", "A build recipe contains an invalid enum");
        return std::nullopt;
    }
    std::vector<GeneratedVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<math::Vec3> allPositions;
    for (const MeshPrimitivePart& part : recipe.parts) {
        if (!finite(part.translation) || !finite(part.scale) || !finite(part.rotation) ||
            std::abs(part.scale.x) <= math::kEpsilon || std::abs(part.scale.y) <= math::kEpsilon ||
            std::abs(part.scale.z) <= math::kEpsilon ||
            math::lengthSquared(part.rotation) <= math::kEpsilon * math::kEpsilon) {
            Log::error("MeshBuilder", "A primitive part has an invalid transform");
            return std::nullopt;
        }
        auto generated = generate(part.primitive);
        if (!generated || generated->vertices.empty() || generated->indices.empty()) {
            Log::error("MeshBuilder", "A primitive part has invalid parameters");
            return std::nullopt;
        }
        if (vertices.size() + generated->vertices.size() > kMaxGeneratedVertices) {
            Log::error("MeshBuilder", "The recipe generates too many vertices");
            return std::nullopt;
        }
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(indices.size());
        const math::Mat44 transform =
            math::trs(part.translation, math::normalize(part.rotation), part.scale);
        const math::Mat33 normalTransform = math::normalMatrix(transform);
        const math::Vec3 tx = math::transformVector(transform, {1, 0, 0});
        const math::Vec3 ty = math::transformVector(transform, {0, 1, 0});
        const math::Vec3 tz = math::transformVector(transform, {0, 0, 1});
        const bool mirrored = math::dot(math::cross(tx, ty), tz) < 0.0F;
        std::vector<math::Vec3> partPositions;
        partPositions.reserve(generated->vertices.size());
        for (GeneratedVertex vertex : generated->vertices) {
            vertex.position = math::transformPoint(transform, vertex.position);
            vertex.normal = math::normalize(normalTransform * vertex.normal, math::Vec3{0, 1, 0});
            math::Vec3 tangent = math::transformVector(transform, math::Vec3{vertex.tangent});
            tangent = math::normalize(tangent - vertex.normal * math::dot(vertex.normal, tangent),
                                      math::Vec3{1, 0, 0});
            vertex.tangent = {tangent, mirrored ? -vertex.tangent.w : vertex.tangent.w};
            partPositions.push_back(vertex.position);
            allPositions.push_back(vertex.position);
            vertices.push_back(vertex);
        }
        for (std::size_t i = 0; i < generated->indices.size(); i += 3) {
            indices.push_back(baseVertex + generated->indices[i]);
            if (mirrored) {
                indices.push_back(baseVertex + generated->indices[i + 2]);
                indices.push_back(baseVertex + generated->indices[i + 1]);
            } else {
                indices.push_back(baseVertex + generated->indices[i + 1]);
                indices.push_back(baseVertex + generated->indices[i + 2]);
            }
        }
        subMeshes.push_back({firstIndex,
                             static_cast<std::uint32_t>(generated->indices.size()),
                             0,
                             part.materialSlot,
                             calculateBounds(partPositions)});
    }

    IndexType indexType = IndexType::UInt32;
    if (recipe.indexPolicy == MeshIndexPolicy::UInt16 ||
        (recipe.indexPolicy == MeshIndexPolicy::Auto &&
         vertices.size() <=
             static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U)) {
        if (vertices.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U) {
            Log::error("MeshBuilder", "The recipe exceeds the UInt16 index range");
            return std::nullopt;
        }
        indexType = IndexType::UInt16;
    }

    MeshBuildResult result;
    result.desc.debugName = recipe.name;
    result.desc.vertexLayout = layoutFor(recipe.vertexLayout);
    result.desc.indexType = indexType;
    result.desc.usage = recipe.usage;
    result.desc.topology = MeshTopology::TriangleList;
    result.desc.subMeshes = std::move(subMeshes);
    result.desc.bounds = calculateBounds(allPositions);
    result.desc.keepCpuCopy = recipe.keepCpuCopy;

    const std::uint32_t stride = result.desc.vertexLayout.bindings.front().stride;
    std::vector<std::byte> vertexBytes(vertices.size() * stride);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const std::size_t base = i * stride;
        writeValue(vertexBytes, base, &vertices[i].position, 12);
        if (recipe.vertexLayout != PrimitiveVertexLayout::Position) {
            writeValue(vertexBytes, base + 12, &vertices[i].normal, 12);
            if (recipe.vertexLayout == PrimitiveVertexLayout::PositionNormalTangentUv) {
                writeValue(vertexBytes, base + 24, &vertices[i].tangent, 16);
                writeValue(vertexBytes, base + 40, &vertices[i].uv, 8);
            } else {
                writeValue(vertexBytes, base + 24, &vertices[i].uv, 8);
            }
        }
    }
    if (!result.data.setVertexData(0, static_cast<std::uint32_t>(vertices.size()), vertexBytes))
        return std::nullopt;
    if (indexType == IndexType::UInt16) {
        std::vector<std::uint16_t> packed;
        packed.reserve(indices.size());
        for (std::uint32_t index : indices)
            packed.push_back(static_cast<std::uint16_t>(index));
        if (!result.data.setIndexData(std::span{packed}))
            return std::nullopt;
    } else if (!result.data.setIndexData(std::span{indices})) {
        return std::nullopt;
    }
    if (!validateMesh(result.desc, result.data))
        return std::nullopt;
    return result;
}

std::optional<MeshAsset> MeshBuilder::buildAsset(const MeshBuildRecipe& recipe) {
    auto result = build(recipe);
    if (!result)
        return std::nullopt;
    MeshAsset asset;
    asset.desc = std::move(result->desc);
    asset.meshData = std::move(result->data);
    asset.buildRecipe = recipe;
    return asset;
}

} // namespace engine

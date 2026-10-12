#include "asset/runtime/MeshAssetConversion.h"
namespace engine {
VertexSemanticType toRuntimeMesh(const MeshAssetVertexSemanticType& value) {
    return static_cast<VertexSemanticType>(value);
}
VertexSemantic toRuntimeMesh(const MeshAssetVertexSemantic& value) {
    return {toRuntimeMesh(value.type), value.index};
}
VertexFormat toRuntimeMesh(const MeshAssetVertexFormat& value) {
    return static_cast<VertexFormat>(value);
}
VertexInputRate toRuntimeMesh(const MeshAssetVertexInputRate& value) {
    return static_cast<VertexInputRate>(value);
}
IndexType toRuntimeMesh(const MeshAssetIndexType& value) {
    return static_cast<IndexType>(value);
}
MeshUsage toRuntimeMesh(const MeshAssetUsage& value) {
    return static_cast<MeshUsage>(value);
}
MeshTopology toRuntimeMesh(const MeshAssetTopology& value) {
    return static_cast<MeshTopology>(value);
}
MeshPrimitiveType toRuntimeMesh(const MeshPrimitiveTypeAssetData& value) {
    return static_cast<MeshPrimitiveType>(value);
}
MeshIndexPolicy toRuntimeMesh(const MeshIndexPolicyAssetData& value) {
    return static_cast<MeshIndexPolicy>(value);
}
PrimitiveVertexLayout toRuntimeMesh(const MeshAssetPrimitiveVertexLayout& value) {
    return static_cast<PrimitiveVertexLayout>(value);
}
MeshPrimitive toRuntimeMesh(const MeshPrimitiveAssetData& value) {
    MeshPrimitive result;
    result.value = std::visit(
        [](const auto& geometry) -> MeshPrimitive::Value { return toRuntimeMesh(geometry); },
        value.value);
    return result;
}
VertexStreamLayout toRuntimeMesh(const MeshAssetVertexStreamLayout& value) {
    VertexStreamLayout result;
    result.semantic = toRuntimeMesh(value.semantic);
    result.format = toRuntimeMesh(value.format);
    result.binding = value.binding;
    result.location = value.location;
    result.inputRate = toRuntimeMesh(value.inputRate);
    return result;
}
VertexLayout toRuntimeMesh(const MeshAssetVertexLayout& value) {
    VertexLayout result;
    for (const auto& item : value.streams)
        result.streams.push_back(toRuntimeMesh(item));
    return result;
}
Aabb toRuntimeMesh(const MeshAssetAabb& value) {
    Aabb result;
    result.minimum = value.minimum;
    result.maximum = value.maximum;
    return result;
}
BoundingSphere toRuntimeMesh(const MeshAssetBoundingSphere& value) {
    BoundingSphere result;
    result.center = value.center;
    result.radius = value.radius;
    return result;
}
MeshBounds toRuntimeMesh(const MeshAssetBounds& value) {
    MeshBounds result;
    result.aabb = toRuntimeMesh(value.aabb);
    result.sphere = toRuntimeMesh(value.sphere);
    return result;
}
SubMesh toRuntimeMesh(const MeshAssetSubMesh& value) {
    SubMesh result;
    result.firstIndex = value.firstIndex;
    result.indexCount = value.indexCount;
    result.vertexOffset = value.vertexOffset;
    result.materialSlot = value.materialSlot;
    result.bounds = toRuntimeMesh(value.bounds);
    return result;
}
MeshCreateInfo toRuntimeMesh(const MeshAssetDesc& value) {
    MeshCreateInfo result;
    result.debugName = value.debugName;
    result.vertexLayout = toRuntimeMesh(value.vertexLayout);
    result.indexType = toRuntimeMesh(value.indexType);
    result.usage = toRuntimeMesh(value.usage);
    result.topology = toRuntimeMesh(value.topology);
    for (const auto& item : value.subMeshes)
        result.subMeshes.push_back(toRuntimeMesh(item));
    result.bounds = toRuntimeMesh(value.bounds);
    result.keepCpuCopy = value.keepCpuCopy;
    return result;
}
VertexStream toRuntimeMesh(const MeshAssetVertexStream& value) {
    VertexStream result;
    result.binding = value.binding;
    result.vertexCount = value.vertexCount;
    result.bytes = value.bytes;
    return result;
}
MeshData toRuntimeMesh(const MeshAssetBufferData& value) {
    MeshData result;
    for (const auto& item : value.vertexStreams)
        result.vertexStreams.push_back(toRuntimeMesh(item));
    result.indices = value.indices;
    result.indexCount = value.indexCount;
    return result;
}
PlaneGeometry toRuntimeMesh(const MeshAssetPlaneGeometry& value) {
    PlaneGeometry result;
    result.size = value.size;
    result.segmentsX = value.segmentsX;
    result.segmentsZ = value.segmentsZ;
    return result;
}
BoxGeometry toRuntimeMesh(const MeshAssetBoxGeometry& value) {
    BoxGeometry result;
    result.size = value.size;
    result.segmentsX = value.segmentsX;
    result.segmentsY = value.segmentsY;
    result.segmentsZ = value.segmentsZ;
    return result;
}
UvSphereGeometry toRuntimeMesh(const MeshAssetUvSphereGeometry& value) {
    UvSphereGeometry result;
    result.radius = value.radius;
    result.longitudeSegments = value.longitudeSegments;
    result.latitudeSegments = value.latitudeSegments;
    return result;
}
CylinderGeometry toRuntimeMesh(const MeshAssetCylinderGeometry& value) {
    CylinderGeometry result;
    result.bottomRadius = value.bottomRadius;
    result.topRadius = value.topRadius;
    result.height = value.height;
    result.radialSegments = value.radialSegments;
    result.heightSegments = value.heightSegments;
    result.capBottom = value.capBottom;
    result.capTop = value.capTop;
    return result;
}
MeshPrimitivePart toRuntimeMesh(const MeshPrimitivePartAssetData& value) {
    MeshPrimitivePart result;
    result.primitive = toRuntimeMesh(value.primitive);
    result.translation = value.translation;
    result.rotation = value.rotation;
    result.scale = value.scale;
    result.materialSlot = value.materialSlot;
    return result;
}
MeshBuildRecipe toRuntimeMesh(const MeshBuildRecipeAssetData& value) {
    MeshBuildRecipe result;
    result.name = value.name;
    for (const auto& item : value.parts)
        result.parts.push_back(toRuntimeMesh(item));
    result.vertexLayout = toRuntimeMesh(value.vertexLayout);
    result.indexPolicy = toRuntimeMesh(value.indexPolicy);
    result.usage = toRuntimeMesh(value.usage);
    result.keepCpuCopy = value.keepCpuCopy;
    return result;
}
MeshAssetVertexSemanticType toMeshAsset(const VertexSemanticType& value) {
    return static_cast<MeshAssetVertexSemanticType>(value);
}
MeshAssetVertexSemantic toMeshAsset(const VertexSemantic& value) {
    return {toMeshAsset(value.type), value.index};
}
MeshAssetVertexFormat toMeshAsset(const VertexFormat& value) {
    return static_cast<MeshAssetVertexFormat>(value);
}
MeshAssetVertexInputRate toMeshAsset(const VertexInputRate& value) {
    return static_cast<MeshAssetVertexInputRate>(value);
}
MeshAssetIndexType toMeshAsset(const IndexType& value) {
    return static_cast<MeshAssetIndexType>(value);
}
MeshAssetUsage toMeshAsset(const MeshUsage& value) {
    return static_cast<MeshAssetUsage>(value);
}
MeshAssetTopology toMeshAsset(const MeshTopology& value) {
    return static_cast<MeshAssetTopology>(value);
}
MeshPrimitiveTypeAssetData toMeshAsset(const MeshPrimitiveType& value) {
    return static_cast<MeshPrimitiveTypeAssetData>(value);
}
MeshIndexPolicyAssetData toMeshAsset(const MeshIndexPolicy& value) {
    return static_cast<MeshIndexPolicyAssetData>(value);
}
MeshAssetPrimitiveVertexLayout toMeshAsset(const PrimitiveVertexLayout& value) {
    return static_cast<MeshAssetPrimitiveVertexLayout>(value);
}
MeshPrimitiveAssetData toMeshAsset(const MeshPrimitive& value) {
    MeshPrimitiveAssetData result;
    result.value = std::visit(
        [](const auto& geometry) -> MeshPrimitiveAssetData::Value { return toMeshAsset(geometry); },
        value.value);
    return result;
}
MeshAssetVertexStreamLayout toMeshAsset(const VertexStreamLayout& value) {
    MeshAssetVertexStreamLayout result;
    result.semantic = toMeshAsset(value.semantic);
    result.format = toMeshAsset(value.format);
    result.binding = value.binding;
    result.location = value.location;
    result.inputRate = toMeshAsset(value.inputRate);
    return result;
}
MeshAssetVertexLayout toMeshAsset(const VertexLayout& value) {
    MeshAssetVertexLayout result;
    for (const auto& item : value.streams)
        result.streams.push_back(toMeshAsset(item));
    return result;
}
MeshAssetAabb toMeshAsset(const Aabb& value) {
    MeshAssetAabb result;
    result.minimum = value.minimum;
    result.maximum = value.maximum;
    return result;
}
MeshAssetBoundingSphere toMeshAsset(const BoundingSphere& value) {
    MeshAssetBoundingSphere result;
    result.center = value.center;
    result.radius = value.radius;
    return result;
}
MeshAssetBounds toMeshAsset(const MeshBounds& value) {
    MeshAssetBounds result;
    result.aabb = toMeshAsset(value.aabb);
    result.sphere = toMeshAsset(value.sphere);
    return result;
}
MeshAssetSubMesh toMeshAsset(const SubMesh& value) {
    MeshAssetSubMesh result;
    result.firstIndex = value.firstIndex;
    result.indexCount = value.indexCount;
    result.vertexOffset = value.vertexOffset;
    result.materialSlot = value.materialSlot;
    result.bounds = toMeshAsset(value.bounds);
    return result;
}
MeshAssetDesc toMeshAsset(const MeshCreateInfo& value) {
    MeshAssetDesc result;
    result.debugName = value.debugName;
    result.vertexLayout = toMeshAsset(value.vertexLayout);
    result.indexType = toMeshAsset(value.indexType);
    result.usage = toMeshAsset(value.usage);
    result.topology = toMeshAsset(value.topology);
    for (const auto& item : value.subMeshes)
        result.subMeshes.push_back(toMeshAsset(item));
    result.bounds = toMeshAsset(value.bounds);
    result.keepCpuCopy = value.keepCpuCopy;
    return result;
}
MeshAssetVertexStream toMeshAsset(const VertexStream& value) {
    MeshAssetVertexStream result;
    result.binding = value.binding;
    result.vertexCount = value.vertexCount;
    result.bytes = value.bytes;
    return result;
}
MeshAssetBufferData toMeshAsset(const MeshData& value) {
    MeshAssetBufferData result;
    for (const auto& item : value.vertexStreams)
        result.vertexStreams.push_back(toMeshAsset(item));
    result.indices = value.indices;
    result.indexCount = value.indexCount;
    return result;
}
MeshAssetPlaneGeometry toMeshAsset(const PlaneGeometry& value) {
    MeshAssetPlaneGeometry result;
    result.size = value.size;
    result.segmentsX = value.segmentsX;
    result.segmentsZ = value.segmentsZ;
    return result;
}
MeshAssetBoxGeometry toMeshAsset(const BoxGeometry& value) {
    MeshAssetBoxGeometry result;
    result.size = value.size;
    result.segmentsX = value.segmentsX;
    result.segmentsY = value.segmentsY;
    result.segmentsZ = value.segmentsZ;
    return result;
}
MeshAssetUvSphereGeometry toMeshAsset(const UvSphereGeometry& value) {
    MeshAssetUvSphereGeometry result;
    result.radius = value.radius;
    result.longitudeSegments = value.longitudeSegments;
    result.latitudeSegments = value.latitudeSegments;
    return result;
}
MeshAssetCylinderGeometry toMeshAsset(const CylinderGeometry& value) {
    MeshAssetCylinderGeometry result;
    result.bottomRadius = value.bottomRadius;
    result.topRadius = value.topRadius;
    result.height = value.height;
    result.radialSegments = value.radialSegments;
    result.heightSegments = value.heightSegments;
    result.capBottom = value.capBottom;
    result.capTop = value.capTop;
    return result;
}
MeshPrimitivePartAssetData toMeshAsset(const MeshPrimitivePart& value) {
    MeshPrimitivePartAssetData result;
    result.primitive = toMeshAsset(value.primitive);
    result.translation = value.translation;
    result.rotation = value.rotation;
    result.scale = value.scale;
    result.materialSlot = value.materialSlot;
    return result;
}
MeshBuildRecipeAssetData toMeshAsset(const MeshBuildRecipe& value) {
    MeshBuildRecipeAssetData result;
    result.name = value.name;
    for (const auto& item : value.parts)
        result.parts.push_back(toMeshAsset(item));
    result.vertexLayout = toMeshAsset(value.vertexLayout);
    result.indexPolicy = toMeshAsset(value.indexPolicy);
    result.usage = toMeshAsset(value.usage);
    result.keepCpuCopy = value.keepCpuCopy;
    return result;
}
} // namespace engine

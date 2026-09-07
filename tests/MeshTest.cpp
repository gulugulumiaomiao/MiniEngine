#include "render/mesh/Mesh.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"
#include "render/mesh/MeshBuilder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace {

template <typename Value>
Value readAt(const std::vector<std::byte>& bytes, std::size_t offset) {
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
    if (!asset.transfer(reader) || !reader.finished() ||
        decoded.type() != AssetType::Mesh ||
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
    if (!jsonDecoded.transfer(jsonReader) ||
        jsonDecoded.desc.debugName != source.desc.debugName ||
        jsonDecoded.desc.vertexLayout != source.desc.vertexLayout ||
        jsonDecoded.meshData.vertexStreams != source.meshData.vertexStreams ||
        jsonDecoded.meshData.indices != source.meshData.indices) {
        return 10;
    }

    Mesh runtime = decoded.instantiate();
    if (runtime.assetPath() != source.assetPath() || runtime.version() != 1 ||
        !runtime.dirty() || runtime.data().vertexStreams.size() != 2) {
        return 4;
    }
    runtime.markClean();
    const math::Vec3 replacement{2.0F, 3.0F, 4.0F};
    if (!runtime.updateVertexData(0, 1,
                                  std::as_bytes(std::span{&replacement, 1})) ||
        runtime.version() != 2 || !runtime.dirty()) {
        return 5;
    }
    const std::uint16_t replacementIndex = 1;
    if (!runtime.updateIndexData(
            2, std::as_bytes(std::span{&replacementIndex, 1})) ||
        runtime.version() != 3) {
        return 6;
    }

    std::vector<std::byte> invalid = binary;
    invalid.front() = std::byte{0};
    const std::string originalName = decoded.desc.debugName;
    BinaryReader invalidReader{invalid};
    if (decoded.transfer(invalidReader) ||
        decoded.desc.debugName != originalName) {
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
    if (!plane || plane->data.vertexStreams[0].vertexCount != 4 ||
        plane->data.indexCount != 6 ||
        plane->desc.indexType != IndexType::UInt16 ||
        !math::nearlyEqual(plane->desc.bounds.aabb.minimum.x, -1.0F) ||
        !math::nearlyEqual(plane->desc.bounds.aabb.maximum.z, 2.0F)) {
        return 11;
    }

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
    cylinderPart.primitive =
        CylinderGeometry{0.5F, 0.25F, 1.0F, 8, 1, true, true};
    cylinderPart.translation = {2.0F, 0.0F, 0.0F};
    cylinderPart.materialSlot = 4;
    primitives.parts = {boxPart, spherePart, cylinderPart};
    auto combined = MeshBuilder::buildAsset(primitives);
    if (!combined || !combined->buildRecipe ||
        combined->desc.subMeshes.size() != 3 ||
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
    if (!proceduralDecoded.transfer(proceduralReader) ||
        !proceduralReader.finished() || !proceduralDecoded.buildRecipe ||
        proceduralDecoded.buildRecipe->parts.size() != 3 ||
        proceduralDecoded.buildRecipe->parts[1].primitive.type() !=
            MeshPrimitiveType::UvSphere) {
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
        !v2Writer.transfer("version", v2Version) ||
        !v2Writer.transfer("description", v2Desc) ||
        !v2Writer.transfer("mesh_data", v2Data) || !v2Writer.endObject()) {
        return 18;
    }
    MeshAsset v2Decoded;
    BinaryReader v2Reader{v2Writer.bytes()};
    if (!v2Decoded.transfer(v2Reader) || !v2Reader.finished() ||
        v2Decoded.buildRecipe ||
        v2Decoded.desc.debugName != source.desc.debugName) {
        return 18;
    }
    return 0;
}

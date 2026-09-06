#include "render/mesh/Mesh.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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
        {{VertexSemanticType::Position, 0}, VertexFormat::Vec3Float32,
         0, 0, 0},
        {{VertexSemanticType::Color, 0}, VertexFormat::Vec4Float32,
         1, 1, 0},
    };
    source.desc.indexType = IndexType::UInt16;
    source.desc.usage = MeshUsage::Dynamic;
    source.desc.bounds = calculateBounds(positions);
    source.desc.subMeshes.push_back(
        {0, 3, 0, 0, source.desc.bounds});
    if (!source.meshData.setVertexData(0, std::span{positions}) ||
        !source.meshData.setVertexData(1, std::span{colors}) ||
        !source.meshData.setIndexData(std::span{indices}) ||
        !validateMesh(source.desc, source.meshData)) {
        return 1;
    }

    BinaryWriter writer;
    if (!source.transfer(writer)) return 2;
    const std::vector<std::byte> binary = writer.takeBytes();
    if (binary.empty()) return 2;

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
    if (!source.transfer(jsonWriter)) return 10;
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
    if (!runtime.updateVertexData(
            0, 1, std::as_bytes(std::span{&replacement, 1})) ||
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
    return 0;
}

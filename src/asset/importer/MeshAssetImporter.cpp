#include "asset/importer/MeshAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"

#include <cstring>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <utility>

namespace engine {
namespace {

using Json = nlohmann::json;

template <typename Enum> std::optional<Enum> enumValue(std::string_view) = delete;

template <> std::optional<VertexSemanticType> enumValue(std::string_view value) {
    if (value == "position")
        return VertexSemanticType::Position;
    if (value == "normal")
        return VertexSemanticType::Normal;
    if (value == "tangent")
        return VertexSemanticType::Tangent;
    if (value == "color")
        return VertexSemanticType::Color;
    if (value == "tex_coord")
        return VertexSemanticType::TexCoord;
    if (value == "joint_indices")
        return VertexSemanticType::JointIndices;
    if (value == "joint_weights")
        return VertexSemanticType::JointWeights;
    if (value == "custom")
        return VertexSemanticType::Custom;
    return std::nullopt;
}

template <> std::optional<VertexFormat> enumValue(std::string_view value) {
    if (value == "float32")
        return VertexFormat::Float32;
    if (value == "vec2_float32")
        return VertexFormat::Vec2Float32;
    if (value == "vec3_float32")
        return VertexFormat::Vec3Float32;
    if (value == "vec4_float32")
        return VertexFormat::Vec4Float32;
    if (value == "uint16x4")
        return VertexFormat::UInt16x4;
    if (value == "uint8x4_normalized")
        return VertexFormat::UInt8x4Normalized;
    return std::nullopt;
}

template <> std::optional<VertexInputRate> enumValue(std::string_view value) {
    if (value == "vertex")
        return VertexInputRate::Vertex;
    if (value == "instance")
        return VertexInputRate::Instance;
    return std::nullopt;
}

template <> std::optional<IndexType> enumValue(std::string_view value) {
    if (value == "uint16")
        return IndexType::UInt16;
    if (value == "uint32")
        return IndexType::UInt32;
    return std::nullopt;
}

template <> std::optional<MeshUsage> enumValue(std::string_view value) {
    if (value == "static")
        return MeshUsage::Static;
    if (value == "dynamic")
        return MeshUsage::Dynamic;
    if (value == "stream")
        return MeshUsage::Stream;
    return std::nullopt;
}

template <> std::optional<MeshTopology> enumValue(std::string_view value) {
    if (value == "triangle_list")
        return MeshTopology::TriangleList;
    if (value == "line_list")
        return MeshTopology::LineList;
    return std::nullopt;
}

template <typename Enum>
bool readEnum(const Json& object, const char* name, Enum& result, bool optional = false) {
    const auto value = object.find(name);
    if (value == object.end())
        return optional;
    if (!value->is_string())
        return false;
    const auto parsed = enumValue<Enum>(value->get_ref<const std::string&>());
    if (!parsed)
        return false;
    result = *parsed;
    return true;
}

bool readBytes(const Json& values, std::vector<std::byte>& result) {
    if (!values.is_array())
        return false;
    result.reserve(values.size());
    for (const Json& value : values) {
        if (!value.is_number_unsigned())
            return false;
        const auto byte = value.get<std::uint32_t>();
        if (byte > 255)
            return false;
        result.push_back(static_cast<std::byte>(byte));
    }
    return true;
}

std::optional<MeshBounds> calculateMeshBounds(const MeshAsset& asset) {
    const VertexAttribute* position =
        asset.desc.vertexLayout.find({VertexSemanticType::Position, 0});
    const VertexBinding* binding =
        position ? asset.desc.vertexLayout.findBinding(position->binding) : nullptr;
    const VertexStream* stream =
        position ? asset.meshData.findVertexStream(position->binding) : nullptr;
    if (!position || !binding || !stream || position->format != VertexFormat::Vec3Float32) {
        return std::nullopt;
    }
    std::vector<math::Vec3> positions(stream->vertexCount);
    for (std::uint32_t index = 0; index < stream->vertexCount; ++index) {
        const std::size_t offset =
            static_cast<std::size_t>(index) * binding->stride + position->offset;
        if (offset + sizeof(math::Vec3) > stream->bytes.size())
            return std::nullopt;
        std::memcpy(&positions[index], stream->bytes.data() + offset, sizeof(math::Vec3));
    }
    return calculateBounds(positions);
}

template <typename Vector>
bool readVector(
    const Json& object, const char* name, Vector& result, std::size_t size, bool optional = true) {
    const auto value = object.find(name);
    if (value == object.end())
        return optional;
    if (!value->is_array() || value->size() != size)
        return false;
    for (std::size_t index = 0; index < size; ++index) {
        if (!(*value)[index].is_number())
            return false;
        result[index] = (*value)[index].get<float>();
    }
    return true;
}

bool readPrimitivePart(const Json& value, MeshPrimitivePart& part) {
    if (!value.is_object() || !value.contains("type") || !value["type"].is_string())
        return false;
    const Json& parameters = value.value("parameters", Json::object());
    if (!parameters.is_object())
        return false;
    const std::string& type = value["type"].get_ref<const std::string&>();
    if (type == "plane") {
        PlaneGeometry geometry;
        if (!readVector(parameters, "size", geometry.size, 2))
            return false;
        geometry.segmentsX = parameters.value("segments_x", geometry.segmentsX);
        geometry.segmentsZ = parameters.value("segments_z", geometry.segmentsZ);
        part.primitive = geometry;
    } else if (type == "box" || type == "cube") {
        BoxGeometry geometry;
        if (!readVector(parameters, "size", geometry.size, 3))
            return false;
        geometry.segmentsX = parameters.value("segments_x", 1U);
        geometry.segmentsY = parameters.value("segments_y", 1U);
        geometry.segmentsZ = parameters.value("segments_z", 1U);
        part.primitive = geometry;
    } else if (type == "uv_sphere" || type == "sphere") {
        UvSphereGeometry geometry;
        geometry.radius = parameters.value("radius", geometry.radius);
        geometry.longitudeSegments =
            parameters.value("longitude_segments", geometry.longitudeSegments);
        geometry.latitudeSegments =
            parameters.value("latitude_segments", geometry.latitudeSegments);
        part.primitive = geometry;
    } else if (type == "cylinder") {
        CylinderGeometry geometry;
        geometry.bottomRadius = parameters.value("bottom_radius", geometry.bottomRadius);
        geometry.topRadius = parameters.value("top_radius", geometry.topRadius);
        geometry.height = parameters.value("height", geometry.height);
        geometry.radialSegments = parameters.value("radial_segments", geometry.radialSegments);
        geometry.heightSegments = parameters.value("height_segments", geometry.heightSegments);
        geometry.capBottom = parameters.value("cap_bottom", geometry.capBottom);
        geometry.capTop = parameters.value("cap_top", geometry.capTop);
        part.primitive = geometry;
    } else {
        return false;
    }
    if (!readVector(value, "translation", part.translation, 3) ||
        !readVector(value, "scale", part.scale, 3))
        return false;
    math::Vec4 rotation{part.rotation.x, part.rotation.y, part.rotation.z, part.rotation.w};
    if (!readVector(value, "rotation", rotation, 4))
        return false;
    part.rotation = math::Quat{rotation.w, rotation.x, rotation.y, rotation.z};
    part.materialSlot = value.value("material_slot", 0U);
    return true;
}

std::shared_ptr<MeshAsset>
parseProceduralMesh(const VirtualPath& path, const Json& root, const Json& source) {
    MeshBuildRecipe recipe;
    recipe.name = root.value("name", path.filename());
    recipe.keepCpuCopy = root.value("keep_cpu_copy", false);
    if (!readEnum(root, "usage", recipe.usage, true))
        return {};
    const std::string layout =
        source.value("vertex_layout", std::string{"position_normal_tangent_uv"});
    if (layout == "position") {
        recipe.vertexLayout = PrimitiveVertexLayout::Position;
    } else if (layout == "position_normal_uv") {
        recipe.vertexLayout = PrimitiveVertexLayout::PositionNormalUv;
    } else if (layout != "position_normal_tangent_uv") {
        return {};
    }
    const std::string policy = source.value("index_policy", std::string{"auto"});
    if (policy == "auto")
        recipe.indexPolicy = MeshIndexPolicy::Auto;
    else if (policy == "uint16")
        recipe.indexPolicy = MeshIndexPolicy::UInt16;
    else if (policy == "uint32")
        recipe.indexPolicy = MeshIndexPolicy::UInt32;
    else
        return {};
    const auto parts = source.find("parts");
    if (parts == source.end() || !parts->is_array())
        return {};
    for (const Json& value : *parts) {
        MeshPrimitivePart part;
        if (!readPrimitivePart(value, part))
            return {};
        recipe.parts.push_back(std::move(part));
    }
    auto built = MeshBuilder::buildAsset(recipe);
    if (!built)
        return {};
    return std::make_shared<MeshAsset>(std::move(*built));
}

std::shared_ptr<MeshAsset> parseRawMesh(const VirtualPath& path, const Json& root) {
    auto asset = std::make_shared<MeshAsset>();
    asset->desc.debugName = root.value("name", path.filename());
    asset->desc.keepCpuCopy = root.value("keep_cpu_copy", false);
    if (!readEnum(root, "index_type", asset->desc.indexType) ||
        !readEnum(root, "usage", asset->desc.usage, true) ||
        !readEnum(root, "topology", asset->desc.topology, true))
        return {};

    const auto bindings = root.find("bindings");
    const auto attributes = root.find("attributes");
    const auto streams = root.find("vertex_streams");
    const auto indices = root.find("indices");
    if (bindings == root.end() || !bindings->is_array() || attributes == root.end() ||
        !attributes->is_array() || streams == root.end() || !streams->is_array() ||
        indices == root.end() || !indices->is_array())
        return {};

    for (const Json& value : *bindings) {
        if (!value.is_object())
            return {};
        VertexBinding binding;
        if (!value.contains("binding") || !value.contains("stride") ||
            !value["binding"].is_number_unsigned() || !value["stride"].is_number_unsigned() ||
            !readEnum(value, "input_rate", binding.inputRate, true))
            return {};
        binding.binding = value["binding"].get<std::uint32_t>();
        binding.stride = value["stride"].get<std::uint32_t>();
        asset->desc.vertexLayout.bindings.push_back(binding);
    }
    for (const Json& value : *attributes) {
        if (!value.is_object() || !value.contains("semantic") || !value["semantic"].is_string() ||
            !value.contains("location") || !value["location"].is_number_unsigned() ||
            !value.contains("binding") || !value["binding"].is_number_unsigned() ||
            !value.contains("offset") || !value["offset"].is_number_unsigned())
            return {};
        VertexAttribute attribute;
        const auto semantic =
            enumValue<VertexSemanticType>(value["semantic"].get_ref<const std::string&>());
        if (!semantic || !readEnum(value, "format", attribute.format))
            return {};
        attribute.semantic = {*semantic,
                              static_cast<std::uint8_t>(value.value("semantic_index", 0U))};
        attribute.location = value["location"].get<std::uint32_t>();
        attribute.binding = value["binding"].get<std::uint32_t>();
        attribute.offset = value["offset"].get<std::uint32_t>();
        asset->desc.vertexLayout.attributes.push_back(attribute);
    }
    for (const Json& value : *streams) {
        if (!value.is_object() || !value.contains("binding") ||
            !value["binding"].is_number_unsigned() || !value.contains("vertex_count") ||
            !value["vertex_count"].is_number_unsigned() || !value.contains("bytes"))
            return {};
        VertexStream stream;
        stream.binding = value["binding"].get<std::uint32_t>();
        stream.vertexCount = value["vertex_count"].get<std::uint32_t>();
        if (!readBytes(value["bytes"], stream.bytes))
            return {};
        asset->meshData.vertexStreams.push_back(std::move(stream));
    }

    if (asset->desc.indexType == IndexType::UInt16) {
        std::vector<std::uint16_t> values;
        for (const Json& value : *indices) {
            if (!value.is_number_unsigned() || value.get<std::uint32_t>() > 65535)
                return {};
            values.push_back(static_cast<std::uint16_t>(value.get<std::uint32_t>()));
        }
        if (!asset->meshData.setIndexData(std::span{values}))
            return {};
    } else {
        std::vector<std::uint32_t> values;
        for (const Json& value : *indices) {
            if (!value.is_number_unsigned())
                return {};
            values.push_back(value.get<std::uint32_t>());
        }
        if (!asset->meshData.setIndexData(std::span{values}))
            return {};
    }

    const auto bounds = calculateMeshBounds(*asset);
    if (!bounds)
        return {};
    asset->desc.bounds = *bounds;
    const auto subMeshes = root.find("sub_meshes");
    if (subMeshes == root.end()) {
        asset->desc.subMeshes.push_back({0, asset->meshData.indexCount, 0, 0, *bounds});
    } else if (subMeshes->is_array()) {
        for (const Json& value : *subMeshes) {
            if (!value.is_object())
                return {};
            SubMesh subMesh;
            subMesh.firstIndex = value.value("first_index", 0U);
            subMesh.indexCount = value.value("index_count", 0U);
            subMesh.vertexOffset = value.value("vertex_offset", 0);
            subMesh.materialSlot = value.value("material_slot", 0U);
            subMesh.bounds = *bounds;
            asset->desc.subMeshes.push_back(subMesh);
        }
    } else
        return {};
    return validateMesh(asset->desc, asset->meshData) ? asset : nullptr;
}

std::shared_ptr<MeshAsset> parseMesh(const VirtualPath& path, std::string_view text) {
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return {};
    const auto source = root.find("source");
    if (source != root.end()) {
        if (!source->is_object() || source->value("type", std::string{}) != "procedural")
            return {};
        return parseProceduralMesh(path, root, *source);
    }
    return parseRawMesh(path, root);
}

} // namespace

AssetImportResult MeshAssetImporter::import(const AssetImportContext& context) const {
    const auto fail = [](std::string error) {
        Log::error("MeshAssetImporter", "%s", error.c_str());
        return AssetImportResult::failed(AssetType::Mesh, std::move(error));
    };
    if (context.meta.assetType != AssetType::Mesh || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid())
        return fail("Invalid Mesh import context");
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source)
        return fail("Cannot read MeshAsset: " + context.sourcePath.string());
    const auto mesh = parseMesh(context.sourcePath, *source);
    if (!mesh)
        return fail("Cannot parse MeshAsset: " + context.sourcePath.string());
    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return fail("Cannot prepare Mesh Artifact: " + context.artifactPath.string());
    }
    BinaryWriter writer;
    if (!mesh->transfer(writer)) {
        return fail("Cannot serialize Mesh Artifact: " + context.sourcePath.string());
    }
    const AssetArtifact artifact{
        1, context.meta.assetId, AssetType::Mesh, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return fail("Cannot save Mesh Artifact: " + context.artifactPath.string());
    }
    return AssetImportResult::succeeded(AssetType::Mesh, context.artifactPath);
}

} // namespace engine

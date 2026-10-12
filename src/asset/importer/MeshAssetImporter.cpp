#include "asset/types/MeshAsset.h"
#include "asset/runtime/MeshAssetResolver.h"
#include "asset/importer/MeshAssetImporter.h"

#include "asset/importer/AssetImportHelpers.h"
#include "core/base/Ref.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
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

template <> std::optional<MeshAssetVertexSemanticType> enumValue(std::string_view value) {
    if (value == "position")
        return MeshAssetVertexSemanticType::Position;
    if (value == "normal")
        return MeshAssetVertexSemanticType::Normal;
    if (value == "tangent")
        return MeshAssetVertexSemanticType::Tangent;
    if (value == "color")
        return MeshAssetVertexSemanticType::Color;
    if (value == "tex_coord")
        return MeshAssetVertexSemanticType::TexCoord;
    if (value == "joint_indices")
        return MeshAssetVertexSemanticType::JointIndices;
    if (value == "joint_weights")
        return MeshAssetVertexSemanticType::JointWeights;
    if (value == "custom")
        return MeshAssetVertexSemanticType::Custom;
    return std::nullopt;
}

template <> std::optional<MeshAssetVertexFormat> enumValue(std::string_view value) {
    if (value == "float32")
        return MeshAssetVertexFormat::Float32;
    if (value == "vec2_float32")
        return MeshAssetVertexFormat::Vec2Float32;
    if (value == "vec3_float32")
        return MeshAssetVertexFormat::Vec3Float32;
    if (value == "vec4_float32")
        return MeshAssetVertexFormat::Vec4Float32;
    if (value == "uint16x4")
        return MeshAssetVertexFormat::UInt16x4;
    if (value == "uint8x4_normalized")
        return MeshAssetVertexFormat::UInt8x4Normalized;
    return std::nullopt;
}

template <> std::optional<MeshAssetVertexInputRate> enumValue(std::string_view value) {
    if (value == "vertex")
        return MeshAssetVertexInputRate::Vertex;
    if (value == "instance")
        return MeshAssetVertexInputRate::Instance;
    return std::nullopt;
}

template <> std::optional<MeshAssetIndexType> enumValue(std::string_view value) {
    if (value == "uint16")
        return MeshAssetIndexType::UInt16;
    if (value == "uint32")
        return MeshAssetIndexType::UInt32;
    return std::nullopt;
}

template <> std::optional<MeshAssetUsage> enumValue(std::string_view value) {
    if (value == "static")
        return MeshAssetUsage::Static;
    if (value == "dynamic")
        return MeshAssetUsage::Dynamic;
    if (value == "stream")
        return MeshAssetUsage::Stream;
    return std::nullopt;
}

template <> std::optional<MeshAssetTopology> enumValue(std::string_view value) {
    if (value == "triangle_list")
        return MeshAssetTopology::TriangleList;
    if (value == "line_list")
        return MeshAssetTopology::LineList;
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

std::optional<MeshAssetBounds> calculateMeshBounds(const MeshAsset& asset) {
    const MeshAssetVertexStreamLayout* position =
        asset.data.desc.vertexLayout.find({MeshAssetVertexSemanticType::Position, 0});
    const MeshAssetVertexStream* stream =
        position ? asset.data.meshData.findVertexStream(position->binding) : nullptr;
    if (!position || !stream || position->format != MeshAssetVertexFormat::Vec3Float32) {
        return std::nullopt;
    }
    const std::uint32_t stride = vertexFormatSize(position->format);
    std::vector<math::Vec3> positions(stream->vertexCount);
    for (std::uint32_t index = 0; index < stream->vertexCount; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * stride;
        if (offset + sizeof(math::Vec3) > stream->bytes.size())
            return std::nullopt;
        std::memcpy(&positions[index], stream->bytes.data() + offset, sizeof(math::Vec3));
    }
    return toMeshAsset(calculateBounds(positions));
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

bool readPrimitivePart(const Json& value, MeshPrimitivePartAssetData& part) {
    if (!value.is_object() || !value.contains("type") || !value["type"].is_string())
        return false;
    const Json& parameters = value.value("parameters", Json::object());
    if (!parameters.is_object())
        return false;
    const std::string& type = value["type"].get_ref<const std::string&>();
    if (type == "plane") {
        MeshAssetPlaneGeometry geometry;
        if (!readVector(parameters, "size", geometry.size, 2))
            return false;
        geometry.segmentsX = parameters.value("segments_x", geometry.segmentsX);
        geometry.segmentsZ = parameters.value("segments_z", geometry.segmentsZ);
        part.primitive = geometry;
    } else if (type == "box" || type == "cube") {
        MeshAssetBoxGeometry geometry;
        if (!readVector(parameters, "size", geometry.size, 3))
            return false;
        geometry.segmentsX = parameters.value("segments_x", 1U);
        geometry.segmentsY = parameters.value("segments_y", 1U);
        geometry.segmentsZ = parameters.value("segments_z", 1U);
        part.primitive = geometry;
    } else if (type == "uv_sphere" || type == "sphere") {
        MeshAssetUvSphereGeometry geometry;
        geometry.radius = parameters.value("radius", geometry.radius);
        geometry.longitudeSegments =
            parameters.value("longitude_segments", geometry.longitudeSegments);
        geometry.latitudeSegments =
            parameters.value("latitude_segments", geometry.latitudeSegments);
        part.primitive = geometry;
    } else if (type == "cylinder") {
        MeshAssetCylinderGeometry geometry;
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

Ref<MeshAsset> parseProceduralMesh(const VirtualPath& path, const Json& root, const Json& source) {
    MeshBuildRecipeAssetData recipe;
    recipe.name = root.value("name", path.filename());
    recipe.keepCpuCopy = root.value("keep_cpu_copy", false);
    if (!readEnum(root, "usage", recipe.usage, true))
        return {};
    const std::string layout =
        source.value("vertex_layout", std::string{"position_normal_tangent_uv"});
    if (layout == "position") {
        recipe.vertexLayout = MeshAssetPrimitiveVertexLayout::Position;
    } else if (layout == "position_normal_uv") {
        recipe.vertexLayout = MeshAssetPrimitiveVertexLayout::PositionNormalUv;
    } else if (layout != "position_normal_tangent_uv") {
        return {};
    }
    const std::string policy = source.value("index_policy", std::string{"auto"});
    if (policy == "auto")
        recipe.indexPolicy = MeshIndexPolicyAssetData::Auto;
    else if (policy == "uint16")
        recipe.indexPolicy = MeshIndexPolicyAssetData::UInt16;
    else if (policy == "uint32")
        recipe.indexPolicy = MeshIndexPolicyAssetData::UInt32;
    else
        return {};
    const auto parts = source.find("parts");
    if (parts == source.end() || !parts->is_array())
        return {};
    for (const Json& value : *parts) {
        MeshPrimitivePartAssetData part;
        if (!readPrimitivePart(value, part))
            return {};
        recipe.parts.push_back(std::move(part));
    }
    Ref<MeshAsset> built = buildMeshAsset(recipe);
    if (!built)
        return {};
    return built;
}

Ref<MeshAsset> parseRawMesh(const VirtualPath& path, const Json& root) {
    auto asset = makeRef<MeshAsset>();
    asset->data.desc.debugName = root.value("name", path.filename());
    asset->data.desc.keepCpuCopy = root.value("keep_cpu_copy", false);
    if (!readEnum(root, "index_type", asset->data.desc.indexType) ||
        !readEnum(root, "usage", asset->data.desc.usage, true) ||
        !readEnum(root, "topology", asset->data.desc.topology, true))
        return {};

    const auto streams = root.find("vertex_streams");
    const auto indices = root.find("indices");
    if (streams == root.end() || !streams->is_array() || indices == root.end() ||
        !indices->is_array())
        return {};

    for (const Json& value : *streams) {
        if (!value.is_object() || !value.contains("binding") ||
            !value["binding"].is_number_unsigned() || !value.contains("location") ||
            !value["location"].is_number_unsigned() || !value.contains("semantic") ||
            !value["semantic"].is_string() || !value.contains("format") ||
            !value["format"].is_string())
            return {};
        MeshAssetVertexStreamLayout layout;
        const auto semantic =
            enumValue<MeshAssetVertexSemanticType>(value["semantic"].get_ref<const std::string&>());
        if (!semantic || !readEnum(value, "format", layout.format))
            return {};
        layout.semantic = {*semantic, static_cast<std::uint8_t>(value.value("semantic_index", 0U))};
        layout.binding = value["binding"].get<std::uint32_t>();
        layout.location = value["location"].get<std::uint32_t>();
        if (!readEnum(value, "input_rate", layout.inputRate, true))
            return {};
        asset->data.desc.vertexLayout.streams.push_back(layout);
    }
    for (const Json& value : *streams) {
        if (!value.is_object() || !value.contains("binding") ||
            !value["binding"].is_number_unsigned() || !value.contains("vertex_count") ||
            !value["vertex_count"].is_number_unsigned() || !value.contains("bytes"))
            return {};
        MeshAssetVertexStream stream;
        stream.binding = value["binding"].get<std::uint32_t>();
        stream.vertexCount = value["vertex_count"].get<std::uint32_t>();
        if (!readBytes(value["bytes"], stream.bytes))
            return {};
        asset->data.meshData.vertexStreams.push_back(std::move(stream));
    }

    if (asset->data.desc.indexType == MeshAssetIndexType::UInt16) {
        std::vector<std::uint16_t> values;
        for (const Json& value : *indices) {
            if (!value.is_number_unsigned() || value.get<std::uint32_t>() > 65535)
                return {};
            values.push_back(static_cast<std::uint16_t>(value.get<std::uint32_t>()));
        }
        if (!asset->data.meshData.setIndexData(std::span{values}))
            return {};
    } else {
        std::vector<std::uint32_t> values;
        for (const Json& value : *indices) {
            if (!value.is_number_unsigned())
                return {};
            values.push_back(value.get<std::uint32_t>());
        }
        if (!asset->data.meshData.setIndexData(std::span{values}))
            return {};
    }

    const auto bounds = calculateMeshBounds(*asset);
    if (!bounds)
        return {};
    asset->data.desc.bounds = *bounds;
    const auto subMeshes = root.find("sub_meshes");
    if (subMeshes == root.end()) {
        asset->data.desc.subMeshes.push_back({0, asset->data.meshData.indexCount, 0, 0, *bounds});
    } else if (subMeshes->is_array()) {
        for (const Json& value : *subMeshes) {
            if (!value.is_object())
                return {};
            MeshAssetSubMesh subMesh;
            subMesh.firstIndex = value.value("first_index", 0U);
            subMesh.indexCount = value.value("index_count", 0U);
            subMesh.vertexOffset = value.value("vertex_offset", 0);
            subMesh.materialSlot = value.value("material_slot", 0U);
            subMesh.bounds = *bounds;
            asset->data.desc.subMeshes.push_back(subMesh);
        }
    } else
        return {};
    return asset->buildRuntimeInfo().has_value() ? asset : nullptr;
}

Ref<MeshAsset> parseMesh(const VirtualPath& path, std::string_view text) {
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

bool MeshImportSettings::transfer(Transfer& archive) {
    (void)archive;
    return true;
}

Hash64 MeshImportSettings::hash() const {
    return hashString("MeshImportSettings");
}

std::unique_ptr<AssetImportSettings>
MeshAssetImporter::createDefaultSettings(const VirtualPath&) const {
    return std::make_unique<MeshImportSettings>();
}

std::vector<VirtualPath> MeshAssetImporter::gatherDependencies(const AssetImportContext&,
                                                               const AssetImportSettings&) const {
    return {};
}

AssetImportResult MeshAssetImporter::import(const AssetImportContext& context,
                                            const AssetImportSettings&) const {
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
    return writeAssetArtifact(context, *mesh, AssetType::Mesh);
}

} // namespace engine

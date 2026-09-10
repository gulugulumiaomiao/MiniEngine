#include "scene/scene/SceneAsset.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t kSceneMagic = 0x454e4353U;
constexpr std::uint16_t kSceneBinaryVersion = 3;
constexpr std::uint32_t kSceneJsonVersion = 1;
constexpr std::uint32_t kMaxNodes = 1U << 20U;
constexpr std::uint32_t kMaxComponentsPerNode = 32;
constexpr std::uint32_t kMaxMaterialsPerNode = 1U << 16U;

bool fail(const VirtualPath& path, std::string_view message) {
    Log::error("SceneAsset",
               "%s: %.*s",
               path.string().c_str(),
               static_cast<int>(message.size()),
               message.data());
    return false;
}

bool finite(float value) {
    return std::isfinite(value);
}
bool finite(const math::Vec2& value) {
    return finite(value.x) && finite(value.y);
}
bool finite(const math::Vec3& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}
bool finite(const math::Vec4& value) {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}
bool finite(const math::Quat& value) {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

bool readFloat(const Json& object, const char* key, float& result, bool optional = true) {
    const auto value = object.find(key);
    if (value == object.end())
        return optional;
    if (!value->is_number())
        return false;
    result = value->get<float>();
    return finite(result);
}

bool readBool(const Json& object, const char* key, bool& result, bool optional = true) {
    const auto value = object.find(key);
    if (value == object.end())
        return optional;
    if (!value->is_boolean())
        return false;
    result = value->get<bool>();
    return true;
}

bool readUInt(const Json& object, const char* key, std::uint32_t& result, bool optional = true) {
    const auto value = object.find(key);
    if (value == object.end())
        return optional;
    if (!value->is_number_unsigned())
        return false;
    result = value->get<std::uint32_t>();
    return true;
}

bool readInt(const Json& object, const char* key, int& result, bool optional = true) {
    const auto value = object.find(key);
    if (value == object.end())
        return optional;
    if (!value->is_number_integer())
        return false;
    result = value->get<int>();
    return true;
}

template <typename Vector>
bool readVector(
    const Json& object, const char* key, Vector& result, std::size_t size, bool optional = true) {
    const auto value = object.find(key);
    if (value == object.end())
        return optional;
    if (!value->is_array() || value->size() != size)
        return false;
    for (std::size_t index = 0; index < size; ++index) {
        if (!(*value)[index].is_number())
            return false;
        result[static_cast<typename Vector::length_type>(index)] = (*value)[index].get<float>();
    }
    return true;
}

bool readRotation(const Json& object, math::Quat& result) {
    const auto value = object.find("rotation");
    if (value == object.end())
        return true;
    if (!value->is_array() || value->size() != 4)
        return false;
    for (const Json& element : *value) {
        if (!element.is_number())
            return false;
    }
    result.x = (*value)[0].get<float>();
    result.y = (*value)[1].get<float>();
    result.z = (*value)[2].get<float>();
    result.w = (*value)[3].get<float>();
    return finite(result);
}

VirtualPath readAssetPath(const VirtualPath& owner, const std::string& value) {
    if (value.find("://") != std::string::npos)
        return VirtualPath{value};
    return owner.parent().joined(value);
}

bool readPrimitive(const Json& source, MeshPrimitive& result) {
    if (!source.is_object())
        return false;
    const auto type = source.find("type");
    const auto parameters = source.find("parameters");
    if (type == source.end() || !type->is_string() ||
        (parameters != source.end() && !parameters->is_object()))
        return false;
    const Json& values = parameters == source.end() ? Json::object() : *parameters;
    const std::string& name = type->get_ref<const std::string&>();
    if (name == "plane") {
        PlaneGeometry geometry;
        if (!readVector(values, "size", geometry.size, 2) ||
            !readUInt(values, "segments_x", geometry.segmentsX) ||
            !readUInt(values, "segments_z", geometry.segmentsZ))
            return false;
        result = geometry;
    } else if (name == "box" || name == "cube") {
        BoxGeometry geometry;
        if (!readVector(values, "size", geometry.size, 3) ||
            !readUInt(values, "segments_x", geometry.segmentsX) ||
            !readUInt(values, "segments_y", geometry.segmentsY) ||
            !readUInt(values, "segments_z", geometry.segmentsZ))
            return false;
        result = geometry;
    } else if (name == "sphere" || name == "uv_sphere") {
        UvSphereGeometry geometry;
        if (!readFloat(values, "radius", geometry.radius) ||
            !readUInt(values, "longitude_segments", geometry.longitudeSegments) ||
            !readUInt(values, "latitude_segments", geometry.latitudeSegments))
            return false;
        result = geometry;
    } else if (name == "cylinder") {
        CylinderGeometry geometry;
        if (!readFloat(values, "bottom_radius", geometry.bottomRadius) ||
            !readFloat(values, "top_radius", geometry.topRadius) ||
            !readFloat(values, "height", geometry.height) ||
            !readUInt(values, "radial_segments", geometry.radialSegments) ||
            !readUInt(values, "height_segments", geometry.heightSegments) ||
            !readBool(values, "cap_bottom", geometry.capBottom) ||
            !readBool(values, "cap_top", geometry.capTop))
            return false;
        result = geometry;
    } else {
        return false;
    }
    return true;
}

bool validRuntimePrimitiveRecipe(const MeshBuildRecipe& recipe) {
    constexpr std::uint32_t maxSegments = 512;
    if (recipe.parts.empty() || recipe.parts.size() > 4096 ||
        recipe.vertexLayout < PrimitiveVertexLayout::Position ||
        recipe.vertexLayout > PrimitiveVertexLayout::PositionNormalTangentUv ||
        recipe.indexPolicy < MeshIndexPolicy::Auto ||
        recipe.indexPolicy > MeshIndexPolicy::UInt32 || recipe.usage < MeshUsage::Static ||
        recipe.usage > MeshUsage::Stream)
        return false;
    return std::ranges::all_of(recipe.parts, [](const MeshPrimitivePart& part) {
        if (!finite(part.translation) || !finite(part.rotation) || !finite(part.scale) ||
            math::lengthSquared(part.rotation) <= math::kEpsilon * math::kEpsilon ||
            std::abs(part.scale.x) <= math::kEpsilon || std::abs(part.scale.y) <= math::kEpsilon ||
            std::abs(part.scale.z) <= math::kEpsilon)
            return false;
        return std::visit(
            [](const auto& geometry) {
                using T = std::decay_t<decltype(geometry)>;
                const auto segments = [](std::uint32_t value, std::uint32_t minimum) {
                    return value >= minimum && value <= maxSegments;
                };
                if constexpr (std::is_same_v<T, PlaneGeometry>) {
                    return finite(geometry.size) && geometry.size.x > 0.0F &&
                           geometry.size.y > 0.0F && segments(geometry.segmentsX, 1) &&
                           segments(geometry.segmentsZ, 1);
                } else if constexpr (std::is_same_v<T, BoxGeometry>) {
                    return finite(geometry.size) && geometry.size.x > 0.0F &&
                           geometry.size.y > 0.0F && geometry.size.z > 0.0F &&
                           segments(geometry.segmentsX, 1) && segments(geometry.segmentsY, 1) &&
                           segments(geometry.segmentsZ, 1);
                } else if constexpr (std::is_same_v<T, UvSphereGeometry>) {
                    return finite(geometry.radius) && geometry.radius > 0.0F &&
                           segments(geometry.longitudeSegments, 3) &&
                           segments(geometry.latitudeSegments, 2);
                } else {
                    return finite(geometry.bottomRadius) && finite(geometry.topRadius) &&
                           finite(geometry.height) && geometry.bottomRadius >= 0.0F &&
                           geometry.topRadius >= 0.0F &&
                           (geometry.bottomRadius > math::kEpsilon ||
                            geometry.topRadius > math::kEpsilon) &&
                           geometry.height > 0.0F && segments(geometry.radialSegments, 3) &&
                           segments(geometry.heightSegments, 1);
                }
            },
            part.primitive.value);
    });
}

bool parseComponent(const VirtualPath& path, const Json& source, SceneComponentAsset& result) {
    if (!source.is_object())
        return false;
    const auto type = source.find("type");
    if (type == source.end() || !type->is_string())
        return false;
    const std::string& typeName = type->get_ref<const std::string&>();

    if (typeName == "Transform") {
        TransformComponentAsset value;
        if (!readVector(source, "position", value.position, 3) ||
            !readRotation(source, value.rotation) || !readVector(source, "scale", value.scale, 3))
            return false;
        result = value;
        return true;
    }
    if (typeName == "Mesh") {
        const auto mesh = source.find("mesh");
        const auto primitive = source.find("primitive");
        if ((mesh == source.end()) == (primitive == source.end()))
            return false;
        MeshComponentAsset value;
        if (mesh != source.end()) {
            if (!mesh->is_string())
                return false;
            value.sourceType = MeshComponentSourceType::Asset;
            value.mesh = readAssetPath(path, mesh->get_ref<const std::string&>());
        } else {
            MeshPrimitive parsed;
            if (!readPrimitive(*primitive, parsed))
                return false;
            value.sourceType = MeshComponentSourceType::Primitive;
            value.primitiveRecipe.name = "Scene Runtime Primitive";
            value.primitiveRecipe.parts.emplace_back(std::move(parsed));
            value.primitiveRecipe.usage = MeshUsage::Dynamic;
            value.primitiveRecipe.keepCpuCopy = true;
        }
        if (!readBool(source, "enabled", value.enabled) ||
            !readBool(source, "visible", value.visible) ||
            !readBool(source, "cast_shadow", value.castShadow) ||
            !readBool(source, "receive_shadow", value.receiveShadow) ||
            !readUInt(source, "layer_mask", value.layerMask))
            return false;
        result = std::move(value);
        return true;
    }
    if (typeName == "Material") {
        MaterialComponentAsset value;
        const auto materials = source.find("materials");
        if (materials == source.end() || !materials->is_array() ||
            !readBool(source, "enabled", value.enabled))
            return false;
        value.materials.reserve(materials->size());
        for (const Json& material : *materials) {
            if (!material.is_string())
                return false;
            value.materials.push_back(readAssetPath(path, material.get_ref<const std::string&>()));
        }
        result = std::move(value);
        return true;
    }
    if (typeName == "Camera") {
        CameraComponentAsset value;
        if (const auto projection = source.find("projection"); projection != source.end()) {
            if (!projection->is_string())
                return false;
            const std::string& name = projection->get_ref<const std::string&>();
            if (name == "perspective")
                value.projection = CameraProjection::Perspective;
            else if (name == "orthographic")
                value.projection = CameraProjection::Orthographic;
            else
                return false;
        }
        if (!readFloat(source, "field_of_view", value.fieldOfView) ||
            !readFloat(source, "orthographic_size", value.orthographicSize) ||
            !readFloat(source, "near_plane", value.nearPlane) ||
            !readFloat(source, "far_plane", value.farPlane) ||
            !readVector(source, "clear_color", value.clearColor, 4) ||
            !readUInt(source, "culling_mask", value.cullingMask) ||
            !readInt(source, "priority", value.priority) ||
            !readBool(source, "primary", value.primary) ||
            !readBool(source, "enabled", value.enabled))
            return false;
        result = value;
        return true;
    }
    if (typeName == "Light") {
        LightComponentAsset value;
        if (const auto lightType = source.find("light_type"); lightType != source.end()) {
            if (!lightType->is_string())
                return false;
            const std::string& name = lightType->get_ref<const std::string&>();
            if (name == "directional")
                value.type = LightType::Directional;
            else if (name == "point")
                value.type = LightType::Point;
            else if (name == "spot")
                value.type = LightType::Spot;
            else
                return false;
        }
        if (!readVector(source, "color", value.color, 3) ||
            !readFloat(source, "intensity", value.intensity) ||
            !readFloat(source, "range", value.range) ||
            !readFloat(source, "inner_spot_angle", value.innerSpotAngle) ||
            !readFloat(source, "outer_spot_angle", value.outerSpotAngle) ||
            !readBool(source, "cast_shadow", value.castShadow) ||
            !readUInt(source, "culling_mask", value.cullingMask) ||
            !readBool(source, "enabled", value.enabled))
            return false;
        result = value;
        return true;
    }
    return false;
}

} // namespace

bool transferSceneAsset(Transfer& archive, SceneAsset& value) {
    std::uint32_t magic = kSceneMagic;
    std::uint16_t version = kSceneBinaryVersion;
    return archive.transfer("magic", magic) && magic == kSceneMagic &&
           archive.transfer("version", version) && version == kSceneBinaryVersion &&
           archive.transfer("name", value.name) && archive.transfer("nodes", value.nodes);
}

namespace detail {

std::shared_ptr<SceneAsset> parseSceneAsset(const VirtualPath& path, std::string_view source) {
    const Json root = Json::parse(source, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        fail(path, "Invalid Scene JSON");
        return {};
    }
    const auto version = root.find("$schemaVersion");
    const auto name = root.find("name");
    const auto nodes = root.find("nodes");
    if (version == root.end() || !version->is_number_unsigned() ||
        version->get<std::uint32_t>() != kSceneJsonVersion || name == root.end() ||
        !name->is_string() || nodes == root.end() || !nodes->is_array() ||
        nodes->size() > kMaxNodes) {
        fail(path, "Scene header fields are missing or invalid");
        return {};
    }

    auto asset = std::make_shared<SceneAsset>();
    asset->name = name->get<std::string>();
    asset->nodes.reserve(nodes->size());
    for (const Json& sourceNode : *nodes) {
        if (!sourceNode.is_object()) {
            fail(path, "Scene node must be an object");
            return {};
        }
        const auto id = sourceNode.find("id");
        const auto nodeName = sourceNode.find("name");
        const auto components = sourceNode.find("components");
        if (id == sourceNode.end() || !id->is_number_unsigned() || nodeName == sourceNode.end() ||
            !nodeName->is_string() || components == sourceNode.end() || !components->is_array() ||
            components->size() > kMaxComponentsPerNode) {
            fail(path, "Scene node fields are missing or invalid");
            return {};
        }
        SceneNodeAsset node;
        node.id = id->get<SceneNodeAssetId>();
        node.name = nodeName->get<std::string>();
        if (!readBool(sourceNode, "active", node.active)) {
            fail(path, "Scene node active flag is invalid");
            return {};
        }
        if (const auto parent = sourceNode.find("parent");
            parent != sourceNode.end() && !parent->is_null()) {
            if (!parent->is_number_unsigned()) {
                fail(path, "Scene node parent is invalid");
                return {};
            }
            node.parent = parent->get<SceneNodeAssetId>();
        }
        node.components.reserve(components->size());
        for (const Json& sourceComponent : *components) {
            SceneComponentAsset component;
            if (!parseComponent(path, sourceComponent, component)) {
                fail(path, "Scene component is invalid or unsupported");
                return {};
            }
            node.components.push_back(std::move(component));
        }
        asset->nodes.push_back(std::move(node));
    }
    asset->setAssetPath(path);
    return validateSceneAsset(*asset, path) ? asset : nullptr;
}

} // namespace detail

bool validateSceneAsset(const SceneAsset& asset, const VirtualPath& scenePath) {
    if (asset.name.empty())
        return fail(scenePath, "Scene name is empty");
    if (asset.nodes.size() > kMaxNodes)
        return fail(scenePath, "Too many Scene nodes");

    std::unordered_map<SceneNodeAssetId, const SceneNodeAsset*> nodeById;
    nodeById.reserve(asset.nodes.size());
    for (const SceneNodeAsset& node : asset.nodes) {
        if (node.id == 0 || node.name.empty() || !nodeById.emplace(node.id, &node).second) {
            return fail(scenePath, "Scene node identity is invalid or duplicated");
        }
        if (node.parent && *node.parent == node.id) {
            return fail(scenePath, "Scene node cannot parent itself");
        }
        if (node.components.size() > kMaxComponentsPerNode) {
            return fail(scenePath, "Too many components on one Scene node");
        }
        std::array<bool, std::variant_size_v<SceneComponentAsset>> seen{};
        for (const SceneComponentAsset& component : node.components) {
            if (seen[component.index()]) {
                return fail(scenePath, "Scene node has a duplicate component type");
            }
            seen[component.index()] = true;
            const bool valid = std::visit(
                [](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, TransformComponentAsset>) {
                        const float lengthSquared = value.rotation.x * value.rotation.x +
                                                    value.rotation.y * value.rotation.y +
                                                    value.rotation.z * value.rotation.z +
                                                    value.rotation.w * value.rotation.w;
                        return finite(value.position) && finite(value.rotation) &&
                               finite(value.scale) && lengthSquared > math::kEpsilon;
                    } else if constexpr (std::is_same_v<T, MeshComponentAsset>) {
                        if (value.sourceType == MeshComponentSourceType::Asset) {
                            return value.mesh.valid() && value.mesh.scheme() == "asset" &&
                                   value.mesh.relativePath().ends_with(".mesh.json");
                        }
                        return value.sourceType == MeshComponentSourceType::Primitive &&
                               !value.mesh.valid() &&
                               validRuntimePrimitiveRecipe(value.primitiveRecipe);
                    } else if constexpr (std::is_same_v<T, MaterialComponentAsset>) {
                        return value.materials.size() <= kMaxMaterialsPerNode &&
                               std::ranges::all_of(value.materials, [](const VirtualPath& path) {
                                   return path.valid() && path.scheme() == "asset" &&
                                          path.relativePath().ends_with(".material.json");
                               });
                    } else if constexpr (std::is_same_v<T, CameraComponentAsset>) {
                        return (value.projection == CameraProjection::Perspective ||
                                value.projection == CameraProjection::Orthographic) &&
                               finite(value.fieldOfView) && value.fieldOfView > 0.0F &&
                               value.fieldOfView < 180.0F && finite(value.orthographicSize) &&
                               value.orthographicSize > 0.0F && finite(value.nearPlane) &&
                               value.nearPlane > 0.0F && finite(value.farPlane) &&
                               value.farPlane > value.nearPlane && finite(value.clearColor);
                    } else if constexpr (std::is_same_v<T, LightComponentAsset>) {
                        return value.type >= LightType::Directional &&
                               value.type <= LightType::Spot && finite(value.color) &&
                               finite(value.intensity) && value.intensity >= 0.0F &&
                               finite(value.range) && value.range > 0.0F &&
                               finite(value.innerSpotAngle) && value.innerSpotAngle >= 0.0F &&
                               finite(value.outerSpotAngle) &&
                               value.outerSpotAngle >= value.innerSpotAngle &&
                               value.outerSpotAngle < 180.0F;
                    }
                    return false;
                },
                component);
            if (!valid)
                return fail(scenePath, "Scene component data is invalid");
        }
        if (!seen[0]) {
            return fail(scenePath, "Scene node requires one Transform component");
        }
    }

    for (const SceneNodeAsset& node : asset.nodes) {
        if (node.parent && !nodeById.contains(*node.parent)) {
            return fail(scenePath, "Scene node parent does not exist");
        }
    }
    enum class Visit : std::uint8_t { None, Visiting, Finished };
    std::unordered_map<SceneNodeAssetId, Visit> visits;
    std::function<bool(SceneNodeAssetId)> visit = [&](SceneNodeAssetId id) {
        Visit& state = visits[id];
        if (state == Visit::Visiting)
            return false;
        if (state == Visit::Finished)
            return true;
        state = Visit::Visiting;
        const SceneNodeAsset& node = *nodeById.at(id);
        if (node.parent && !visit(*node.parent))
            return false;
        state = Visit::Finished;
        return true;
    };
    for (const SceneNodeAsset& node : asset.nodes) {
        if (!visit(node.id))
            return fail(scenePath, "Scene hierarchy contains a cycle");
    }
    return true;
}

bool SceneAsset::transfer(Transfer& archive) {
    SceneAsset decoded;
    decoded.setAssetPath(assetPath());
    SceneAsset& target = archive.reading() ? decoded : *this;
    if ((archive.writing() && !validateSceneAsset(*this, assetPath())) ||
        !archive.beginObject({}) || !transferSceneAsset(archive, target) || !archive.endObject() ||
        (archive.reading() && !validateSceneAsset(decoded, assetPath()))) {
        return fail(assetPath(), "Invalid SceneAsset contents");
    }
    if (archive.reading()) {
        name = std::move(decoded.name);
        nodes = std::move(decoded.nodes);
    }
    return true;
}

std::unique_ptr<Scene> SceneAsset::instantiate(const SceneInstantiationContext& context) const {
    if (!validateSceneAsset(*this, assetPath())) {
        fail(assetPath(), "Scene cannot be instantiated");
        return {};
    }

    auto scene = std::make_unique<Scene>(name);
    std::unordered_map<SceneNodeAssetId, NodeHandle> handles;
    handles.reserve(nodes.size());

    for (const SceneNodeAsset& source : nodes) {
        const NodeHandle handle = scene->createNode(source.name);
        Node* node = scene->findNode(handle);
        if (!node || !handles.emplace(source.id, handle).second)
            return {};
        node->setActive(source.active);

        for (const SceneComponentAsset& component : source.components) {
            const bool succeeded = std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, TransformComponentAsset>) {
                        node->transform().setLocalPosition(value.position);
                        node->transform().setLocalRotation(value.rotation);
                        node->transform().setLocalScale(value.scale);
                    } else if constexpr (std::is_same_v<T, MeshComponentAsset>) {
                        MeshComponent* runtime = node->addComponent<MeshComponent>();
                        if (value.sourceType == MeshComponentSourceType::Asset) {
                            if (!context.loadMesh)
                                return false;
                            const MeshHandle mesh = context.loadMesh(value.mesh);
                            if (!mesh)
                                return false;
                            runtime->setAssetMesh(mesh);
                        } else {
                            runtime->setPrimitiveRecipe(value.primitiveRecipe);
                            if (!runtime->applyPrimitiveChanges())
                                return false;
                        }
                        runtime->visible = value.visible;
                        runtime->castShadow = value.castShadow;
                        runtime->receiveShadow = value.receiveShadow;
                        runtime->layerMask = value.layerMask;
                        runtime->setEnabled(value.enabled);
                    } else if constexpr (std::is_same_v<T, MaterialComponentAsset>) {
                        MaterialComponent* runtime = node->addComponent<MaterialComponent>();
                        if (!value.materials.empty() && !context.loadMaterial)
                            return false;
                        for (std::size_t slot = 0; slot < value.materials.size(); ++slot) {
                            const MaterialHandle material =
                                context.loadMaterial(value.materials[slot]);
                            if (!material)
                                return false;
                            runtime->setMaterial(static_cast<std::uint32_t>(slot), material);
                        }
                        runtime->setEnabled(value.enabled);
                    } else if constexpr (std::is_same_v<T, CameraComponentAsset>) {
                        CameraComponent* runtime = node->addComponent<CameraComponent>();
                        runtime->projection = value.projection;
                        runtime->fieldOfView = value.fieldOfView;
                        runtime->orthographicSize = value.orthographicSize;
                        runtime->nearPlane = value.nearPlane;
                        runtime->farPlane = value.farPlane;
                        runtime->clearColor = value.clearColor;
                        runtime->cullingMask = value.cullingMask;
                        runtime->priority = value.priority;
                        runtime->primary = value.primary;
                        runtime->setEnabled(value.enabled);
                    } else if constexpr (std::is_same_v<T, LightComponentAsset>) {
                        LightComponent* runtime = node->addComponent<LightComponent>();
                        runtime->type = value.type;
                        runtime->color = value.color;
                        runtime->intensity = value.intensity;
                        runtime->range = value.range;
                        runtime->innerSpotAngle = value.innerSpotAngle;
                        runtime->outerSpotAngle = value.outerSpotAngle;
                        runtime->castShadow = value.castShadow;
                        runtime->cullingMask = value.cullingMask;
                        runtime->setEnabled(value.enabled);
                    }
                    return true;
                },
                component);
            if (!succeeded) {
                fail(assetPath(), "Cannot load a Scene component resource");
                return {};
            }
        }
    }

    for (const SceneNodeAsset& source : nodes) {
        if (!source.parent)
            continue;
        Node* node = scene->findNode(handles.at(source.id));
        const auto parent = handles.find(*source.parent);
        if (!node || parent == handles.end() || !node->setParent(parent->second)) {
            fail(assetPath(), "Cannot instantiate Scene hierarchy");
            return {};
        }
    }
    scene->updateTransforms();
    return scene;
}

} // namespace engine

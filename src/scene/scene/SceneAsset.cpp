#include "scene/scene/SceneAsset.h"

#include "asset/format/SceneAssetFormat.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

#include <type_traits>
#include <unordered_map>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kSceneMagic = 0x454e4353U;
constexpr std::uint16_t kSceneBinaryVersion = 3;

bool fail(const VirtualPath& path, std::string_view message) {
    Log::error("SceneAsset",
               "%s: %.*s",
               path.string().c_str(),
               static_cast<int>(message.size()),
               message.data());
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

bool SceneAsset::transfer(Transfer& archive) {
    SceneAsset decoded;
    decoded.setAssetPath(assetPath());
    SceneAsset& target = archive.reading() ? decoded : *this;
    if ((archive.writing() && !format::validateSceneAsset(*this, assetPath())) ||
        !archive.beginObject({}) || !transferSceneAsset(archive, target) || !archive.endObject() ||
        (archive.reading() && !format::validateSceneAsset(decoded, assetPath()))) {
        return fail(assetPath(), "Invalid SceneAsset contents");
    }
    if (archive.reading()) {
        name = std::move(decoded.name);
        nodes = std::move(decoded.nodes);
    }
    return true;
}

std::unique_ptr<Scene> SceneAsset::instantiate(const SceneInstantiationContext& context) const {
    if (!format::validateSceneAsset(*this, assetPath())) {
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

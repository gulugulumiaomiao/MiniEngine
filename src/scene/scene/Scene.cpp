#include "scene/scene/Scene.h"

#include "core/logging/Log.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/scene/RenderScene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

#include <algorithm>
#include <vector>

namespace engine {

Scene::Scene(std::string name) : name_(std::move(name)) {
    Node rootNode{*this, "Root"};
    root_ = nodes_.insert(std::move(rootNode));
    nodes_.find(root_)->initialize(root_);
}

Scene::~Scene() {
    clear();
    if (Node* rootNode = nodes_.find(root_))
        rootNode->detachComponents();
    (void)nodes_.release(root_);
}

NodeHandle Scene::createNode(std::string name) {
    Node node{*this, std::move(name)};
    const NodeHandle handle = nodes_.insert(std::move(node));
    Node* created = nodes_.find(handle);
    created->initialize(handle);
    created->parent_ = root_;
    nodes_.find(root_)->children_.push_back(handle);
    return handle;
}

bool Scene::destroyNode(NodeHandle handle) {
    if (handle == root_) {
        Log::warn("Scene", "Scene root cannot be destroyed");
        return false;
    }
    Node* node = nodes_.find(handle);
    if (!node)
        return false;

    const std::vector<NodeHandle> children = node->children_;
    for (NodeHandle child : children) {
        (void)destroyNode(child);
    }

    if (Node* parent = nodes_.find(node->parent_)) {
        std::erase(parent->children_, handle);
    }
    node->detachComponents();
    return nodes_.release(handle);
}

void Scene::clear() {
    Node* rootNode = nodes_.find(root_);
    if (!rootNode)
        return;
    const std::vector<NodeHandle> children = rootNode->children_;
    for (NodeHandle child : children)
        (void)destroyNode(child);
}

void Scene::update(float deltaTime) {
    Node* rootNode = nodes_.find(root_);
    if (!rootNode)
        return;
    rootNode->updateComponentsSubtree(deltaTime);
    updateTransforms();
}

void Scene::updateTransforms() {
    if (Node* rootNode = nodes_.find(root_)) {
        rootNode->updateTransformSubtree(math::Mat44{1.0F}, false);
    }
}

void Scene::buildRenderScene(RenderScene& output, float aspectRatio) {
    output.clear();
    updateTransforms();
    if (Node* rootNode = nodes_.find(root_)) {
        extractRenderNode(*rootNode, output, aspectRatio > math::kEpsilon ? aspectRatio : 1.0F);
    }
}

void Scene::extractRenderNode(Node& node, RenderScene& output, float aspectRatio) {
    if (!node.activeInHierarchy_)
        return;

    const math::Mat44& world = node.transform_->worldMatrix_;
    if (const CameraComponent* camera = node.getComponent<CameraComponent>();
        camera && camera->active()) {
        RenderCamera candidate{
            .view = math::inverse(world),
            .projection = camera->projectionMatrix(aspectRatio),
            .worldPosition = node.transform_->worldPosition(),
            .clearColor = camera->clearColor,
            .cullingMask = camera->cullingMask,
            .priority = camera->priority,
            .primary = camera->primary,
        };
        const auto& selected = output.camera();
        if (!selected || (candidate.primary && !selected->primary) ||
            (candidate.primary == selected->primary && candidate.priority > selected->priority)) {
            output.setCamera(std::move(candidate));
        }
    }

    if (const LightComponent* light = node.getComponent<LightComponent>();
        light && light->active()) {
        output.submit({
            .type = light->type,
            .position = node.transform_->worldPosition(),
            .direction = math::normalize(math::transformVector(world, {0.0F, 0.0F, -1.0F}),
                                         math::Vec3{0.0F, 0.0F, -1.0F}),
            .color = light->color,
            .intensity = light->intensity,
            .range = light->range,
            .innerSpotAngle = light->innerSpotAngle,
            .outerSpotAngle = light->outerSpotAngle,
            .castShadow = light->castShadow,
            .cullingMask = light->cullingMask,
        });
    }

    const MeshComponent* mesh = node.getComponent<MeshComponent>();
    const MaterialComponent* material = node.getComponent<MaterialComponent>();
    if (mesh && material && mesh->active() && material->active() && mesh->visible && mesh->mesh()) {
        // The world-space bounds radius feeds shadow-volume fitting and frustum culling; scaling
        // the local bounding sphere by the longest transform axis keeps it conservative under
        // non-uniform scale.
        float boundsRadius = 0.0F;
        if (const Mesh* meshInstance = MESH_MANAGER.find(mesh->mesh())) {
            float maxAxisScale = 0.0F;
            for (std::uint32_t column = 0; column < 3; ++column) {
                maxAxisScale = std::max(maxAxisScale, math::length(math::Vec3(world[column])));
            }
            boundsRadius = meshInstance->desc().bounds.sphere.radius * maxAxisScale;
        }
        output.submit({
            .mesh = mesh->mesh(),
            .materials = material->materials(),
            .transform = world,
            .layerMask = mesh->layerMask,
            .boundsRadius = boundsRadius,
            .castShadow = mesh->castShadow,
            .receiveShadow = mesh->receiveShadow,
        });
    }

    for (NodeHandle child : node.children_) {
        if (Node* childNode = nodes_.find(child)) {
            extractRenderNode(*childNode, output, aspectRatio);
        }
    }
}

} // namespace engine

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

#if defined(MINI_EDITOR)
namespace {

struct NodeMove {
    Node* node{};
    Node* parent{};
    std::size_t oldIndex{};
    bool sameParent{};
    math::Vec3 position{};
    math::Quat rotation{};
    math::Vec3 scale{};
};

bool matrixNear(const math::Mat44& a, const math::Mat44& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const float x = a[column][row], y = b[column][row];
            if (!std::isfinite(x) || !std::isfinite(y) ||
                std::abs(x - y) > 1.0e-5F + 1.0e-5F * std::max(std::abs(x), std::abs(y)))
                return false;
        }
    }
    return true;
}

// 不做剪切近似：分解后必须能重组出相同的矩阵，负缩放通过反射轴保留。
bool decomposeLocal(const math::Mat44& matrix, NodeMove& move) {
    if (!matrixNear(matrix, matrix))
        return false;
    math::Mat33 axes{matrix};
    for (int column = 0; column < 3; ++column) {
        move.scale[column] = math::length(axes[column]);
        if (!std::isfinite(move.scale[column]) || move.scale[column] <= math::kEpsilon)
            return false;
        axes[column] /= move.scale[column];
    }
    if (std::abs(math::dot(axes[0], axes[1])) > 1.0e-5F ||
        std::abs(math::dot(axes[0], axes[2])) > 1.0e-5F ||
        std::abs(math::dot(axes[1], axes[2])) > 1.0e-5F)
        return false;
    if (glm::determinant(axes) < 0.0F) {
        axes[0] = -axes[0];
        move.scale.x = -move.scale.x;
    }
    move.position = math::Vec3{matrix[3]};
    move.rotation = math::normalize(glm::quat_cast(axes));
    return matrixNear(math::trs(move.position, move.rotation, move.scale), matrix);
}

bool prepareNodeMove(Scene& scene,
                     NodeHandle handle,
                     NodeHandle parentHandle,
                     std::size_t finalIndex,
                     NodeMove& move,
                     std::string& error) {
    error.clear();
    move.node = scene.findNode(handle);
    move.parent = scene.findNode(parentHandle);
    if (!move.node || !move.parent || handle == scene.rootHandle()) {
        error = "Invalid node or parent; Scene Root cannot be moved.";
        return false;
    }
    for (const Node* ancestor = move.parent; ancestor;
         ancestor = scene.findNode(ancestor->parent())) {
        if (ancestor == move.node) {
            error = "A node cannot be moved under itself or its descendants.";
            return false;
        }
    }
    const Node* oldParent = scene.findNode(move.node->parent());
    if (!oldParent) {
        error = "The source node has no valid parent.";
        return false;
    }
    const auto& oldChildren = oldParent->children();
    const auto found = std::ranges::find(oldChildren, handle);
    if (found == oldChildren.end()) {
        error = "The source node is missing from its parent's children.";
        return false;
    }
    move.oldIndex = static_cast<std::size_t>(found - oldChildren.begin());
    move.sameParent = oldParent == move.parent;
    const std::size_t count = move.parent->children().size() - (move.sameParent ? 1U : 0U);
    if (finalIndex > count) {
        error = "The insertion index is out of range.";
        return false;
    }
    if (move.sameParent)
        return true;

    scene.updateTransforms();
    const math::Mat44 world = move.node->transform().worldMatrix();
    const math::Mat44 parentWorld = move.parent->transform().worldMatrix();
    math::Mat33 normalizedAxes{parentWorld};
    for (int column = 0; column < 3; ++column) {
        const float length = math::length(normalizedAxes[column]);
        if (!std::isfinite(length) || length <= math::kEpsilon) {
            error = "Cannot preserve world transform: parent has a zero or invalid scale.";
            return false;
        }
        normalizedAxes[column] /= length;
    }
    const math::Mat44 inverseParent = math::inverse(parentWorld);
    if (std::abs(glm::determinant(normalizedAxes)) <= math::kEpsilon ||
        !matrixNear(parentWorld * inverseParent, math::Mat44{1.0F})) {
        error = "Cannot preserve world transform: parent transform is singular or unstable.";
        return false;
    }
    if (!decomposeLocal(inverseParent * world, move) ||
        !matrixNear(parentWorld * math::trs(move.position, move.rotation, move.scale), world)) {
        error =
            "Cannot preserve world transform: local TRS would require shear or a degenerate scale.";
        return false;
    }
    return true;
}

} // namespace

bool Scene::canMoveNode(NodeHandle node,
                        NodeHandle parent,
                        std::size_t finalIndex,
                        std::string& error) {
    NodeMove move;
    return prepareNodeMove(*this, node, parent, finalIndex, move, error);
}

NodeMoveResult Scene::moveNode(NodeHandle handle,
                               NodeHandle parentHandle,
                               std::size_t finalIndex,
                               std::string& error) {
    NodeMove move;
    if (!prepareNodeMove(*this, handle, parentHandle, finalIndex, move, error))
        return NodeMoveResult::Rejected;
    if (move.sameParent && move.oldIndex == finalIndex)
        return NodeMoveResult::Unchanged;

    auto& destination = move.parent->children_;
    // 在任何修改之前完成分配；后续句柄搬移不会分配或抛出异常。
    if (!move.sameParent)
        destination.reserve(destination.size() + 1);
    auto& source = findNode(move.node->parent_)->children_;
    source.erase(source.begin() + static_cast<std::ptrdiff_t>(move.oldIndex));
    destination.insert(destination.begin() + static_cast<std::ptrdiff_t>(finalIndex), handle);
    if (!move.sameParent) {
        move.node->parent_ = parentHandle;
        move.node->transform().setLocalPosition(move.position);
        move.node->transform().setLocalRotation(move.rotation);
        move.node->transform().setLocalScale(move.scale);
        move.node->markTransformDirty();
        move.node->refreshActiveSubtree(move.parent->activeInHierarchy_);
    }
    return NodeMoveResult::Changed;
}
#endif

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

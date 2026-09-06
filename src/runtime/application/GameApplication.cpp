#include "runtime/application/GameApplication.h"

#include "core/base/BuildConfig.h"
#include "runtime/engine/Engine.h"
#include "render/mesh/Mesh.h"
#include "render/renderer/Renderer.h"
#include "render/mesh/Vertex.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

#include <array>
#include <cstddef>
#include <span>

namespace engine {

AppConfig GameApplication::getConfig() const {
    return {
        .name = std::string{build::kWindowTitle},
        .width = 1280,
        .height = 720,
        .vsync = true,
    };
}

void GameApplication::onStart() {
    constexpr std::array vertices{
        Vertex{{0.00F, -0.65F}, {1.0F, 0.2F, 0.2F}},
        Vertex{{0.65F, 0.65F}, {0.2F, 1.0F, 0.3F}},
        Vertex{{-0.65F, 0.65F}, {0.2F, 0.4F, 1.0F}},
    };
    constexpr std::array<std::uint16_t, 3> indices{0, 1, 2};
    constexpr std::array positions{
        math::Vec3{0.00F, -0.65F, 0.0F},
        math::Vec3{0.65F, 0.65F, 0.0F},
        math::Vec3{-0.65F, 0.65F, 0.0F},
    };

    const MeshBounds bounds = calculateBounds(positions);
    MeshDesc meshDesc;
    meshDesc.debugName = "Triangle";
    meshDesc.vertexLayout = {
        {{0, sizeof(Vertex), VertexInputRate::Vertex}},
        {{{VertexSemanticType::Position, 0}, VertexFormat::Vec2Float32, 0, 0,
          static_cast<std::uint32_t>(offsetof(Vertex, position))},
         {{VertexSemanticType::Color, 0}, VertexFormat::Vec3Float32, 1, 0,
          static_cast<std::uint32_t>(offsetof(Vertex, color))}},
    };
    meshDesc.indexType = IndexType::UInt16;
    meshDesc.bounds = bounds;
    meshDesc.subMeshes.push_back(
        {0, static_cast<std::uint32_t>(indices.size()), 0, 0, bounds});
    MeshData meshData;
    (void)meshData.setVertexData(0, std::span{vertices});
    (void)meshData.setIndexData(std::span{indices});

    Renderer& renderer = ENGINE.renderer();
    triangle_ = renderer.createMesh(meshDesc, meshData);
    warmMaterial_ = renderer.loadMaterial(
        VirtualPath{"asset://materials/warm_vertex_color.material.json"});
    coolMaterial_ = renderer.loadMaterial(
        VirtualPath{"asset://materials/cool_vertex_color.material.json"});

    Scene& scene = ENGINE.scene();
    warmNode_ = scene.createNode("Warm Triangle");
    Node& warmNode = *scene.findNode(warmNode_);
    warmNode.addComponent<MeshComponent>()->mesh = triangle_;
    warmNode.addComponent<MaterialComponent>()->setMaterial(0, warmMaterial_);
    warmNode.transform().setLocalPosition({-0.38F, 0.0F, 0.0F});
    warmNode.transform().setLocalScale({0.65F, 0.65F, 1.0F});

    coolNode_ = scene.createNode("Cool Triangle");
    Node& coolNode = *scene.findNode(coolNode_);
    coolNode.addComponent<MeshComponent>()->mesh = triangle_;
    coolNode.addComponent<MaterialComponent>()->setMaterial(0, coolMaterial_);
    coolNode.transform().setLocalPosition({0.38F, 0.0F, 0.0F});
    coolNode.transform().setLocalScale({0.65F, 0.65F, 1.0F});

    cameraNode_ = scene.createNode("Main Camera");
    Node& cameraNode = *scene.findNode(cameraNode_);
    cameraNode.addComponent<CameraComponent>()->primary = true;
    cameraNode.transform().setLocalPosition({0.0F, 0.0F, 2.0F});

    lightNode_ = scene.createNode("Directional Light");
    Node& lightNode = *scene.findNode(lightNode_);
    LightComponent* light = lightNode.addComponent<LightComponent>();
    light->type = LightType::Directional;
    light->color = {1.0F, 1.0F, 1.0F};
    light->intensity = 1.0F;
}

void GameApplication::onUpdate(float deltaTime) {
    (void)deltaTime;
}

void GameApplication::onStop() {
    ENGINE.scene().clear();
    ENGINE.renderScene().clear();
    Renderer& renderer = ENGINE.renderer();
    renderer.destroyMaterial(warmMaterial_);
    renderer.destroyMaterial(coolMaterial_);
    renderer.destroyMesh(triangle_);
    warmMaterial_ = {};
    coolMaterial_ = {};
    triangle_ = {};
    warmNode_ = {};
    coolNode_ = {};
    cameraNode_ = {};
    lightNode_ = {};
}

} // namespace engine

#include "core/Application.h"

#include "renderer/Mesh.h"
#include "renderer/Vertex.h"

#include <array>
#include <cstddef>
#include <span>

namespace engine {

Application::Application() {
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
        sizeof(Vertex),
        {{VertexSemantic::Position, VertexFormat::Vec2Float32,
          static_cast<std::uint32_t>(offsetof(Vertex, position)), 0},
         {VertexSemantic::Color0, VertexFormat::Vec3Float32,
          static_cast<std::uint32_t>(offsetof(Vertex, color)), 1}},
    };
    meshDesc.indexType = IndexType::UInt16;
    meshDesc.bounds = bounds;
    meshDesc.subMeshes.push_back({0, static_cast<std::uint32_t>(indices.size()), 0, 0, bounds});
    const MeshData meshData{
        std::as_bytes(std::span{vertices}), std::as_bytes(std::span{indices}),
        static_cast<std::uint32_t>(vertices.size()),
        static_cast<std::uint32_t>(indices.size()),
    };
    const MeshHandle triangle = renderer_.createMesh(meshDesc, meshData);
    const MaterialHandle warmMaterial = renderer_.loadMaterial(
        "materials/warm_vertex_color.material.json");
    const MaterialHandle coolMaterial = renderer_.loadMaterial(
        "materials/cool_vertex_color.material.json");
    const math::Mat44 leftTransform = math::trs(
        {-0.38F, 0.0F, 0.0F}, math::Quat{1.0F, 0.0F, 0.0F, 0.0F},
        {0.65F, 0.65F, 1.0F});
    const math::Mat44 rightTransform = math::trs(
        {0.38F, 0.0F, 0.0F}, math::Quat{1.0F, 0.0F, 0.0F, 0.0F},
        {0.65F, 0.65F, 1.0F});
    scene_.submit({triangle, warmMaterial, leftTransform});
    scene_.submit({triangle, coolMaterial, rightTransform});
}

void Application::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();
        renderer_.renderFrame(scene_);
    }
    renderer_.waitIdle();
}

} // namespace engine

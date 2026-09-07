#pragma once

#include "rhi/api/RhiTypes.h"
#include "render/renderer/RenderResources.h"

#include <cstddef>
#include <vector>

namespace engine {

class ShaderPass;

enum class RenderPhase { Forward, DepthOnly, ShadowCaster };

struct SceneDrawData {
    math::Mat44 viewProjection{1.0F};
    math::Vec4 cameraPosition{0.0F};
    math::Vec4 directionalLightDirection{0.0F, 0.0F, -1.0F, 0.0F};
    math::Vec4 directionalLightColorIntensity{1.0F};
    math::Vec4 pointLightPositionRange{0.0F};
    math::Vec4 pointLightColorIntensity{0.0F};
};
static_assert(sizeof(SceneDrawData) == 144,
              "SceneDrawData must match the std140 shader layout");
static_assert(offsetof(SceneDrawData, cameraPosition) == 64);
static_assert(offsetof(SceneDrawData, directionalLightDirection) == 80);
static_assert(offsetof(SceneDrawData, directionalLightColorIntensity) == 96);
static_assert(offsetof(SceneDrawData, pointLightPositionRange) == 112);
static_assert(offsetof(SceneDrawData, pointLightColorIntensity) == 128);

struct ObjectDrawData {
    math::Mat44 transform;
};
static_assert(sizeof(ObjectDrawData) == 64,
              "ObjectDrawData must match the std430 shader layout");

struct DrawItem {
    const ShaderPass* shaderPass{};
    RenderPhase renderPhase{RenderPhase::Forward};
    rhi::GraphicsPipelineHandle pipeline;
    MaterialHandle material;
    rhi::BindGroupHandle materialBindGroup;
    struct VertexBuffer {
        std::uint32_t binding{};
        rhi::BufferHandle buffer;
    };
    std::vector<VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    rhi::IndexFormat indexFormat{rhi::IndexFormat::UInt32};
    rhi::DrawIndexedArguments arguments;
    int renderQueue{2000};
};

struct DrawList {
    std::vector<DrawItem> items;
    std::vector<ObjectDrawData> objects;
    SceneDrawData scene;
    math::Vec4 clearColor{0.025F, 0.055F, 0.10F, 1.0F};
};

struct MeshDrawInfo {
    std::vector<DrawItem::VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    rhi::IndexFormat indexFormat{rhi::IndexFormat::UInt32};
    struct Range {
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        std::int32_t vertexOffset{};
        std::uint32_t materialSlot{};
    };
    std::vector<Range> subMeshes;
};

} // namespace engine

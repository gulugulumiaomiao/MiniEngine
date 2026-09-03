#pragma once

#include "rhi/RhiTypes.h"
#include "renderer/RenderResources.h"

#include <vector>

namespace engine {

class ShaderPass;

enum class RenderPhase { Forward, DepthOnly, ShadowCaster };

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
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    rhi::IndexFormat indexFormat{rhi::IndexFormat::UInt32};
    rhi::DrawIndexedArguments arguments;
    int renderQueue{2000};
};

struct DrawList {
    std::vector<DrawItem> items;
    std::vector<ObjectDrawData> objects;
};

struct MeshDrawInfo {
    rhi::BufferHandle vertexBuffer;
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

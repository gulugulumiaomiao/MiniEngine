#pragma once

#include "core/math/Math.h"
#include "rhi/api/PipelineDesc.h"
#include "rhi/api/RhiTypes.h"
#include "render/base/RenderHandle.h"
#include <cstdint>
#include <vector>

namespace engine {

struct IndexRange {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::int32_t vertexOffset{};
};

// CPU-side input extracted from one renderable node and one SubMesh. MeshHandle keeps
// source geometry available for static batching; GPU handles are resolved later.
struct SourceDrawItem {
    MeshHandle mesh;
    struct VertexBuffer {
        std::uint32_t binding{};
        rhi::BufferHandle buffer;
    };
    std::vector<VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    IndexRange indexRange;
    MaterialHandle material;
    math::Mat44 worldMatrix{1.0F};
    std::uint32_t layerMask{1};
    std::uint32_t objectIndex{};
    bool castShadow{true};
};

using SourceDrawItemList = std::vector<SourceDrawItem>;
struct RenderItem {
    int renderQueue{2000};
    rhi::GraphicsPipelineHandle pipeline;
    rhi::DrawStateDesc drawState;
    struct VertexBuffer {
        std::uint32_t binding{};
        rhi::BufferHandle buffer;
    };
    std::vector<VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    rhi::IndexFormat indexFormat{rhi::IndexFormat::UInt32};
    rhi::BindGroupHandle materialBindGroup;
    rhi::DrawIndexedArguments arguments;
};

using RenderItemList = std::vector<RenderItem>;

} // namespace engine

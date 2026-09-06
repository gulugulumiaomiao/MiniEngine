#pragma once

#include "render/renderer/DrawList.h"
#include "render/renderer/RenderResources.h"

#include <span>

namespace engine {

struct MeshData;
struct MeshDesc;
class ShaderPass;
class Shader;
class Mesh;
struct VertexLayout;
struct ShaderVariantKey;

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    [[nodiscard]] virtual MeshDrawInfo prepareMesh(MeshHandle handle,
                                                   Mesh& mesh) = 0;
    virtual void releaseMesh(MeshHandle handle) = 0;
    [[nodiscard]] virtual rhi::GraphicsPipelineHandle pipelineForPass(
        const Shader& shader, const ShaderPass& pass,
        const ShaderVariantKey& variant, const VertexLayout& vertexLayout) = 0;
    virtual void renderFrame(const DrawList& drawList) = 0;
    virtual void waitIdle() = 0;
};

} // namespace engine

#pragma once

#include "renderer/DrawList.h"
#include "renderer/RenderResources.h"

#include <span>

namespace engine {

struct MeshData;
struct MeshDesc;
class ShaderPass;
class Shader;
struct ShaderVariantKey;

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    [[nodiscard]] virtual MeshHandle createMesh(const MeshDesc& desc,
                                                const MeshData& data) = 0;
    virtual void destroyMesh(MeshHandle handle) = 0;
    [[nodiscard]] virtual MeshDrawInfo meshDrawInfo(MeshHandle handle) const = 0;
    [[nodiscard]] virtual rhi::GraphicsPipelineHandle pipelineForPass(
        const Shader& shader, const ShaderPass& pass,
        const ShaderVariantKey& variant) = 0;
    virtual void renderFrame(const DrawList& drawList) = 0;
    virtual void waitIdle() = 0;
};

} // namespace engine

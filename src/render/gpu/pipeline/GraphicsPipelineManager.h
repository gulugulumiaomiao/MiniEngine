#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/pipeline/GraphicsPipelineCache.h"
#include "render/gpu/pipeline/GraphicsPipelineGpuResource.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {

class GraphicsPipelineGpuFactory;

namespace rhi {
class IDevice;
}

class GraphicsPipelineManager final : public Singleton<GraphicsPipelineManager> {
public:
    ~GraphicsPipelineManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device,
                                  rhi::BindGroupLayoutHandle sceneLayout,
                                  rhi::BindGroupLayoutHandle materialLayout);
    [[nodiscard]] rhi::GraphicsPipelineHandle resolve(const Shader& shader,
                                                      const ShaderPass& pass,
                                                      const ShaderVariantKey& variant,
                                                      const Mesh& mesh,
                                                      rhi::TextureFormat colorFormat,
                                                      rhi::TextureFormat depthFormat);
    void refreshShaders(std::uint64_t frameSerial, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void clear();
    void shutdown();
    [[nodiscard]] bool initialized() const { return factory_ != nullptr; }

private:
    struct RetiredPipeline {
        GraphicsPipelineGpuResource resource;
        std::uint64_t serial{};
    };

    friend class Singleton<GraphicsPipelineManager>;
    GraphicsPipelineManager();

    [[nodiscard]] static GraphicsPipelineCacheKey makeCacheKey(const ShaderProgram& program,
                                                               const ShaderPass& pass,
                                                               std::uint64_t vertexLayoutHash,
                                                               rhi::TextureFormat colorFormat,
                                                               rhi::TextureFormat depthFormat);
    [[nodiscard]] rhi::GraphicsPipelineDesc makeDescription(const ShaderPass& pass,
                                                            const VertexLayout& vertexLayout,
                                                            rhi::TextureFormat colorFormat,
                                                            rhi::TextureFormat depthFormat,
                                                            rhi::ShaderHandle vertexShader,
                                                            std::string vertexEntry,
                                                            rhi::ShaderHandle fragmentShader,
                                                            std::string fragmentEntry) const;
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);

    rhi::BindGroupLayoutHandle sceneLayout_;
    rhi::BindGroupLayoutHandle materialLayout_;
    GraphicsPipelineCache cache_;
    std::unique_ptr<GraphicsPipelineGpuFactory> factory_;
    std::vector<RetiredPipeline> retired_;
    std::uint64_t lastShaderPollSerial_{~std::uint64_t{}};
};

} // namespace engine

#define GRAPHICS_PIPELINE_MANAGER (::engine::GraphicsPipelineManager::instance())

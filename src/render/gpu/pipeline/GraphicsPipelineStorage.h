#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageCache.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageEntry.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {

class GraphicsPipelineStorageFactory;

namespace rhi {
class IDevice;
}

class GraphicsPipelineStorage final : public Singleton<GraphicsPipelineStorage> {
public:
    ~GraphicsPipelineStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device,
                                  rhi::RID sceneLayout,
                                  rhi::RID materialLayout,
                                  rhi::RID globalLayout);
    [[nodiscard]] rhi::RID resolve(const Shader& shader,
                                   const ShaderPass& pass,
                                   const ShaderVariantKey& variant,
                                   const Mesh& mesh,
                                   rhi::PixelFormat colorFormat,
                                   rhi::PixelFormat depthFormat);
    void refreshShaders(std::uint64_t frameSerial, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void clear();
    void shutdown();
    [[nodiscard]] bool initialized() const { return factory_ != nullptr; }

private:
    struct RetiredPipeline {
        GraphicsPipelineStorageEntry resource;
        std::uint64_t serial{};
    };

    friend class Singleton<GraphicsPipelineStorage>;
    GraphicsPipelineStorage();

    [[nodiscard]] static GraphicsPipelineStorageCacheKey
    makeCacheKey(const ShaderProgram& program,
                 std::uint64_t vertexLayoutHash,
                 rhi::PixelFormat colorFormat,
                 rhi::PixelFormat depthFormat);
    [[nodiscard]] rhi::GraphicsPipelineDesc makeDescription(const VertexLayout& vertexLayout,
                                                            rhi::PixelFormat colorFormat,
                                                            rhi::PixelFormat depthFormat,
                                                            rhi::RID vertexShader,
                                                            std::string vertexEntry,
                                                            rhi::RID fragmentShader,
                                                            std::string fragmentEntry) const;
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);

    rhi::RID sceneLayout_;
    rhi::RID materialLayout_;
    rhi::RID globalLayout_;
    GraphicsPipelineStorageCache cache_;
    std::unique_ptr<GraphicsPipelineStorageFactory> factory_;
    std::vector<RetiredPipeline> retired_;
    std::uint64_t lastShaderPollSerial_{~std::uint64_t{}};
};

} // namespace engine

#define GRAPHICS_PIPELINE_STORAGE (::engine::GraphicsPipelineStorage::instance())

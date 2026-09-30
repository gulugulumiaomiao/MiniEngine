#pragma once

#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageCache.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageEntry.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "render/shader/ShaderProgram.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine {

class GraphicsPipelineStorageFactory;

namespace rhi {
class IDevice;
}

// Draw-item-stage GPU store. It owns the engine's ShaderCompilePipeline (absorbing the former
// ShaderStorage role), produces and caches Ref<ShaderProgram> (vs/fs modules + reflected set
// layouts + pipeline layout), and caches the VkPipeline per (program, vertex layout, RT formats).
// Graphics-pipeline creation lives here rather than in ShaderProgram because it needs draw-context
// state (mesh vertex layout, render-target formats) unknown at shader-compile time.
class GraphicsPipelineStorage final : public Singleton<GraphicsPipelineStorage> {
public:
    ~GraphicsPipelineStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    // Compiles (or reuses) the program for a shader pass + variant, building its vs/fs modules,
    // reflected uniform-set layouts and pipeline layout.
    [[nodiscard]] Ref<ShaderProgram> getOrCreateProgram(const Shader& shader,
                                                        const ShaderPass& pass,
                                                        const ShaderVariantKey& variant);
    [[nodiscard]] rhi::RID resolve(const ShaderProgram& program,
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
    struct RetiredProgram {
        Ref<ShaderProgram> program;
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
                                                            const ShaderProgram& program) const;

    rhi::IDevice* device_{};
    std::unique_ptr<ShaderCompilePipeline> compilePipeline_;
    std::unordered_map<ShaderProgramId, Ref<ShaderProgram>> programs_;
    GraphicsPipelineStorageCache cache_;
    std::unique_ptr<GraphicsPipelineStorageFactory> factory_;
    std::vector<RetiredPipeline> retired_;
    std::vector<RetiredProgram> retiredPrograms_;
    std::uint64_t lastShaderPollSerial_{~std::uint64_t{}};
};

} // namespace engine

#define GRAPHICS_PIPELINE_STORAGE (::engine::GraphicsPipelineStorage::instance())

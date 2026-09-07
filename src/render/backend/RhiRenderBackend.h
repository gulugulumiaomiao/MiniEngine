#pragma once

#include "render/backend/IRenderBackend.h"
#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace engine {

class Window;
class PipelineCache;
class CompiledShaderCache;
class ShaderProgramCache;
class RhiShaderCache;
class MaterialGpuCache;
class MeshGpuCache;
class ShaderPass;

class RhiRenderBackend final : public IRenderBackend {
    public:
    RhiRenderBackend(Window& window, bool vsync);
    ~RhiRenderBackend() override;

    RhiRenderBackend(const RhiRenderBackend&) = delete;
    RhiRenderBackend& operator=(const RhiRenderBackend&) = delete;

    [[nodiscard]] MeshDrawInfo prepareMesh(MeshHandle handle, Mesh& mesh) override;
    void releaseMesh(MeshHandle handle) override;
    [[nodiscard]] rhi::GraphicsPipelineHandle
    pipelineForPass(const Shader& shader, const ShaderPass& pass, const ShaderVariantKey& variant,
                    const VertexLayout& vertexLayout) override;
    void renderFrame(const DrawList& drawList) override;
    void waitIdle() override;

    private:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxRenderObjects = 1024;

    struct FrameResources {
        rhi::BufferHandle sceneBuffer;
        rhi::BufferHandle objectBuffer;
        rhi::BindGroupHandle sceneBindGroup;
    };

    void createBindGroupLayouts();
    void createFrameResources();
    void destroyFrameResources();
    void uploadFrameData(FrameResources& frame, const DrawList& drawList);
    void refreshShaderCaches();
    void recordDrawCommands(FrameResources& frame, const DrawList& drawList);
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    rhi::BindGroupLayoutHandle sceneBindGroupLayout_;
    rhi::BindGroupLayoutHandle materialBindGroupLayout_;
    std::array<FrameResources, kFramesInFlight> frames_{};
    std::unique_ptr<MeshGpuCache> meshGpuCache_;
    std::unique_ptr<CompiledShaderCache> compiledShaderCache_;
    std::unique_ptr<ShaderProgramCache> shaderProgramCache_;
    std::unique_ptr<RhiShaderCache> rhiShaderCache_;
    std::unique_ptr<MaterialGpuCache> materialGpuCache_;
    std::unique_ptr<PipelineCache> pipelineCache_;
    std::uint64_t frameSerial_{};
    std::uint64_t lastShaderPollSerial_{std::numeric_limits<std::uint64_t>::max()};
};

} // namespace engine

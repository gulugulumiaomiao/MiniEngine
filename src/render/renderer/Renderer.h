#pragma once

#include "render/material/Material.h"
#include "render/renderer/DrawList.h"
#include "render/renderer/RenderResources.h"
#include "rhi/RhiFactory.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace engine {

class MaterialGpuCache;
class Mesh;
class MeshGpuCache;
class PipelineCache;
class RenderScene;
class RhiShaderCache;
class ShaderPass;
class ShaderCompilePipeline;
struct MeshData;
struct MeshDesc;
struct MeshBuildRecipe;
struct ShaderVariantKey;
struct VertexLayout;
class Window;

class Renderer final {
public:
    Renderer(Window& window, rhi::Context context);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] MeshHandle createMesh(const MeshDesc& desc, const MeshData& data);
    [[nodiscard]] MeshHandle createProceduralMesh(const MeshBuildRecipe& recipe);
    [[nodiscard]] MeshHandle loadMesh(const VirtualPath& meshPath);
    [[nodiscard]] MaterialHandle loadMaterial(const VirtualPath& materialPath);
    void destroyMesh(MeshHandle handle);
    void destroyMaterial(MaterialHandle handle);
    void setMaterialFloat(MaterialHandle handle, std::string_view name, float value);
    void setMaterialVec2(MaterialHandle handle, std::string_view name, const math::Vec2& value);
    void setMaterialVec3(MaterialHandle handle, std::string_view name, const math::Vec3& value);
    void setMaterialVec4(MaterialHandle handle, std::string_view name, const math::Vec4& value);
    void setMaterialBool(MaterialHandle handle, std::string_view name, bool value);
    void setMaterialTexture(MaterialHandle handle, std::string_view name, std::string value);
    void setMaterialShader(MaterialHandle handle, const VirtualPath& shaderPath);
    void renderFrame(const RenderScene& scene);
    void waitIdle();

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
    [[nodiscard]] MeshDrawInfo prepareMesh(MeshHandle handle, Mesh& mesh);
    void releaseMesh(MeshHandle handle);
    [[nodiscard]] rhi::GraphicsPipelineHandle pipelineForPass(const Shader& shader,
                                                              const ShaderPass& pass,
                                                              const ShaderVariantKey& variant,
                                                              const VertexLayout& vertexLayout);
    void refreshShaderCaches();
    void uploadFrameData(FrameResources& frame, const DrawList& drawList);
    void recordDrawCommands(FrameResources& frame, const DrawList& drawList);
    void submitDrawList(const DrawList& drawList);
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    rhi::BindGroupLayoutHandle sceneBindGroupLayout_;
    rhi::BindGroupLayoutHandle materialBindGroupLayout_;
    std::array<FrameResources, kFramesInFlight> frames_{};
    std::unique_ptr<MeshGpuCache> meshGpuCache_;
    std::unique_ptr<ShaderCompilePipeline> shaderCompilePipeline_;
    std::unique_ptr<RhiShaderCache> rhiShaderCache_;
    std::unique_ptr<MaterialGpuCache> materialGpuCache_;
    std::unique_ptr<PipelineCache> pipelineCache_;
    std::uint64_t frameSerial_{};
    std::uint64_t lastShaderPollSerial_{std::numeric_limits<std::uint64_t>::max()};
};

} // namespace engine

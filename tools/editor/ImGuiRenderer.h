#pragma once

#include "render/gpu/frame/FrameGpuManager.h"
#include "rhi/api/PipelineDesc.h"
#include "rhi/api/RhiTypes.h"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
class IGraphicsCommandEncoder;
} // namespace rhi

} // namespace engine

namespace engine::editor {

// Draws Dear ImGui geometry through the engine RHI, replacing the official
// imgui_impl_vulkan backend so no editor code talks to Vulkan directly. The caller
// owns the rendering scope (barriers, beginRendering/endRendering); this class only
// uploads the frame's geometry and records pipeline state plus draw calls.
class ImGuiRenderer final {
public:
    ImGuiRenderer() = default;
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    // Builds the font atlas texture, the UI pipeline and the per-frame geometry
    // buffers. colorFormat must match the attachment the overlay renders into; an sRGB
    // format also selects the fragment shader variant that decodes ImGui's colors.
    [[nodiscard]] bool initialize(rhi::IDevice& device, rhi::TextureFormat colorFormat);
    // Destroys every GPU resource; the caller must have made the device idle first.
    void shutdown();

    // Rebuilds the pipeline when a swapchain recreation changed the color format.
    // Requires an idle device, which Renderer guarantees around swapchain resizes.
    void onColorFormatChanged(rhi::TextureFormat colorFormat);

    // Records the draw calls for drawData. frameIndex selects the geometry buffers;
    // the swapchain fence already proved that frame's buffers are free to overwrite.
    void render(rhi::IGraphicsCommandEncoder& encoder,
                const ImDrawData& drawData,
                std::uint32_t frameIndex);

private:
    // Geometry is double buffered like the rest of the engine's per-frame resources.
    struct Geometry {
        rhi::BufferHandle vertexBuffer;
        rhi::BufferHandle indexBuffer;
        std::uint32_t vertexCapacity{};
        std::uint32_t indexCapacity{};
    };

    [[nodiscard]] bool createFontTexture();
    [[nodiscard]] bool createPipeline();
    void destroyPipeline();
    // Grows a frame's buffers when the UI needs more geometry than they hold.
    [[nodiscard]] bool reserveGeometry(Geometry& geometry,
                                       std::uint32_t vertexCount,
                                       std::uint32_t indexCount);
    void releaseGeometry(Geometry& geometry);

    rhi::IDevice* device_{};
    rhi::TextureFormat colorFormat_{rhi::TextureFormat::Undefined};
    rhi::ShaderHandle vertexShader_;
    rhi::ShaderHandle fragmentShader_;
    rhi::BindGroupLayoutHandle textureLayout_;
    rhi::GraphicsPipelineHandle pipeline_;
    rhi::TextureHandle fontTexture_;
    rhi::TextureViewHandle fontView_;
    rhi::SamplerHandle sampler_;
    rhi::BindGroupHandle fontBindGroup_;
    std::array<Geometry, FrameGpuManager::kFramesInFlight> geometry_;
    // Reused scratch buffers: the vertex copy applies ImGui's display transform and
    // the index copy concatenates the draw lists into one range.
    std::vector<ImDrawVert> vertexStaging_;
    std::vector<ImDrawIdx> indexStaging_;
};

} // namespace engine::editor

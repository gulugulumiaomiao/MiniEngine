#pragma once

#include "core/math/Math.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "rhi/api/ResourceDesc.h"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
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
    [[nodiscard]] bool initialize(rhi::IDevice& device, rhi::PixelFormat colorFormat);
    // Destroys every GPU resource; the caller must have made the device idle first.
    void shutdown();

    // Rebuilds the pipeline when a swapchain recreation changed the color format.
    // Requires an idle device, which Renderer guarantees around swapchain resizes.
    void onColorFormatChanged(rhi::PixelFormat colorFormat);

    // generation zero cannot collide with encoded live RHI handles.
    static constexpr ImTextureID kSceneTextureId = 1;
    // Called after the selected slot's fence, never while that slot is in flight.
    [[nodiscard]] bool setSceneTexture(std::uint32_t frameIndex, rhi::RID view);

    // Records the draw calls for drawData. frameIndex selects the geometry buffers;
    // the swapchain fence already proved that frame's buffers are free to overwrite.
    void render(rhi::RID commandBuffer,
                const ImDrawData& drawData,
                std::uint32_t frameIndex);

private:
    // Geometry is double buffered like the rest of the engine's per-frame resources.
    // ImGui's interleaved vertex is split into one buffer per semantic so the RHI only
    // needs a single stream description per binding.
    struct Geometry {
        std::array<rhi::RID, 3> vertexBuffers{};
        std::array<std::uint32_t, 3> vertexCapacities{};
        rhi::RID indexBuffer;
        std::uint32_t indexCapacity{};
    };

    [[nodiscard]] bool createFontTexture();
    [[nodiscard]] bool createPipeline();
    void destroyPipeline();
    // Grows a frame's buffers when the UI needs more geometry than they hold.
    [[nodiscard]] bool
    reserveGeometry(Geometry& geometry, std::uint32_t vertexCount, std::uint32_t indexCount);
    void releaseGeometry(Geometry& geometry);

    rhi::IDevice* device_{};
    rhi::PixelFormat colorFormat_{rhi::PixelFormat::Undefined};
    rhi::RID vertexShader_;
    rhi::RID fragmentShader_;
    rhi::RID textureLayout_;
    rhi::RID pipeline_;
    rhi::RID fontTexture_;
    rhi::RID fontView_;
    rhi::RID sampler_;
    rhi::RID fontBindGroup_;
    struct SceneTexture {
        rhi::RID view;
        rhi::RID group;
    };
    std::array<SceneTexture, FrameGpuManager::kFramesInFlight> sceneTextures_{};
    std::array<Geometry, FrameGpuManager::kFramesInFlight> geometry_;
    // Reused scratch buffers: the vertex copy applies ImGui's display transform and
    // deinterleaves the data into one buffer per semantic; the index copy concatenates
    // the draw lists into one range.
    std::vector<math::Vec2> positionStaging_;
    std::vector<math::Vec2> uvStaging_;
    std::vector<std::uint32_t> colorStaging_;
    std::vector<ImDrawIdx> indexStaging_;
};

} // namespace engine::editor

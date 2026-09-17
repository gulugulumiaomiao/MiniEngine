#include "tools/editor/ImGuiRenderer.h"

#include "TestRenderDevice.h"
#include <gtest/gtest.h>
#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <cstdint>

#include <span>

#include <vector>

namespace {

using namespace engine;

// Draws two windows, one of them reaching past the framebuffer, so the draw data has
// several lists and at least one clip rect that has to be clamped.
const ImDrawData& buildFrame(const ImVec2& displaySize) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = displaySize;
    io.DeltaTime = 1.0F / 60.0F;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({10.0F, 10.0F});
    ImGui::SetNextWindowSize({200.0F, 120.0F});
    ImGui::Begin("First");
    ImGui::TextUnformatted("hello");
    ImGui::End();
    ImGui::SetNextWindowPos({displaySize.x - 60.0F, displaySize.y - 60.0F});
    ImGui::SetNextWindowSize({240.0F, 240.0F});
    ImGui::Begin("Second");
    ImGui::TextUnformatted("world");
    ImGui::End();
    ImGui::Render();
    return *ImGui::GetDrawData();
}

} // namespace

class ImGuiRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
    }
    void TearDown() override { ImGui::DestroyContext(); }
};
TEST_F(ImGuiRendererTest, GeometryFontsAndColorFormats) {
    constexpr ImVec2 kDisplaySize{800.0F, 600.0F};
    constexpr auto kColorFormat = rhi::TextureFormat::Bgra8Srgb;

    MockDevice device;

    {
        editor::ImGuiRenderer renderer;
        if (!renderer.initialize(device, kColorFormat))
            FAIL() << "initialize failed";

        // The font atlas must be a sampled, upload-capable RGBA8 texture bound through a
        // single fragment-visible combined image sampler.
        if (device.textures.size() != 1 || device.textureUploads != 1 ||
            device.textures[0].format != rhi::TextureFormat::Rgba8Unorm ||
            !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::Sampled) ||
            !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::TransferDestination)) {
            FAIL() << "font atlas texture is not set up for sampling";
        }
        if (device.layoutEntries.size() != 1 ||
            device.layoutEntries[0].type != rhi::BindingType::SampledTexture ||
            device.layoutEntries[0].visibility != rhi::ShaderVisibility::Fragment ||
            device.bindGroupEntries.size() != 1 ||
            device.bindGroupEntries[0].type != rhi::BindingType::SampledTexture) {
            FAIL() << "font bind group layout is wrong";
        }
        if (device.samplers.size() != 1 ||
            device.samplers[0].addressU != rhi::SamplerAddressMode::ClampToEdge ||
            device.samplers[0].addressV != rhi::SamplerAddressMode::ClampToEdge) {
            FAIL() << "font sampler must clamp so glyphs do not bleed";
        }
        if (ImGui::GetIO().Fonts->TexID == 0)
            FAIL() << "the font atlas texture id was not published to ImGui";

        // Both stages come from the embedded SPIR-V, which is always a multiple of four
        // bytes, and the pipeline must describe ImGui's vertex layout and blending.
        if (device.shaders.size() != 2 || device.shaders[0] != rhi::ShaderStage::Vertex ||
            device.shaders[1] != rhi::ShaderStage::Fragment ||
            device.shaderBytecode[0].size() % 4 != 0 ||
            device.shaderBytecode[1].size() % 4 != 0 || device.shaderBytecode[0].empty()) {
            FAIL() << "UI shader modules are wrong";
        }
        if (device.pipelines.size() != 1)
            FAIL() << "expected exactly one UI pipeline";
        const rhi::GraphicsPipelineDesc& pipeline = device.pipelines[0];
        if (pipeline.vertexBindings.size() != 1 ||
            pipeline.vertexBindings[0].stride != sizeof(ImDrawVert) ||
            pipeline.vertexAttributes.size() != 3 ||
            pipeline.vertexAttributes[2].format != rhi::VertexFormat::UInt8x4Normalized ||
            pipeline.blend.mode != rhi::BlendMode::Alpha || pipeline.depthStencil.depthTestEnable ||
            pipeline.depthStencil.depthWriteEnable || pipeline.raster.cull != rhi::CullMode::None ||
            pipeline.colorFormats.size() != 1 || pipeline.colorFormats[0] != kColorFormat ||
            pipeline.bindGroupLayouts.size() != 1) {
            FAIL() << "UI pipeline state does not match ImGui's requirements";
        }

        const ImDrawData& drawData = buildFrame(kDisplaySize);
        MockEncoder encoder;
        renderer.render(encoder, drawData, 0);

        // Geometry is host visible so the frame can be written without a staging copy.
        if (device.buffers.size() != 2 ||
            device.buffers[0].memoryUsage != rhi::MemoryUsage::Upload ||
            device.buffers[1].memoryUsage != rhi::MemoryUsage::Upload ||
            !rhi::hasFlag(device.buffers[0].usage, rhi::BufferUsage::Vertex) ||
            !rhi::hasFlag(device.buffers[1].usage, rhi::BufferUsage::Index)) {
            FAIL() << "per-frame geometry buffers are wrong";
        }
        if (device.uploadedBytes.size() != 2 ||
            device.uploadedBytes[0] !=
                static_cast<std::size_t>(drawData.TotalVtxCount) * sizeof(ImDrawVert) ||
            device.uploadedBytes[1] !=
                static_cast<std::size_t>(drawData.TotalIdxCount) * sizeof(ImDrawIdx)) {
            FAIL() << "the whole frame's geometry must be uploaded once";
        }
        if (encoder.boundPipelines.size() != 1 || encoder.viewports.size() != 1 ||
            encoder.viewports[0].width != kDisplaySize.x ||
            encoder.viewports[0].height != kDisplaySize.y || encoder.indexFormats.size() != 1 ||
            encoder.indexFormats[0] != rhi::IndexFormat::UInt16) {
            FAIL() << "pipeline, viewport or index format binding is wrong";
        }

        // Every command becomes one indexed draw whose offsets point into the single
        // concatenated buffer pair, and every clip rect stays inside the framebuffer.
        std::vector<rhi::DrawIndexedArguments> expected;
        std::uint32_t vertexBase{};
        std::uint32_t indexBase{};
        for (const ImDrawList* list : drawData.CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback || command.ElemCount == 0 || command.GetTexID() == 0)
                    continue;
                expected.push_back({.indexCount = command.ElemCount,
                                    .firstIndex = command.IdxOffset + indexBase,
                                    .vertexOffset = static_cast<std::int32_t>(command.VtxOffset +
                                                                              vertexBase)});
            }
            vertexBase += static_cast<std::uint32_t>(list->VtxBuffer.Size);
            indexBase += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        }
        if (expected.empty())
            FAIL() << "the test frame produced no draw commands";
        if (encoder.draws.size() != expected.size())
            FAIL() << "draw call count does not match the command count";
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const rhi::DrawIndexedArguments& actual = encoder.draws[index];
            if (actual.indexCount != expected[index].indexCount ||
                actual.firstIndex != expected[index].firstIndex ||
                actual.vertexOffset != expected[index].vertexOffset || actual.instanceCount != 1) {
                FAIL() << "draw offsets do not accumulate across the draw lists";
            }
        }
        if (encoder.scissors.size() != expected.size() ||
            encoder.boundGroups.size() != expected.size())
            FAIL() << "each draw needs its own scissor and texture binding";
        for (const rhi::Rect& scissor : encoder.scissors) {
            if (scissor.x < 0 || scissor.y < 0 ||
                static_cast<float>(scissor.x) + static_cast<float>(scissor.width) >
                    kDisplaySize.x ||
                static_cast<float>(scissor.y) + static_cast<float>(scissor.height) >
                    kDisplaySize.y) {
                FAIL() << "clip rects were not clamped to the framebuffer";
            }
        }
        for (const rhi::BindGroupHandle& group : encoder.boundGroups) {
            if (group.index != device.bindGroups || group.generation != 1)
                FAIL() << "draws must sample the bind group registered for the atlas";
        }

        // A second frame reuses the buffers instead of recreating them.
        const std::size_t buffersAfterFirstFrame = device.buffers.size();
        MockEncoder secondEncoder;
        renderer.render(secondEncoder, buildFrame(kDisplaySize), 1);
        if (device.buffers.size() != buffersAfterFirstFrame + 2)
            FAIL() << "the second frame in flight needs its own geometry buffers";
        MockEncoder thirdEncoder;
        renderer.render(thirdEncoder, buildFrame(kDisplaySize), 0);
        if (device.buffers.size() != buffersAfterFirstFrame + 2)
            FAIL() << "a frame index must reuse the buffers it already grew";

        renderer.shutdown();
        if (device.destroyedTextures != 1 || device.destroyedViews != 1 ||
            device.destroyedSamplers != 1 || device.destroyedBindGroups != 1 ||
            device.destroyedLayouts != 1 || device.destroyedPipelines != 1 ||
            device.destroyedShaders != 2 ||
            device.destroyedBuffers != static_cast<int>(device.buffers.size())) {
            FAIL() << "shutdown leaked GPU resources";
        }
    }

    // A plain attachment must keep ImGui's colors untouched, while the sRGB attachment
    // used above needs the decoding fragment variant.
    MockDevice unormDevice;
    {
        editor::ImGuiRenderer renderer;
        if (!renderer.initialize(unormDevice, rhi::TextureFormat::Bgra8Unorm))
            FAIL() << "initialize failed for a non-sRGB attachment";
        if (unormDevice.shaderBytecode.size() != 2 ||
            unormDevice.shaderBytecode[0] != device.shaderBytecode[0]) {
            FAIL() << "the vertex stage must not depend on the attachment format";
        }
        if (unormDevice.shaderBytecode[1] == device.shaderBytecode[1])
            FAIL() << "an sRGB attachment must use a color decoding fragment shader";
        renderer.shutdown();
    }

}

TEST_F(ImGuiRendererTest, SceneBindingsAreCachedPerAcquiredFrameAndReleased) {
    MockDevice device;
    editor::ImGuiRenderer renderer;
    ASSERT_TRUE(renderer.initialize(device, rhi::TextureFormat::Bgra8Srgb));
    ASSERT_TRUE(renderer.setSceneTexture(0, {40, 1}));
    ASSERT_TRUE(renderer.setSceneTexture(1, {41, 1}));
    EXPECT_EQ(device.bindGroups, 3U); // font plus two scene slots
    ASSERT_TRUE(renderer.setSceneTexture(0, {40, 1}));
    EXPECT_EQ(device.bindGroups, 3U);
    ASSERT_TRUE(renderer.setSceneTexture(0, {40, 2})); // same index, new generation
    EXPECT_EQ(device.bindGroups, 4U);
    EXPECT_EQ(device.destroyedBindGroups, 1);
    EXPECT_EQ(device.bindGroupEntries[0].textureView.generation, 2U);
    EXPECT_FALSE(renderer.setSceneTexture(FrameGpuManager::kFramesInFlight, {42, 1}));
    renderer.shutdown();
    EXPECT_EQ(device.destroyedBindGroups, 4);
    EXPECT_FALSE(renderer.setSceneTexture(0, {40, 1}));
}

TEST_F(ImGuiRendererTest, LogicalSceneImageUsesTheAcquiredSlotAndSkipsMissingBindings) {
    MockDevice device;
    editor::ImGuiRenderer renderer;
    ASSERT_TRUE(renderer.initialize(device, rhi::TextureFormat::Bgra8Srgb));
    ASSERT_TRUE(renderer.setSceneTexture(0, {40, 1}));
    ASSERT_TRUE(renderer.setSceneTexture(1, {41, 1}));
    const auto imageFrame = []() -> const ImDrawData& {
        ImGui::GetIO().DisplaySize = {800, 600};
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddImage(editor::ImGuiRenderer::kSceneTextureId,
                                                {0, 0}, {400, 300});
        ImGui::Render();
        return *ImGui::GetDrawData();
    };
    MockEncoder slot1;
    renderer.render(slot1, imageFrame(), 1);
    ASSERT_EQ(slot1.boundGroups.size(), 1U);
    EXPECT_EQ(slot1.boundGroups[0].index, 3U);
    MockEncoder slot0;
    renderer.render(slot0, imageFrame(), 0);
    ASSERT_EQ(slot0.boundGroups.size(), 1U);
    EXPECT_EQ(slot0.boundGroups[0].index, 2U);
    EXPECT_FALSE(renderer.setSceneTexture(0, {}));
    MockEncoder missing;
    renderer.render(missing, imageFrame(), 0);
    EXPECT_TRUE(missing.draws.empty());
}

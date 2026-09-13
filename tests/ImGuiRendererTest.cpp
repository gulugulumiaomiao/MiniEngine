#include "tools/editor/ImGuiRenderer.h"

#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace engine;

int fail(std::string_view message) {
    std::fprintf(stderr, "ImGuiRendererTest: %s\n", message.data());
    return 1;
}

// Hands out live handles (generation 1) and records every description so the test can
// assert what the renderer asked the RHI for.
class MockDevice final : public rhi::IDevice {
public:
    rhi::BufferHandle createBuffer(const rhi::BufferDesc& desc) override {
        buffers.push_back(desc);
        return {static_cast<std::uint32_t>(buffers.size()), 1};
    }
    void destroyBuffer(rhi::BufferHandle) override { ++destroyedBuffers; }
    void uploadBuffer(rhi::BufferHandle, std::span<const std::byte> data, std::uint64_t) override {
        uploadedBytes.push_back(data.size_bytes());
    }

    rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        return {static_cast<std::uint32_t>(textures.size()), 1};
    }
    void destroyTexture(rhi::TextureHandle) override { ++destroyedTextures; }
    void uploadTexture(rhi::TextureHandle, std::span<const rhi::TextureUploadRegion>) override {
        ++textureUploads;
    }
    rhi::TextureViewHandle createTextureView(const rhi::TextureViewDesc&) override {
        return {++views, 1};
    }
    void destroyTextureView(rhi::TextureViewHandle) override { ++destroyedViews; }
    rhi::SamplerHandle createSampler(const rhi::SamplerDesc& desc) override {
        samplers.push_back(desc);
        return {static_cast<std::uint32_t>(samplers.size()), 1};
    }
    void destroySampler(rhi::SamplerHandle) override { ++destroyedSamplers; }
    rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override {
        shaders.push_back(desc.stage);
        shaderBytecode.emplace_back(desc.bytecode.begin(), desc.bytecode.end());
        return {static_cast<std::uint32_t>(shaders.size()), 1};
    }
    void destroyShader(rhi::ShaderHandle) override { ++destroyedShaders; }
    rhi::GraphicsPipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override {
        pipelines.push_back(desc);
        return {static_cast<std::uint32_t>(pipelines.size()), 1};
    }
    void destroyGraphicsPipeline(rhi::GraphicsPipelineHandle) override { ++destroyedPipelines; }
    rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override {
        layoutEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++layouts, 1};
    }
    void destroyBindGroupLayout(rhi::BindGroupLayoutHandle) override { ++destroyedLayouts; }
    rhi::BindGroupHandle createBindGroup(const rhi::BindGroupDesc& desc) override {
        bindGroupEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++bindGroups, 1};
    }
    void destroyBindGroup(rhi::BindGroupHandle) override { ++destroyedBindGroups; }

    VkDevice device() const override { return VK_NULL_HANDLE; }
    VkInstance instance() const override { return VK_NULL_HANDLE; }
    VkPhysicalDevice physicalDevice() const override { return VK_NULL_HANDLE; }
    VkQueue graphicsQueue() const override { return VK_NULL_HANDLE; }
    std::uint32_t graphicsQueueFamily() const override { return 0; }
    VkBuffer resolveBuffer(rhi::BufferHandle) const override { return VK_NULL_HANDLE; }
    VkImage resolveTexture(rhi::TextureHandle) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(rhi::TextureViewHandle) const override { return VK_NULL_HANDLE; }
    rhi::ResolvedPipeline resolvePipeline(rhi::GraphicsPipelineHandle) const override { return {}; }
    VkDescriptorSet resolveBindGroup(rhi::BindGroupHandle) const override { return VK_NULL_HANDLE; }
    void waitIdle() override {}

    std::vector<rhi::BufferDesc> buffers;
    std::vector<rhi::TextureDesc> textures;
    std::vector<rhi::SamplerDesc> samplers;
    std::vector<rhi::ShaderStage> shaders;
    std::vector<std::vector<std::byte>> shaderBytecode;
    std::vector<rhi::GraphicsPipelineDesc> pipelines;
    std::vector<rhi::BindGroupLayoutEntry> layoutEntries;
    std::vector<rhi::BindGroupEntry> bindGroupEntries;
    std::vector<std::size_t> uploadedBytes;
    std::uint32_t views{};
    std::uint32_t layouts{};
    std::uint32_t bindGroups{};
    std::uint32_t textureUploads{};
    int destroyedBuffers{};
    int destroyedTextures{};
    int destroyedViews{};
    int destroyedSamplers{};
    int destroyedShaders{};
    int destroyedPipelines{};
    int destroyedLayouts{};
    int destroyedBindGroups{};
};

class MockEncoder final : public rhi::IGraphicsCommandEncoder {
public:
    void resourceBarriers(std::span<const rhi::TextureBarrier>) override {}
    void beginRendering(const rhi::RenderingInfo&) override {}
    void endRendering() override {}
    [[nodiscard]] VkCommandBuffer nativeCommandBuffer() const override { return VK_NULL_HANDLE; }
    void setViewport(const rhi::Viewport& viewport) override { viewports.push_back(viewport); }
    void setScissor(const rhi::Rect& scissor) override { scissors.push_back(scissor); }
    void bindPipeline(rhi::GraphicsPipelineHandle pipeline) override {
        boundPipelines.push_back(pipeline);
    }
    void bindVertexBuffer(std::uint32_t, rhi::BufferHandle buffer, std::uint64_t) override {
        boundVertexBuffers.push_back(buffer);
    }
    void bindIndexBuffer(rhi::BufferHandle buffer, std::uint64_t, rhi::IndexFormat format) override {
        boundIndexBuffers.push_back(buffer);
        indexFormats.push_back(format);
    }
    void bindGroup(std::uint32_t set,
                   rhi::BindGroupHandle group,
                   std::span<const std::uint32_t>) override {
        boundSets.push_back(set);
        boundGroups.push_back(group);
    }
    void draw(const rhi::DrawArguments&) override {}
    void drawIndexed(const rhi::DrawIndexedArguments& arguments) override {
        draws.push_back(arguments);
    }
    void beginDebugLabel(std::string_view, const math::Vec4&) override {}
    void endDebugLabel() override {}

    std::vector<rhi::Viewport> viewports;
    std::vector<rhi::Rect> scissors;
    std::vector<rhi::GraphicsPipelineHandle> boundPipelines;
    std::vector<rhi::BufferHandle> boundVertexBuffers;
    std::vector<rhi::BufferHandle> boundIndexBuffers;
    std::vector<rhi::IndexFormat> indexFormats;
    std::vector<std::uint32_t> boundSets;
    std::vector<rhi::BindGroupHandle> boundGroups;
    std::vector<rhi::DrawIndexedArguments> draws;
};

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

int main() {
    constexpr ImVec2 kDisplaySize{800.0F, 600.0F};
    constexpr auto kColorFormat = rhi::TextureFormat::Bgra8Srgb;

    MockDevice device;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    {
        editor::ImGuiRenderer renderer;
        if (!renderer.initialize(device, kColorFormat))
            return fail("initialize failed");

        // The font atlas must be a sampled, upload-capable RGBA8 texture bound through a
        // single fragment-visible combined image sampler.
        if (device.textures.size() != 1 || device.textureUploads != 1 ||
            device.textures[0].format != rhi::TextureFormat::Rgba8Unorm ||
            !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::Sampled) ||
            !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::TransferDestination)) {
            return fail("font atlas texture is not set up for sampling");
        }
        if (device.layoutEntries.size() != 1 ||
            device.layoutEntries[0].type != rhi::BindingType::SampledTexture ||
            device.layoutEntries[0].visibility != rhi::ShaderVisibility::Fragment ||
            device.bindGroupEntries.size() != 1 ||
            device.bindGroupEntries[0].type != rhi::BindingType::SampledTexture) {
            return fail("font bind group layout is wrong");
        }
        if (device.samplers.size() != 1 ||
            device.samplers[0].addressU != rhi::SamplerAddressMode::ClampToEdge ||
            device.samplers[0].addressV != rhi::SamplerAddressMode::ClampToEdge) {
            return fail("font sampler must clamp so glyphs do not bleed");
        }
        if (ImGui::GetIO().Fonts->TexID == 0)
            return fail("the font atlas texture id was not published to ImGui");

        // Both stages come from the embedded SPIR-V, which is always a multiple of four
        // bytes, and the pipeline must describe ImGui's vertex layout and blending.
        if (device.shaders.size() != 2 || device.shaders[0] != rhi::ShaderStage::Vertex ||
            device.shaders[1] != rhi::ShaderStage::Fragment ||
            device.shaderBytecode[0].size() % 4 != 0 ||
            device.shaderBytecode[1].size() % 4 != 0 || device.shaderBytecode[0].empty()) {
            return fail("UI shader modules are wrong");
        }
        if (device.pipelines.size() != 1)
            return fail("expected exactly one UI pipeline");
        const rhi::GraphicsPipelineDesc& pipeline = device.pipelines[0];
        if (pipeline.vertexBindings.size() != 1 ||
            pipeline.vertexBindings[0].stride != sizeof(ImDrawVert) ||
            pipeline.vertexAttributes.size() != 3 ||
            pipeline.vertexAttributes[2].format != rhi::VertexFormat::UInt8x4Normalized ||
            pipeline.blend.mode != rhi::BlendMode::Alpha || pipeline.depthStencil.depthTestEnable ||
            pipeline.depthStencil.depthWriteEnable || pipeline.raster.cull != rhi::CullMode::None ||
            pipeline.colorFormats.size() != 1 || pipeline.colorFormats[0] != kColorFormat ||
            pipeline.bindGroupLayouts.size() != 1) {
            return fail("UI pipeline state does not match ImGui's requirements");
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
            return fail("per-frame geometry buffers are wrong");
        }
        if (device.uploadedBytes.size() != 2 ||
            device.uploadedBytes[0] !=
                static_cast<std::size_t>(drawData.TotalVtxCount) * sizeof(ImDrawVert) ||
            device.uploadedBytes[1] !=
                static_cast<std::size_t>(drawData.TotalIdxCount) * sizeof(ImDrawIdx)) {
            return fail("the whole frame's geometry must be uploaded once");
        }
        if (encoder.boundPipelines.size() != 1 || encoder.viewports.size() != 1 ||
            encoder.viewports[0].width != kDisplaySize.x ||
            encoder.viewports[0].height != kDisplaySize.y || encoder.indexFormats.size() != 1 ||
            encoder.indexFormats[0] != rhi::IndexFormat::UInt16) {
            return fail("pipeline, viewport or index format binding is wrong");
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
            return fail("the test frame produced no draw commands");
        if (encoder.draws.size() != expected.size())
            return fail("draw call count does not match the command count");
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const rhi::DrawIndexedArguments& actual = encoder.draws[index];
            if (actual.indexCount != expected[index].indexCount ||
                actual.firstIndex != expected[index].firstIndex ||
                actual.vertexOffset != expected[index].vertexOffset || actual.instanceCount != 1) {
                return fail("draw offsets do not accumulate across the draw lists");
            }
        }
        if (encoder.scissors.size() != expected.size() ||
            encoder.boundGroups.size() != expected.size())
            return fail("each draw needs its own scissor and texture binding");
        for (const rhi::Rect& scissor : encoder.scissors) {
            if (scissor.x < 0 || scissor.y < 0 ||
                static_cast<float>(scissor.x) + static_cast<float>(scissor.width) >
                    kDisplaySize.x ||
                static_cast<float>(scissor.y) + static_cast<float>(scissor.height) >
                    kDisplaySize.y) {
                return fail("clip rects were not clamped to the framebuffer");
            }
        }
        for (const rhi::BindGroupHandle& group : encoder.boundGroups) {
            if (group.index != device.bindGroups || group.generation != 1)
                return fail("draws must sample the bind group registered for the atlas");
        }

        // A second frame reuses the buffers instead of recreating them.
        const std::size_t buffersAfterFirstFrame = device.buffers.size();
        MockEncoder secondEncoder;
        renderer.render(secondEncoder, buildFrame(kDisplaySize), 1);
        if (device.buffers.size() != buffersAfterFirstFrame + 2)
            return fail("the second frame in flight needs its own geometry buffers");
        MockEncoder thirdEncoder;
        renderer.render(thirdEncoder, buildFrame(kDisplaySize), 0);
        if (device.buffers.size() != buffersAfterFirstFrame + 2)
            return fail("a frame index must reuse the buffers it already grew");

        renderer.shutdown();
        if (device.destroyedTextures != 1 || device.destroyedViews != 1 ||
            device.destroyedSamplers != 1 || device.destroyedBindGroups != 1 ||
            device.destroyedLayouts != 1 || device.destroyedPipelines != 1 ||
            device.destroyedShaders != 2 ||
            device.destroyedBuffers != static_cast<int>(device.buffers.size())) {
            return fail("shutdown leaked GPU resources");
        }
    }

    // A plain attachment must keep ImGui's colors untouched, while the sRGB attachment
    // used above needs the decoding fragment variant.
    MockDevice unormDevice;
    {
        editor::ImGuiRenderer renderer;
        if (!renderer.initialize(unormDevice, rhi::TextureFormat::Bgra8Unorm))
            return fail("initialize failed for a non-sRGB attachment");
        if (unormDevice.shaderBytecode.size() != 2 ||
            unormDevice.shaderBytecode[0] != device.shaderBytecode[0]) {
            return fail("the vertex stage must not depend on the attachment format");
        }
        if (unormDevice.shaderBytecode[1] == device.shaderBytecode[1])
            return fail("an sRGB attachment must use a color decoding fragment shader");
        renderer.shutdown();
    }

    ImGui::DestroyContext();
    return 0;
}

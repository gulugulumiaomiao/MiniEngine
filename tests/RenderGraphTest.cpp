#include "render/render_graph/RenderGraph.h"
#include "render/render_graph/RgTexturePool.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace {

class MockDevice final : public engine::rhi::IDevice {
public:
    engine::rhi::RID createBuffer(const engine::rhi::BufferDesc&) override { return {}; }
    void destroyBuffer(engine::rhi::RID) override {}
    void
    uploadBuffer(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}

    engine::rhi::RID createTexture(const engine::rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        views.push_back({.type = desc.dimension,
                         .format = desc.format,
                         .baseMip = 0,
                         .mipCount = desc.mipCount,
                         .baseLayer = 0,
                         .layerCount = desc.arrayLayers});
        return {static_cast<std::uint32_t>(textures.size()), 1};
    }
    void destroyTexture(engine::rhi::RID handle) override {
        if (handle)
            destroyedTextures.push_back(handle);
    }
    void uploadTexture(engine::rhi::RID,
                       std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::RID
    createTextureView(engine::rhi::RID,
                      const engine::rhi::TextureViewDesc& desc) override {
        views.push_back(desc);
        return {static_cast<std::uint32_t>(views.size()), 1};
    }
    engine::rhi::RID defaultTextureView(engine::rhi::RID) const override {
        return views.empty()
                   ? engine::rhi::RID{}
                   : engine::rhi::RID{static_cast<std::uint32_t>(views.size()), 1};
    }
    void destroyTextureView(engine::rhi::RID handle) override {
        if (handle)
            destroyedViews.push_back(handle);
    }
    engine::rhi::RID createSampler(const engine::rhi::SamplerDesc&) override {
        return {};
    }
    void destroySampler(engine::rhi::RID) override {}
    engine::rhi::RID createShader(const engine::rhi::ShaderDesc&) override { return {}; }
    void destroyShader(engine::rhi::RID) override {}
    engine::rhi::RID
    createGraphicsPipeline(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void destroyGraphicsPipeline(engine::rhi::RID) override {}
    engine::rhi::RID
    createBindGroupLayout(const engine::rhi::BindGroupLayoutDesc&) override {
        return {};
    }
    void destroyBindGroupLayout(engine::rhi::RID) override {}
    engine::rhi::RID createBindGroup(const engine::rhi::BindGroupDesc&) override {
        return {};
    }
    void destroyBindGroup(engine::rhi::RID) override {}
    std::unique_ptr<engine::rhi::ICommandBuffer> createCommandBuffer() override { return nullptr; }
    void submitCommand(engine::rhi::ICommandBuffer&, const engine::rhi::SubmitSync&) override {}
    void waitIdle() override {}

    std::vector<engine::rhi::TextureDesc> textures;
    std::vector<engine::rhi::TextureViewDesc> views;
    std::vector<engine::rhi::RID> destroyedTextures;
    std::vector<engine::rhi::RID> destroyedViews;
};

class MockGraphicsEncoder final : public engine::rhi::ICommandBuffer {
public:
    void begin() override { currentState = engine::rhi::CommandState::Recording; }
    void end() override { currentState = engine::rhi::CommandState::Executable; }
    [[nodiscard]] engine::rhi::CommandState state() const override { return currentState; }
    void resourceBarriers(std::span<const engine::rhi::TextureBarrier> barriers) override {
        events.push_back("barriers:" + std::to_string(barriers.size()));
        recordedBarriers.insert(recordedBarriers.end(), barriers.begin(), barriers.end());
    }
    void beginRendering(const engine::rhi::RenderingInfo&) override {
        events.emplace_back("beginRendering");
    }
    void endRendering() override { events.emplace_back("endRendering"); }
    void setViewport(const engine::rhi::Viewport&) override {}
    void setScissor(const engine::rhi::Rect&) override {}
    void setCullMode(engine::rhi::CullMode) override {}
    void setFrontFace(engine::rhi::FrontFace) override {}
    void setDepthTestEnable(bool) override {}
    void setDepthWriteEnable(bool) override {}
    void setDepthCompareOp(engine::rhi::CompareOp) override {}
    void setBlendState(engine::rhi::BlendMode) override {}
    void setColorWriteMask(engine::rhi::ColorWriteMask) override {}
    void setPrimitiveTopology(engine::rhi::PrimitiveTopology) override {}
    void setFillMode(engine::rhi::FillMode) override {}
    void bindPipeline(engine::rhi::RID) override {}
    void bindVertexBuffer(std::uint32_t, engine::rhi::RID, std::uint64_t) override {}
    void
    bindIndexBuffer(engine::rhi::RID, std::uint64_t, engine::rhi::IndexFormat) override {}
    void bindGroup(std::uint32_t,
                   engine::rhi::RID,
                   std::span<const std::uint32_t>) override {}
    void draw(const engine::rhi::DrawArguments&) override {}
    void drawIndexed(const engine::rhi::DrawIndexedArguments&) override {}
    void beginDebugLabel(std::string_view name, const engine::math::Vec4&) override {
        events.push_back("label:" + std::string{name});
    }
    void endDebugLabel() override { events.emplace_back("endLabel"); }
    void copyBuffer(const engine::rhi::BufferCopy&) override {}
    void copyImage(const engine::rhi::ImageCopy&) override {}
    void copyBufferToImage(const engine::rhi::BufferImageCopy&) override {}
    void copyImageToBuffer(const engine::rhi::BufferImageCopy&) override {}
    void updateBuffer(const engine::rhi::BufferUpdate&) override {}
    void updateImage(const engine::rhi::ImageUpdate&) override {}

    std::vector<std::string> events;
    engine::rhi::CommandState currentState{engine::rhi::CommandState::Initial};
    std::vector<engine::rhi::TextureBarrier> recordedBarriers;
};

} // namespace

int main() {
    using namespace engine;

    MockDevice device;
    RgTexturePool pool{device, 2};
    pool.beginFrame(0);

    const rhi::RID texture{3, 7};
    const rhi::RID view{3, 7};

    RenderGraph graph;
    const RgTextureHandle imported = graph.importTexture({texture,
                                                          view,
                                                          rhi::ResourceState::Undefined,
                                                          rhi::ResourceState::Present,
                                                          rhi::TextureAspect::Color});
    RgRenderingInfo rendering;
    rendering.renderArea = {0, 0, 1280, 720};
    rendering.colorAttachments.push_back({imported, rhi::LoadOp::Clear, rhi::StoreOp::Store, {}});
    graph.addGraphicsPass(
        "Forward",
        std::move(rendering),
        {{imported, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [](rhi::ICommandBuffer&) {});

    graph.compile(pool);
    MockGraphicsEncoder encoder;
    graph.execute(encoder);

    const std::vector<std::string> expectedEvents{
        "label:Forward", "barriers:1", "beginRendering", "endRendering", "endLabel", "barriers:1"};
    if (encoder.events != expectedEvents || encoder.recordedBarriers.size() != 2 ||
        encoder.recordedBarriers[0].before != rhi::ResourceState::Undefined ||
        encoder.recordedBarriers[0].after != rhi::ResourceState::ColorAttachment ||
        encoder.recordedBarriers[1].before != rhi::ResourceState::ColorAttachment ||
        encoder.recordedBarriers[1].after != rhi::ResourceState::Present) {
        return 1;
    }

    graph.reset();

    // Transient texture path: a second pass reads from a transient color texture.
    pool.beginFrame(1);
    RenderGraph transientGraph;
    const RgTextureHandle transient = transientGraph.createTexture({
        .format = rhi::PixelFormat::Rgba8Unorm,
        .width = 1280,
        .height = 720,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
        .aspect = rhi::TextureAspect::Color,
        .debugName = "Transient",
    });
    RgRenderingInfo transientRendering;
    transientRendering.renderArea = {0, 0, 1280, 720};
    transientRendering.colorAttachments.push_back(
        {transient, rhi::LoadOp::Clear, rhi::StoreOp::Store, {}});
    transientGraph.addGraphicsPass(
        "Write",
        std::move(transientRendering),
        {{transient, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [](rhi::ICommandBuffer&) {});

    transientGraph.compile(pool);
    MockGraphicsEncoder transientEncoder;
    transientGraph.execute(transientEncoder);
    transientGraph.reset();

    if (device.textures.empty() || device.views.empty()) {
        return 2;
    }

    // Shadow map barrier chain: a transient depth texture written by a ShadowCaster-style
    // pass (DepthAttachment) and read by a Forward-style pass (ShaderRead). Transient
    // textures receive no final barrier back to Undefined.
    pool.beginFrame(0);
    RenderGraph shadowGraph;
    const RgTextureHandle shadowMap = shadowGraph.createTexture({
        .format = rhi::PixelFormat::Depth32Float,
        .width = 1024,
        .height = 1024,
        .usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled,
        .aspect = rhi::TextureAspect::Depth,
        .debugName = "ShadowMap",
    });
    RgRenderingInfo shadowRendering;
    shadowRendering.renderArea = {0, 0, 1024, 1024};
    shadowRendering.depthAttachments.push_back(
        {shadowMap, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});
    shadowGraph.addGraphicsPass(
        "ShadowCaster",
        std::move(shadowRendering),
        {{shadowMap, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment}},
        [](rhi::ICommandBuffer&) {});
    shadowGraph.addGraphicsPass(
        "Forward",
        RgRenderingInfo{},
        {{shadowMap, rhi::TextureAspect::Depth, rhi::ResourceState::ShaderRead}},
        [](rhi::ICommandBuffer&) {});
    shadowGraph.compile(pool);
    MockGraphicsEncoder shadowEncoder;
    shadowGraph.execute(shadowEncoder);
    shadowGraph.reset();

    const std::vector<std::string> expectedShadowEvents{
        "label:ShadowCaster",
        "barriers:1",
        "beginRendering",
        "endRendering",
        "endLabel",
        "label:Forward",
        "barriers:1",
        "beginRendering",
        "endRendering",
        "endLabel",
        "barriers:0",
    };
    const bool usageValid =
        rhi::hasFlag(device.textures.back().usage, rhi::TextureUsage::DepthStencilAttachment) &&
        rhi::hasFlag(device.textures.back().usage, rhi::TextureUsage::Sampled);
    if (shadowEncoder.events != expectedShadowEvents ||
        shadowEncoder.recordedBarriers.size() != 2 ||
        shadowEncoder.recordedBarriers[0].before != rhi::ResourceState::Undefined ||
        shadowEncoder.recordedBarriers[0].after != rhi::ResourceState::DepthAttachment ||
        shadowEncoder.recordedBarriers[1].before != rhi::ResourceState::DepthAttachment ||
        shadowEncoder.recordedBarriers[1].after != rhi::ResourceState::ShaderRead || !usageValid) {
        return 3;
    }
    return 0;
}

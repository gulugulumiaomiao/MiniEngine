#include "render/render_target/RenderTarget.h"
#include "render/render_graph/RgTexturePool.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace {

class FakeDevice final : public engine::rhi::IDevice {
public:
    engine::rhi::RID createBuffer(const engine::rhi::BufferDesc&) override { return {}; }
    void destroyBuffer(engine::rhi::RID) override {}
    void
    uploadBuffer(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}

    engine::rhi::RID createTexture(const engine::rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        const engine::rhi::RID texture{static_cast<std::uint32_t>(textures.size() - 1),
                                                 1};
        if (failNextView) {
            failNextView = false;
        } else {
            views.push_back({.type = desc.dimension,
                             .format = desc.format,
                             .baseMip = 0,
                             .mipCount = desc.mipCount,
                             .baseLayer = 0,
                             .layerCount = desc.arrayLayers});
            defaultViews.emplace(
                texture.index(),
                engine::rhi::RID{static_cast<std::uint32_t>(views.size() - 1), 1});
        }
        return texture;
    }
    void destroyTexture(engine::rhi::RID handle) override {
        if (!handle)
            return;
        destroyedTextures.push_back(handle);
        const auto view = defaultViews.find(handle.index());
        if (view != defaultViews.end()) {
            destroyedViews.push_back(view->second);
            defaultViews.erase(view);
        }
    }
    void uploadTexture(engine::rhi::RID,
                       std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::RID
    createTextureView(engine::rhi::RID,
                      const engine::rhi::TextureViewDesc& desc) override {
        views.push_back(desc);
        return {static_cast<std::uint32_t>(views.size() - 1), 1};
    }
    engine::rhi::RID
    defaultTextureView(engine::rhi::RID texture) override {
        const auto found = defaultViews.find(texture.index());
        return found == defaultViews.end() ? engine::rhi::RID{} : found->second;
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
    std::unordered_map<std::uint32_t, engine::rhi::RID> defaultViews;
    std::vector<engine::rhi::RID> destroyedTextures;
    std::vector<engine::rhi::RID> destroyedViews;
    bool failNextView{};
};

class FakeEncoder final : public engine::rhi::ICommandBuffer {
public:
    void begin() override { currentState = engine::rhi::CommandState::Recording; }
    void end() override { currentState = engine::rhi::CommandState::Executable; }
    [[nodiscard]] engine::rhi::CommandState state() const override { return currentState; }
    void resourceBarriers(std::span<const engine::rhi::TextureBarrier> values) override {
        barriers.insert(barriers.end(), values.begin(), values.end());
    }
    void beginRendering(const engine::rhi::RenderingInfo&) override {}
    void endRendering() override {}
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
    void beginDebugLabel(std::string_view, const engine::math::Vec4&) override {}
    void endDebugLabel() override {}
    void copyBuffer(const engine::rhi::BufferCopy&) override {}
    void copyImage(const engine::rhi::ImageCopy&) override {}
    void copyBufferToImage(const engine::rhi::BufferImageCopy&) override {}
    void copyImageToBuffer(const engine::rhi::BufferImageCopy&) override {}
    void updateBuffer(const engine::rhi::BufferUpdate&) override {}
    void updateImage(const engine::rhi::ImageUpdate&) override {}

    engine::rhi::CommandState currentState{engine::rhi::CommandState::Initial};
    std::vector<engine::rhi::TextureBarrier> barriers;
};

} // namespace

int main() {
    using namespace engine;

    FakeDevice device;
    RenderTarget target{device};
    RenderTargetDesc desc;
    desc.width = 640;
    desc.height = 360;
    desc.debugName = "Lighting";
    desc.colorAttachments = {
        {.format = rhi::PixelFormat::Rgba8Unorm,
         .additionalUsage = rhi::TextureUsage::Sampled,
         .clearColor = {0.1F, 0.2F, 0.3F, 1.0F}},
        {.format = rhi::PixelFormat::Rgba8Srgb,
         .additionalUsage = rhi::TextureUsage::TransferSource,
         .loadOp = rhi::LoadOp::DontCare,
         .storeOp = rhi::StoreOp::Store},
    };
    desc.depthAttachment = RenderTargetDepthAttachmentDesc{
        .format = rhi::PixelFormat::Depth32Float,
        .additionalUsage = rhi::TextureUsage::Sampled,
        .loadOp = rhi::LoadOp::Clear,
        .storeOp = rhi::StoreOp::DontCare,
        .clearDepth = 0.75F,
    };
    if (!target.create(desc) || !target.valid() || target.colorAttachmentCount() != 2 ||
        !target.hasDepthAttachment() || target.depthFormat() != rhi::PixelFormat::Depth32Float ||
        target.colorFormat(1) != rhi::PixelFormat::Rgba8Srgb || device.textures.size() != 3 ||
        device.views.size() != 3) {
        return 1;
    }
    if (!rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::ColorAttachment) ||
        !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::Sampled) ||
        !rhi::hasFlag(device.textures[1].usage, rhi::TextureUsage::TransferSource) ||
        !rhi::hasFlag(device.textures[2].usage, rhi::TextureUsage::DepthStencilAttachment) ||
        device.views[0].format != rhi::PixelFormat::Rgba8Unorm ||
        device.views[2].format != rhi::PixelFormat::Depth32Float ||
        device.textures[0].debugName != "Lighting.Color0" ||
        device.textures[2].debugName != "Lighting.Depth") {
        return 2;
    }

    RgTexturePool pool{device, 2};
    pool.beginFrame(0);

    const rhi::RenderingInfo rendering = target.renderingInfo();
    if (rendering.renderArea.width != 640 || rendering.renderArea.height != 360 ||
        rendering.colorAttachments.size() != 2 || rendering.depthAttachments.size() != 1 ||
        rendering.colorAttachments[0].clearColor.x != 0.1F ||
        rendering.colorAttachments[1].loadOp != rhi::LoadOp::DontCare ||
        rendering.depthAttachments[0].clearDepth != 0.75F) {
        return 3;
    }

    RenderGraph firstGraph;
    const RgTextureHandle color0 =
        target.importColor(firstGraph, 0, rhi::ResourceState::ShaderRead);
    const RgTextureHandle color1 =
        target.importColor(firstGraph, 1, rhi::ResourceState::ShaderRead);
    const RgTextureHandle depth = target.importDepth(firstGraph, rhi::ResourceState::ShaderRead);
    RgRenderingInfo firstRendering;
    firstRendering.renderArea = {0, 0, 640, 360};
    firstRendering.colorAttachments.push_back(
        {color0, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.1F, 0.2F, 0.3F, 1.0F}});
    firstRendering.colorAttachments.push_back(
        {color1, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}});
    firstRendering.depthAttachments.push_back(
        {depth, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 0.75F});
    firstGraph.addGraphicsPass(
        "Lighting",
        std::move(firstRendering),
        {{color0, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {color1, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {depth, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment}},
        [](rhi::ICommandBuffer&) {});
    firstGraph.compile(pool);
    FakeEncoder firstEncoder;
    firstGraph.execute(firstEncoder);
    firstGraph.reset();
    if (firstEncoder.barriers.size() != 6 ||
        firstEncoder.barriers[0].before != rhi::ResourceState::Undefined ||
        firstEncoder.barriers[0].after != rhi::ResourceState::ColorAttachment ||
        firstEncoder.barriers[2].aspect != rhi::TextureAspect::Depth ||
        firstEncoder.barriers[5].after != rhi::ResourceState::ShaderRead) {
        return 4;
    }

    pool.beginFrame(1);
    RenderGraph secondGraph;
    const RgTextureHandle c0b = target.importColor(secondGraph, 0);
    const RgTextureHandle c1b = target.importColor(secondGraph, 1);
    const RgTextureHandle db = target.importDepth(secondGraph);
    RgRenderingInfo secondRendering;
    secondRendering.renderArea = {0, 0, 640, 360};
    secondRendering.colorAttachments.push_back({c0b, rhi::LoadOp::Clear, rhi::StoreOp::Store, {}});
    secondRendering.colorAttachments.push_back(
        {c1b, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}});
    secondRendering.depthAttachments.push_back(
        {db, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 1.0F});
    secondGraph.addGraphicsPass(
        "Lighting",
        std::move(secondRendering),
        {{c0b, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {c1b, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {db, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment}},
        [](rhi::ICommandBuffer&) {});
    secondGraph.compile(pool);
    FakeEncoder secondEncoder;
    secondGraph.execute(secondEncoder);
    secondGraph.reset();
    if (secondEncoder.barriers.size() != 3 ||
        secondEncoder.barriers[0].before != rhi::ResourceState::ShaderRead ||
        secondEncoder.barriers[2].before != rhi::ResourceState::ShaderRead) {
        return 5;
    }

    const rhi::RID oldColor = target.colorTexture(0);
    device.failNextView = true;
    if (target.resize(800, 600) || target.width() != 640 || target.height() != 360 ||
        target.colorTexture(0) != oldColor || device.destroyedTextures.size() != 1 ||
        !device.destroyedViews.empty()) {
        return 6;
    }
    if (!target.resize(800, 600) || target.width() != 800 || target.height() != 600 ||
        target.colorTexture(0) == oldColor || device.textures.size() != 7 ||
        device.destroyedTextures.size() != 4 || device.destroyedViews.size() != 3 ||
        device.textures[4].width != 800 || device.textures[4].height != 600) {
        return 7;
    }
    if (!target.resize(800, 600) || device.textures.size() != 7) {
        return 8;
    }

    target.release();
    if (target.valid() || device.destroyedTextures.size() != 7 ||
        device.destroyedViews.size() != 6) {
        return 9;
    }
}

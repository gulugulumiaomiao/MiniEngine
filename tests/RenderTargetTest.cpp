#include "render/render_target/RenderTarget.h"
#include "render/render_graph/RgTexturePool.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

class FakeDevice final : public engine::rhi::IDevice {
public:
    engine::rhi::BufferHandle createBuffer(const engine::rhi::BufferDesc&) override { return {}; }
    void destroyBuffer(engine::rhi::BufferHandle) override {}
    void
    uploadBuffer(engine::rhi::BufferHandle, std::span<const std::byte>, std::uint64_t) override {}

    engine::rhi::TextureHandle createTexture(const engine::rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        return {static_cast<std::uint32_t>(textures.size() - 1), 1};
    }
    void destroyTexture(engine::rhi::TextureHandle handle) override {
        if (handle)
            destroyedTextures.push_back(handle);
    }
    void uploadTexture(engine::rhi::TextureHandle,
                       std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::TextureViewHandle
    createTextureView(const engine::rhi::TextureViewDesc& desc) override {
        if (failNextView) {
            failNextView = false;
            return {};
        }
        views.push_back(desc);
        return {static_cast<std::uint32_t>(views.size() - 1), 1};
    }
    void destroyTextureView(engine::rhi::TextureViewHandle handle) override {
        if (handle)
            destroyedViews.push_back(handle);
    }
    engine::rhi::SamplerHandle createSampler(const engine::rhi::SamplerDesc&) override {
        return {};
    }
    void destroySampler(engine::rhi::SamplerHandle) override {}
    engine::rhi::ShaderHandle createShader(const engine::rhi::ShaderDesc&) override { return {}; }
    void destroyShader(engine::rhi::ShaderHandle) override {}
    engine::rhi::GraphicsPipelineHandle
    createGraphicsPipeline(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void destroyGraphicsPipeline(engine::rhi::GraphicsPipelineHandle) override {}
    engine::rhi::BindGroupLayoutHandle
    createBindGroupLayout(const engine::rhi::BindGroupLayoutDesc&) override {
        return {};
    }
    void destroyBindGroupLayout(engine::rhi::BindGroupLayoutHandle) override {}
    engine::rhi::BindGroupHandle createBindGroup(const engine::rhi::BindGroupDesc&) override {
        return {};
    }
    void destroyBindGroup(engine::rhi::BindGroupHandle) override {}
    VkDevice device() const override { return VK_NULL_HANDLE; }
    VkBuffer resolveBuffer(engine::rhi::BufferHandle) const override { return VK_NULL_HANDLE; }
    VkImage resolveTexture(engine::rhi::TextureHandle) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(engine::rhi::TextureViewHandle) const override {
        return VK_NULL_HANDLE;
    }
    engine::rhi::ResolvedPipeline
    resolvePipeline(engine::rhi::GraphicsPipelineHandle) const override {
        return {};
    }
    VkDescriptorSet resolveBindGroup(engine::rhi::BindGroupHandle) const override {
        return VK_NULL_HANDLE;
    }
    void waitIdle() override {}

    std::vector<engine::rhi::TextureDesc> textures;
    std::vector<engine::rhi::TextureViewDesc> views;
    std::vector<engine::rhi::TextureHandle> destroyedTextures;
    std::vector<engine::rhi::TextureViewHandle> destroyedViews;
    bool failNextView{};
};

class FakeEncoder final : public engine::rhi::IGraphicsCommandEncoder {
public:
    void resourceBarriers(std::span<const engine::rhi::TextureBarrier> values) override {
        barriers.insert(barriers.end(), values.begin(), values.end());
    }
    void beginRendering(const engine::rhi::RenderingInfo&) override {}
    void endRendering() override {}
    void setViewport(const engine::rhi::Viewport&) override {}
    void setScissor(const engine::rhi::Rect&) override {}
    void bindPipeline(engine::rhi::GraphicsPipelineHandle) override {}
    void bindVertexBuffer(std::uint32_t, engine::rhi::BufferHandle, std::uint64_t) override {}
    void
    bindIndexBuffer(engine::rhi::BufferHandle, std::uint64_t, engine::rhi::IndexFormat) override {}
    void bindGroup(std::uint32_t,
                   engine::rhi::BindGroupHandle,
                   std::span<const std::uint32_t>) override {}
    void draw(const engine::rhi::DrawArguments&) override {}
    void drawIndexed(const engine::rhi::DrawIndexedArguments&) override {}
    void beginDebugLabel(std::string_view, const engine::math::Vec4&) override {}
    void endDebugLabel() override {}

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
        {.format = rhi::TextureFormat::Rgba8Unorm,
         .additionalUsage = rhi::TextureUsage::Sampled,
         .clearColor = {0.1F, 0.2F, 0.3F, 1.0F}},
        {.format = rhi::TextureFormat::Rgba8Srgb,
         .additionalUsage = rhi::TextureUsage::TransferSource,
         .loadOp = rhi::LoadOp::DontCare,
         .storeOp = rhi::StoreOp::Store},
    };
    desc.depthAttachment = RenderTargetDepthAttachmentDesc{
        .format = rhi::TextureFormat::Depth32Float,
        .additionalUsage = rhi::TextureUsage::Sampled,
        .loadOp = rhi::LoadOp::Clear,
        .storeOp = rhi::StoreOp::DontCare,
        .clearDepth = 0.75F,
    };
    if (!target.create(desc) || !target.valid() || target.colorAttachmentCount() != 2 ||
        !target.hasDepthAttachment() || target.depthFormat() != rhi::TextureFormat::Depth32Float ||
        target.colorFormat(1) != rhi::TextureFormat::Rgba8Srgb || device.textures.size() != 3 ||
        device.views.size() != 3) {
        return 1;
    }
    if (!rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::ColorAttachment) ||
        !rhi::hasFlag(device.textures[0].usage, rhi::TextureUsage::Sampled) ||
        !rhi::hasFlag(device.textures[1].usage, rhi::TextureUsage::TransferSource) ||
        !rhi::hasFlag(device.textures[2].usage, rhi::TextureUsage::DepthStencilAttachment) ||
        device.views[0].aspect != rhi::TextureAspect::Color ||
        device.views[2].aspect != rhi::TextureAspect::Depth ||
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
    const RgTextureHandle color0 = target.importColor(firstGraph, 0, rhi::ResourceState::ShaderRead);
    const RgTextureHandle color1 = target.importColor(firstGraph, 1, rhi::ResourceState::ShaderRead);
    const RgTextureHandle depth = target.importDepth(firstGraph, rhi::ResourceState::ShaderRead);
    RgRenderingInfo firstRendering;
    firstRendering.renderArea = {0, 0, 640, 360};
    firstRendering.colorAttachments.push_back({color0, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.1F, 0.2F, 0.3F, 1.0F}});
    firstRendering.colorAttachments.push_back({color1, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}});
    firstRendering.depthAttachments.push_back({depth, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 0.75F});
    firstGraph.addGraphicsPass("Lighting",
                               std::move(firstRendering),
                               {{color0, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
                                {color1, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
                                {depth, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment}},
                               [](rhi::IGraphicsCommandEncoder&) {});
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
    secondRendering.colorAttachments.push_back({c1b, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {}});
    secondRendering.depthAttachments.push_back({db, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 1.0F});
    secondGraph.addGraphicsPass("Lighting",
                                std::move(secondRendering),
                                {{c0b, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
                                 {c1b, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
                                 {db, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment}},
                                [](rhi::IGraphicsCommandEncoder&) {});
    secondGraph.compile(pool);
    FakeEncoder secondEncoder;
    secondGraph.execute(secondEncoder);
    secondGraph.reset();
    if (secondEncoder.barriers.size() != 3 ||
        secondEncoder.barriers[0].before != rhi::ResourceState::ShaderRead ||
        secondEncoder.barriers[2].before != rhi::ResourceState::ShaderRead) {
        return 5;
    }

    const rhi::TextureHandle oldColor = target.colorTexture(0);
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

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
    engine::rhi::RID buffer_allocate_rid(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_allocate_memory(engine::rhi::RID) override {}
    void buffer_free_memory(engine::rhi::RID) override {}
    void buffer_release_rid(engine::rhi::RID) override {}
    engine::rhi::RID buffer_create(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_destroy(engine::rhi::RID) override {}
    void buffer_upload(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}
    engine::rhi::RID buffer_acquire_transient(const engine::rhi::BufferDesc&) override { return {}; }

    engine::rhi::RID texture_create(const engine::rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        const engine::rhi::RID texture{static_cast<std::uint32_t>(textures.size() - 1), 1};
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
    void texture_destroy(engine::rhi::RID handle) override {
        if (!handle)
            return;
        destroyedTextures.push_back(handle);
        const auto view = defaultViews.find(handle.index());
        if (view != defaultViews.end()) {
            destroyedViews.push_back(view->second);
            defaultViews.erase(view);
        }
    }
    void texture_upload(engine::rhi::RID,
                        std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::RID
    texture_view_create(engine::rhi::RID, const engine::rhi::TextureViewDesc& desc) override {
        views.push_back(desc);
        return {static_cast<std::uint32_t>(views.size() - 1), 1};
    }
    engine::rhi::RID texture_default_view(engine::rhi::RID texture) override {
        const auto found = defaultViews.find(texture.index());
        return found == defaultViews.end() ? engine::rhi::RID{} : found->second;
    }
    void texture_view_destroy(engine::rhi::RID handle) override {
        if (handle)
            destroyedViews.push_back(handle);
    }
    engine::rhi::RID sampler_create(const engine::rhi::SamplerDesc&) override { return {}; }
    void sampler_destroy(engine::rhi::RID) override {}
    engine::rhi::RID shader_create(const engine::rhi::ShaderDesc&) override { return {}; }
    void shader_destroy(engine::rhi::RID) override {}
    engine::rhi::RID pipeline_create(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void pipeline_destroy(engine::rhi::RID) override {}
    engine::rhi::RID
    bind_group_layout_create(const engine::rhi::BindGroupLayoutDesc&) override {
        return {};
    }
    void bind_group_layout_destroy(engine::rhi::RID) override {}
    engine::rhi::RID bind_group_create(const engine::rhi::BindGroupDesc&) override { return {}; }
    void bind_group_destroy(engine::rhi::RID) override {}
    void waitIdle() override {}

    std::vector<engine::rhi::TextureDesc> textures;
    std::vector<engine::rhi::TextureViewDesc> views;
    std::unordered_map<std::uint32_t, engine::rhi::RID> defaultViews;
    std::vector<engine::rhi::RID> destroyedTextures;
    std::vector<engine::rhi::RID> destroyedViews;
    bool failNextView{};
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

    const rhi::RenderingInfo rendering = target.renderingInfo();
    if (rendering.renderArea.width != 640 || rendering.renderArea.height != 360 ||
        rendering.colorAttachments.size() != 2 || rendering.depthAttachments.size() != 1 ||
        rendering.colorAttachments[0].clearColor.x != 0.1F ||
        rendering.colorAttachments[1].loadOp != rhi::LoadOp::DontCare ||
        rendering.depthAttachments[0].clearDepth != 0.75F) {
        return 3;
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

#include "render/render_graph/RgTexturePool.h"
#include "rhi/api/Device.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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

engine::RgTextureDesc makeDesc(std::uint32_t width, std::uint32_t height) {
    return {.format = engine::rhi::PixelFormat::Rgba8Unorm,
            .width = width,
            .height = height,
            .usage = engine::rhi::TextureUsage::ColorAttachment,
            .aspect = engine::rhi::TextureAspect::Color,
            .debugName = "Pool"};
}

} // namespace

int main() {
    using namespace engine;

    FakeDevice device;
    RgTexturePool pool{device, 2};

    if (pool.framesInFlight() != 2 || pool.totalEntryCount() != 0 || pool.inUseCount() != 0) {
        return 1;
    }

    pool.beginFrame(0);
    RgTexturePool::PooledTexture* a = pool.acquire(makeDesc(128, 128));
    if (!a || a->texture.index() != 1 || a->view.index() != 1 || pool.inUseCount() != 1 ||
        pool.totalEntryCount() != 1) {
        return 2;
    }

    // Release and re-acquire: the same entry should be reused.
    pool.release(a);
    RgTexturePool::PooledTexture* b = pool.acquire(makeDesc(128, 128));
    if (b != a || pool.inUseCount() != 1 || pool.totalEntryCount() != 1) {
        return 3;
    }

    // Different size: new entry.
    RgTexturePool::PooledTexture* c = pool.acquire(makeDesc(256, 256));
    if (c == b || c->texture.index() != 2 || pool.inUseCount() != 2 || pool.totalEntryCount() != 2) {
        return 4;
    }

    pool.release(b);
    pool.release(c);
    if (pool.inUseCount() != 0) {
        return 5;
    }

    // Next frame uses bucket 1, so it cannot reuse the entry from frame 0 (bucket 0).
    pool.beginFrame(1);
    if (pool.inUseCount() != 0) {
        return 6;
    }

    RgTexturePool::PooledTexture* d = pool.acquire(makeDesc(128, 128));
    if (d == b || d->texture.index() != 3 || pool.inUseCount() != 1 || pool.totalEntryCount() != 3) {
        return 7;
    }

    // Two frames later: bucket 0 is available again.
    pool.release(d);
    pool.beginFrame(2);
    RgTexturePool::PooledTexture* e = pool.acquire(makeDesc(128, 128));
    if (e != b || pool.inUseCount() != 1 || pool.totalEntryCount() != 3) {
        return 8;
    }

    return 0;
}

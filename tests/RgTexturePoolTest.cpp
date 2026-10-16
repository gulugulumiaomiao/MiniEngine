#include "TestTextureDesc.h"
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
    engine::rhi::RID buffer_allocate_rid(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_allocate_memory(engine::rhi::RID) override {}
    void buffer_free_memory(engine::rhi::RID) override {}
    void buffer_release_rid(engine::rhi::RID) override {}
    engine::rhi::RID buffer_create(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_destroy(engine::rhi::RID) override {}
    void buffer_upload(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}
    engine::rhi::RID buffer_acquire_transient(const engine::rhi::BufferDesc&) override { return {}; }

    engine::rhi::TextureFormatCapabilities texture_format_capabilities(
        engine::rhi::PixelFormat format, engine::rhi::TextureTiling = engine::rhi::TextureTiling::Optimal) const override {
        const bool valid = engine::rhi::formatInfo(format).valid();
        const bool color = engine::rhi::isColorFormat(format);
        return {valid, valid, color, engine::rhi::isDepthFormat(format), color, valid, valid};
    }
    engine::rhi::RID texture_create(const engine::rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        views.push_back({.type = desc.dimension,
                         .format = desc.format,
                         .baseMip = 0,
                         .mipCount = desc.mipCount,
                         .baseLayer = 0,
                         .layerCount = desc.arrayLayers});
        return {static_cast<std::uint32_t>(textures.size()), 1};
    }
    void texture_destroy(engine::rhi::RID handle) override {
        if (handle)
            destroyedTextures.push_back(handle);
    }
    void texture_upload(engine::rhi::RID,
                        std::span<const engine::rhi::TextureUploadRegion>) override {}
    engine::rhi::RID
    texture_view_create(engine::rhi::RID, const engine::rhi::TextureViewDesc& desc) override {
        views.push_back(desc);
        return {static_cast<std::uint32_t>(views.size()), 1};
    }
    engine::rhi::RID texture_default_view(engine::rhi::RID) override {
        return views.empty() ? engine::rhi::RID{}
                             : engine::rhi::RID{static_cast<std::uint32_t>(views.size()), 1};
    }
    void texture_view_destroy(engine::rhi::RID handle) override {
        if (handle)
            destroyedViews.push_back(handle);
    }
    engine::rhi::RID sampler_create(const engine::rhi::SamplerDesc&) override { return {1, 1}; }
    void sampler_destroy(engine::rhi::RID) override {}
    engine::rhi::RID shader_create(const engine::rhi::ShaderDesc&) override { return {}; }
    void shader_destroy(engine::rhi::RID) override {}
    engine::rhi::RID pipeline_create(const engine::rhi::GraphicsPipelineDesc&) override {
        return {};
    }
    void pipeline_destroy(engine::rhi::RID) override {}
    engine::rhi::RID shader_allocate_rid() override { return {}; }
    void shader_release_rid(engine::rhi::RID) override {}
    engine::rhi::RID descriptor_set_layout_allocate_rid() override { return {}; }
    void descriptor_set_layout_build(engine::rhi::RID,
                                     const engine::rhi::DescriptorSetLayoutDesc&) override {}
    void descriptor_set_layout_destroy(engine::rhi::RID) override {}
    engine::rhi::RID pipeline_layout_create(const engine::rhi::PipelineLayoutDesc&) override {
        return {};
    }
    void pipeline_layout_destroy(engine::rhi::RID) override {}
    engine::rhi::RID descriptor_set_allocate(const engine::rhi::DescriptorSetAllocateInfo&) override {
        return {};
    }
    void descriptor_set_update(engine::rhi::RID,
                               std::span<const engine::rhi::DescriptorWrite>) override {}
    void descriptor_set_destroy(engine::rhi::RID) override {}
    void waitIdle() override {}

    std::vector<engine::rhi::TextureDesc> textures;
    std::vector<engine::rhi::TextureViewDesc> views;
    std::vector<engine::rhi::RID> destroyedTextures;
    std::vector<engine::rhi::RID> destroyedViews;
};

engine::TextureDesc makeDesc(std::uint32_t width, std::uint32_t height) {
    return engine::test::textureDesc(engine::TextureFormat::Rgba8Unorm, width, height, "Pool");
}

} // namespace

int main() {
    using namespace engine;
    using Status = RgTexturePool::Status;
    FakeDevice device;
    RgTexturePool pool;
    auto desc = makeDesc(128, 128);
    auto* a = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (!a || !a->texture || pool.totalEntryCount() != 1 || pool.inUseCount() != 1) return 1;
    pool.release(a);
    auto* b = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (b != a || b->status() != Status::Leased) return 2;

    pool.beginBatch();
    pool.recordUsage(b, rhi::ResourceState::ShaderRead, rhi::AccessMode::Read);
    pool.release(b);
    if (b->status() != Status::PendingSubmit || b->lastState != rhi::ResourceState::Undefined) return 3;
    auto* c = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (c == b || pool.totalEntryCount() != 2) return 4;
    pool.release(c);
    pool.onSubmitted(1);
    if (b->status() != Status::InFlight || b->lastState != rhi::ResourceState::ShaderRead) return 5;
    pool.collect(0);
    auto* beforeCompletion = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (beforeCompletion == b) return 6;
    pool.release(beforeCompletion);
    pool.collect(1);
    auto* reused = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (reused != b || reused->lastState != rhi::ResourceState::ShaderRead) return 7;

    pool.beginBatch();
    pool.recordUsage(reused, rhi::ResourceState::ColorAttachment, rhi::AccessMode::Write);
    pool.onSubmitted(2);
    pool.collect(2); // GPU completion alone cannot release the CPU lease.
    auto* stillLeased = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (stillLeased == reused) return 8;
    pool.release(stillLeased);
    pool.release(reused);
    auto* completedAndReleased = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (completedAndReleased != reused) return 9;

    pool.beginBatch();
    rhi::ResourceState imported = rhi::ResourceState::Undefined;
    pool.recordUsage(completedAndReleased, rhi::ResourceState::ShaderRead, rhi::AccessMode::Read);
    pool.recordState(&imported, rhi::ResourceState::ShaderRead);
    if (imported != rhi::ResourceState::Undefined || pool.trackedState(&imported) != rhi::ResourceState::ShaderRead) return 10;
    pool.release(completedAndReleased);
    pool.cancelBatch();
    if (completedAndReleased->status() != Status::Available ||
        completedAndReleased->lastState != rhi::ResourceState::ColorAttachment ||
        imported != rhi::ResourceState::Undefined) return 11;

    pool.beginBatch();
    pool.recordState(&imported, rhi::ResourceState::CopySource);
    pool.onSubmitted(3);
    if (imported != rhi::ResourceState::CopySource) return 12;
    pool.collect(3);

    auto multisampled = desc;
    multisampled.samples = 4;
    auto* msaa = pool.acquire(multisampled, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    auto sampled = desc;
    auto* sampledEntry = pool.acquire(sampled, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSource);
    if (msaa == b || sampledEntry == b || msaa == sampledEntry) return 13;
    pool.release(msaa); pool.release(sampledEntry);

    // Many new hash groups must not invalidate an entry/bin used by release.
    auto* stable = pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    for (std::uint32_t size = 16; size < 512; ++size) {
        auto* other = pool.acquire(makeDesc(size, 32), rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
        pool.release(other);
    }
    pool.release(stable);
    if (pool.acquire(desc, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled) != stable) return 14;
    pool.release(stable);
    return pool.inUseCount() == 0 ? 0 : 15;
}

#include "render/gpu/texture/TextureGpuCache.h"
#include "render/gpu/texture/TextureGpuFactory.h"

#include "render/texture/Texture.h"
#include "render/texture/TextureManager.h"
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
        textureDescs.push_back(desc);
        ++createdTextures;
        return {createdTextures - 1, 1};
    }
    void destroyTexture(engine::rhi::TextureHandle handle) override {
        if (handle)
            ++destroyedTextures;
    }
    void uploadTexture(engine::rhi::TextureHandle,
                       std::span<const engine::rhi::TextureUploadRegion> regions) override {
        ++textureUploads;
        uploadedMipCounts.push_back(static_cast<std::uint32_t>(regions.size()));
        uploadedByteCounts.push_back(0);
        for (const engine::rhi::TextureUploadRegion& region : regions)
            uploadedByteCounts.back() += region.data.size_bytes();
    }
    engine::rhi::TextureViewHandle
    createTextureView(const engine::rhi::TextureViewDesc& desc) override {
        viewDescs.push_back(desc);
        ++createdViews;
        return {createdViews - 1, 1};
    }
    void destroyTextureView(engine::rhi::TextureViewHandle handle) override {
        if (handle)
            ++destroyedViews;
    }
    engine::rhi::SamplerHandle createSampler(const engine::rhi::SamplerDesc& desc) override {
        samplerDesc = desc;
        ++createdSamplers;
        return {createdSamplers - 1, 1};
    }
    void destroySampler(engine::rhi::SamplerHandle handle) override {
        if (handle)
            ++destroyedSamplers;
    }

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
    void waitIdle() override { ++waits; }

    std::vector<engine::rhi::TextureDesc> textureDescs;
    std::vector<engine::rhi::TextureViewDesc> viewDescs;
    std::vector<std::uint32_t> uploadedMipCounts;
    std::vector<std::size_t> uploadedByteCounts;
    engine::rhi::SamplerDesc samplerDesc;
    std::uint32_t createdTextures{};
    std::uint32_t destroyedTextures{};
    std::uint32_t textureUploads{};
    std::uint32_t createdViews{};
    std::uint32_t destroyedViews{};
    std::uint32_t createdSamplers{};
    std::uint32_t destroyedSamplers{};
    std::uint32_t waits{};
};

} // namespace

int main() {
    using namespace engine;

    TEXTURE_MANAGER.clear();
    const TextureHandle white = TEXTURE_MANAGER.defaultWhite();
    const TextureHandle black = TEXTURE_MANAGER.defaultBlack();
    const TextureHandle normal = TEXTURE_MANAGER.defaultNormal();
    const TextureHandle error = TEXTURE_MANAGER.errorTexture();
    const Texture* whiteTexture = TEXTURE_MANAGER.find(white);
    const Texture* normalTexture = TEXTURE_MANAGER.find(normal);
    const Texture* errorTexture = TEXTURE_MANAGER.find(error);
    if (!white || !black || !normal || !error || white == black || black == normal ||
        normal == error || !whiteTexture || !normalTexture || !errorTexture ||
        whiteTexture->desc().format != TextureFormat::Rgba8Srgb ||
        normalTexture->desc().format != TextureFormat::Rgba8Unorm ||
        errorTexture->desc().width != 2 || errorTexture->mipData()[0].bytes.size() != 16) {
        return 1;
    }

    FakeDevice device;
    {
        TextureGpuCache cache;
        TextureGpuFactory factory{device};
        Texture* texture = TEXTURE_MANAGER.find(white);
        TextureGpuResource firstResource;
        if (!factory.create({*texture}, firstResource))
            return 2;
        const TextureGpuCacheKey firstKey = TextureGpuCache::key(white, texture->version());
        (void)cache.put(firstKey, std::move(firstResource));
        texture->markClean();
        const TextureGpuResource* first = cache.find(firstKey);
        const TextureGpuResource* cached = cache.find(firstKey);
        if (!first || !cached || first->view != cached->view || cache.size() != 1 ||
            device.createdSamplers != 0 || device.createdTextures != 1 ||
            device.createdViews != 1 || device.textureUploads != 1 ||
            device.uploadedMipCounts[0] != 1 || device.uploadedByteCounts[0] != 4) {
            return 2;
        }

        TextureAsset replacement;
        replacement.setAssetPath(whiteTexture->assetPath());
        replacement.desc = whiteTexture->desc();
        replacement.mipData = {
            {1, 1, {std::byte{0x80}, std::byte{0x80}, std::byte{0x80}, std::byte{0xff}}}};
        if (!TEXTURE_MANAGER.replace(white, replacement.instantiate()))
            return 3;
        texture = TEXTURE_MANAGER.find(white);
        const rhi::TextureViewHandle firstView = first->view;
        TextureGpuResource refreshedResource;
        if (!factory.create({*texture}, refreshedResource))
            return 4;
        auto oldResources =
            cache.extractIf([sourceKey = TextureGpuCache::sourceKey(white)](
                                const TextureGpuCacheKey& key, const TextureGpuResource&) {
                return key.source == sourceKey;
            });
        device.waitIdle();
        for (auto& [unused, resource] : oldResources) {
            (void)unused;
            factory.release(resource);
        }
        const TextureGpuCacheKey refreshedKey = TextureGpuCache::key(white, texture->version());
        (void)cache.put(refreshedKey, std::move(refreshedResource));
        texture->markClean();
        const TextureGpuResource* refreshed = cache.find(refreshedKey);
        if (!refreshed || refreshed->view == firstView || device.waits != 1 ||
            device.createdTextures != 2 || device.destroyedTextures != 1 ||
            device.destroyedViews != 1 || device.textureUploads != 2) {
            return 4;
        }
        auto removed = cache.extractAll();
        device.waitIdle();
        for (auto& [unused, resource] : removed) {
            (void)unused;
            factory.release(resource);
        }
        if (cache.size() != 0 || device.waits != 2 || device.destroyedTextures != 2 ||
            device.destroyedViews != 2) {
            return 5;
        }
    }
    if (device.destroyedSamplers != 0)
        return 6;
    TEXTURE_MANAGER.clear();
    return 0;
}

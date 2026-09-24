#include "render/material/Material.h"
#include "render/texture/Sampler.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace {

class FakeRhiTexture final : public engine::rhi::IRHITexture {
public:
    FakeRhiTexture(engine::rhi::TextureDesc desc,
                   engine::rhi::RID defaultView,
                   std::vector<engine::rhi::TextureViewDesc>& viewDescs,
                   std::vector<engine::rhi::RID>& viewTextures,
                   std::uint32_t& createdViews,
                   engine::rhi::RID texture)
        : desc_(std::move(desc)), defaultView_(defaultView), viewDescs_(viewDescs),
          viewTextures_(viewTextures), createdViews_(createdViews), texture_(texture) {
        views_.emplace(engine::rhi::TextureViewDesc{.type = desc_.dimension,
                                                    .format = desc_.format,
                                                    .baseMip = 0,
                                                    .mipCount = desc_.mipCount,
                                                    .baseLayer = 0,
                                                    .layerCount = desc_.arrayLayers},
                       defaultView_);
    }

    engine::rhi::TextureType type() const override { return desc_.dimension; }
    engine::rhi::PixelFormat format() const override { return desc_.format; }
    std::uint32_t width() const override { return desc_.width; }
    std::uint32_t height() const override { return desc_.height; }
    std::uint32_t depth() const override { return desc_.depth; }
    std::uint32_t arrayLayers() const override { return desc_.arrayLayers; }
    std::uint32_t mipCount() const override { return desc_.mipCount; }
    engine::rhi::RID defaultView() const override { return defaultView_; }
    engine::rhi::RID createView(const engine::rhi::TextureViewDesc& desc) override {
        engine::rhi::TextureViewDesc normalized = desc;
        if (normalized.format == engine::rhi::PixelFormat::Undefined)
            normalized.format = desc_.format;
        if (const auto found = views_.find(normalized); found != views_.end())
            return found->second;
        viewDescs_.push_back(normalized);
        viewTextures_.push_back(texture_);
        const engine::rhi::RID handle{++createdViews_, 1};
        views_.emplace(normalized, handle);
        return handle;
    }

private:
    engine::rhi::TextureDesc desc_;
    engine::rhi::RID defaultView_;
    std::vector<engine::rhi::TextureViewDesc>& viewDescs_;
    std::vector<engine::rhi::RID>& viewTextures_;
    std::uint32_t& createdViews_;
    engine::rhi::RID texture_;
    std::unordered_map<engine::rhi::TextureViewDesc,
                       engine::rhi::RID,
                       engine::rhi::TextureViewDescHash>
        views_;
};

class FakeDevice final : public engine::rhi::IDevice {
public:
    engine::rhi::RID createBuffer(const engine::rhi::BufferDesc&) override { return {}; }
    void destroyBuffer(engine::rhi::RID) override {}
    void
    uploadBuffer(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}

    engine::rhi::RID createTexture(const engine::rhi::TextureDesc& desc) override {
        textureDescs.push_back(desc);
        const engine::rhi::RID texture{++createdTextures, 1};
        const engine::rhi::RID defaultView{++createdViews, 1};
        textures.push_back(std::make_unique<FakeRhiTexture>(
            desc, defaultView, viewDescs, viewTextures, createdViews, texture));
        return texture;
    }
    void destroyTexture(engine::rhi::RID handle) override {
        if (handle)
            ++destroyedTextures;
    }
    void uploadTexture(engine::rhi::RID,
                       std::span<const engine::rhi::TextureUploadRegion> regions) override {
        ++textureUploads;
        uploadedMipCounts.push_back(static_cast<std::uint32_t>(regions.size()));
        uploadedByteCounts.push_back(0);
        for (const engine::rhi::TextureUploadRegion& region : regions)
            uploadedByteCounts.back() += region.data.size_bytes();
    }
    engine::rhi::RID
    createTextureView(engine::rhi::RID texture,
                      const engine::rhi::TextureViewDesc& desc) override {
        viewDescs.push_back(desc);
        viewTextures.push_back(texture);
        return {++createdViews, 1};
    }
    engine::rhi::RID
    defaultTextureView(engine::rhi::RID texture) const override {
        const auto* resource = resolveTextureResource(texture);
        return resource ? resource->defaultView() : engine::rhi::RID{};
    }
    void destroyTextureView(engine::rhi::RID) override {}
    engine::rhi::RID createSampler(const engine::rhi::SamplerDesc& desc) override {
        samplerDescs.push_back(desc);
        return {static_cast<std::uint32_t>(samplerDescs.size()), 1};
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
    engine::rhi::IRHITexture* resolveTextureResource(engine::rhi::RID handle) override {
        return handle.index() > 0 && handle.index() <= textures.size()
                   ? textures[handle.index() - 1].get()
                   : nullptr;
    }
    const engine::rhi::IRHITexture*
    resolveTextureResource(engine::rhi::RID handle) const override {
        return handle.index() > 0 && handle.index() <= textures.size()
                   ? textures[handle.index() - 1].get()
                   : nullptr;
    }
    void waitIdle() override { ++waits; }

    std::vector<engine::rhi::TextureDesc> textureDescs;
    std::vector<std::unique_ptr<FakeRhiTexture>> textures;
    std::vector<engine::rhi::TextureViewDesc> viewDescs;
    std::vector<engine::rhi::RID> viewTextures;
    std::vector<engine::rhi::SamplerDesc> samplerDescs;
    std::vector<std::uint32_t> uploadedMipCounts;
    std::vector<std::size_t> uploadedByteCounts;
    std::uint32_t createdTextures{};
    std::uint32_t destroyedTextures{};
    std::uint32_t createdViews{};
    std::uint32_t textureUploads{};
    std::uint32_t waits{};
};

// Constructing the FakeDevice registers it as the process-wide active device (IDevice
// singleton), which is how layer-2 Texture/Sampler reach a backend without a manager.
class TextureTest : public ::testing::Test {
protected:
    FakeDevice device;
};

TEST_F(TextureTest, BuiltinsAreDistinctUploadedAndSampleable) {
    using namespace engine;
    const Ref<Texture> white = Texture::defaultWhite();
    const Ref<Texture> black = Texture::defaultBlack();
    const Ref<Texture> normal = Texture::defaultNormal();
    const Ref<Texture> error = Texture::errorTexture();

    ASSERT_TRUE(white);
    ASSERT_TRUE(black);
    ASSERT_TRUE(normal);
    ASSERT_TRUE(error);
    EXPECT_NE(white, black);
    EXPECT_NE(black, normal);
    EXPECT_NE(normal, error);
    EXPECT_EQ(white->format(), TextureFormat::Rgba8Srgb);
    EXPECT_EQ(normal->format(), TextureFormat::Rgba8Unorm);
    EXPECT_EQ(error->width(), 2U);
    EXPECT_FALSE(white->isAssetBacked());

    // Each builtin owns an RHI texture, a default view and a device sampler, and uploaded once.
    EXPECT_TRUE(white->textureHandle());
    EXPECT_TRUE(white->defaultView());
    EXPECT_TRUE(white->defaultSampler());
    const rhi::TextureBinding binding = white->binding();
    EXPECT_TRUE(binding.view);
    EXPECT_TRUE(binding.sampler);
    EXPECT_EQ(binding.view, white->defaultView());
    EXPECT_EQ(binding.sampler, white->defaultSampler());
    EXPECT_GE(device.textureUploads, 4U);
}

TEST_F(TextureTest, InstantiateIsSingleInstanceAndCloneIsDetached) {
    using namespace engine;
    Ref<TextureAsset> asset = makeRef<TextureAsset>();
    asset->desc = TextureDesc{
        TextureType::Texture2D, TextureFormat::Rgba8Srgb, TextureColorSpace::Srgb, 1, 1, 1};
    asset->pixels = {0xffU, 0xffU, 0xffU, 0xffU};

    const Ref<Texture> first = asset->instantiate();
    const Ref<Texture> second = asset->instantiate();
    ASSERT_TRUE(first);
    EXPECT_EQ(first, second); // repeated instantiate reuses the single instance
    EXPECT_TRUE(first->isAssetBacked());
    EXPECT_EQ(first->width(), 1U);
    EXPECT_TRUE(first->binding().view);

    const Ref<Texture> cloned = asset->clone();
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, first);            // clone is an independent runtime texture
    EXPECT_FALSE(cloned->isAssetBacked()); // and is not linked back to the asset
    EXPECT_EQ(cloned->width(), 1U);
}

TEST_F(TextureTest, MaterialRetainsTextureByRef) {
    using namespace engine;
    Ref<Texture> texture = Texture::defaultWhite();
    ASSERT_TRUE(texture);
    const std::uint32_t before = texture.useCount();

    Material material;
    material.textures.emplace("mainTexture", "");
    material.setTexture("mainTexture", texture);

    EXPECT_EQ(material.resolveTexture("mainTexture"), texture);
    EXPECT_EQ(texture.useCount(), before + 1);
    EXPECT_TRUE(material.resolveSampler("mainTexture") == nullptr);
}

TEST_F(TextureTest, SamplerOverrideKeepsDefaultViewButSwapsSampler) {
    using namespace engine;
    const Ref<Texture> white = Texture::defaultWhite();
    ASSERT_TRUE(white);

    const SamplerDesc pointDesc{
        TextureFilterMode::Point, TextureAddressMode::Repeat, TextureAddressMode::Repeat, 1.0F};
    const Ref<Sampler> point = Sampler::resolve(pointDesc);
    ASSERT_TRUE(point);

    const rhi::TextureBinding defaultBinding = white->binding();
    const rhi::TextureBinding pointBinding = white->binding(point);
    EXPECT_EQ(defaultBinding.view, pointBinding.view);        // same default view
    EXPECT_NE(defaultBinding.sampler, pointBinding.sampler);  // overridden sampler
    EXPECT_EQ(pointBinding.sampler, point->rhiHandle());
}

TEST(TextureValidationTest, RejectsUnimplementedTextureDimensions) {
    using namespace engine;
    TextureDesc desc{
        TextureType::Texture2DArray, TextureFormat::Rgba8Unorm, TextureColorSpace::Linear, 1, 1, 1};
    desc.arrayLayers = 2;
    const std::vector<std::uint8_t> pixels{0, 0, 0, 0};
    EXPECT_FALSE(validateTexture(desc, pixels));
}

} // namespace

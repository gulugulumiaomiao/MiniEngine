#include "TestMaterial.h"
#include "asset/types/TextureAsset.h"
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

class FakeRhiTexture final {
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

    engine::rhi::TextureType type() const { return desc_.dimension; }
    engine::rhi::PixelFormat format() const { return desc_.format; }
    std::uint32_t width() const { return desc_.width; }
    std::uint32_t height() const { return desc_.height; }
    std::uint32_t depth() const { return desc_.depth; }
    std::uint32_t arrayLayers() const { return desc_.arrayLayers; }
    std::uint32_t mipCount() const { return desc_.mipCount; }
    engine::rhi::RID defaultView() const { return defaultView_; }
    engine::rhi::RID createView(const engine::rhi::TextureViewDesc& desc) {
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
    engine::rhi::RID buffer_allocate_rid(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_allocate_memory(engine::rhi::RID) override {}
    void buffer_free_memory(engine::rhi::RID) override {}
    void buffer_release_rid(engine::rhi::RID) override {}
    engine::rhi::RID buffer_create(const engine::rhi::BufferDesc&) override { return {}; }
    void buffer_destroy(engine::rhi::RID) override {}
    void buffer_upload(engine::rhi::RID, std::span<const std::byte>, std::uint64_t) override {}
    engine::rhi::RID buffer_acquire_transient(const engine::rhi::BufferDesc&) override {
        return {};
    }

    engine::rhi::RID texture_create(const engine::rhi::TextureDesc& desc) override {
        textureDescs.push_back(desc);
        const engine::rhi::RID texture{++createdTextures, 1};
        const engine::rhi::RID defaultView{++createdViews, 1};
        textures.push_back(std::make_unique<FakeRhiTexture>(
            desc, defaultView, viewDescs, viewTextures, createdViews, texture));
        return texture;
    }
    void texture_destroy(engine::rhi::RID handle) override {
        if (handle)
            ++destroyedTextures;
    }
    void texture_upload(engine::rhi::RID,
                        std::span<const engine::rhi::TextureUploadRegion> regions) override {
        ++textureUploads;
        uploadedMipCounts.push_back(static_cast<std::uint32_t>(regions.size()));
        uploadedByteCounts.push_back(0);
        for (const engine::rhi::TextureUploadRegion& region : regions)
            uploadedByteCounts.back() += region.data.size_bytes();
    }
    engine::rhi::RID texture_view_create(engine::rhi::RID texture,
                                         const engine::rhi::TextureViewDesc& desc) override {
        viewDescs.push_back(desc);
        viewTextures.push_back(texture);
        return {++createdViews, 1};
    }
    engine::rhi::RID texture_default_view(engine::rhi::RID texture) override {
        return texture.index() > 0 && texture.index() <= textures.size()
                   ? textures[texture.index() - 1]->defaultView()
                   : engine::rhi::RID{};
    }
    void texture_view_destroy(engine::rhi::RID) override {}
    engine::rhi::RID sampler_create(const engine::rhi::SamplerDesc& desc) override {
        samplerDescs.push_back(desc);
        return {static_cast<std::uint32_t>(samplerDescs.size()), 1};
    }
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
    engine::rhi::RID
    descriptor_set_allocate(const engine::rhi::DescriptorSetAllocateInfo&) override {
        return {};
    }
    void descriptor_set_update(engine::rhi::RID,
                               std::span<const engine::rhi::DescriptorWrite>) override {}
    void descriptor_set_destroy(engine::rhi::RID) override {}
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
    EXPECT_NE(cloned, first);              // clone is an independent runtime texture
    EXPECT_FALSE(cloned->isAssetBacked()); // and is not linked back to the asset
    EXPECT_EQ(cloned->width(), 1U);
}

TEST_F(TextureTest, MaterialRetainsTextureByRef) {
    using namespace engine;
    Ref<Texture> texture = Texture::defaultWhite();
    ASSERT_TRUE(texture);
    const std::uint32_t before = texture.useCount();

    Material material{
        test::materialInfo({{"mainTexture", ShaderPropertyType::Texture2D, std::string{}}})};
    material.setTexture("mainTexture", texture);

    EXPECT_EQ(material.resolveTexture("mainTexture"), texture);
    EXPECT_EQ(texture.useCount(), before + 1);
    EXPECT_TRUE(material.resolveSampler("mainTexture") == nullptr);
}

TEST_F(TextureTest, RenderTextureFactoriesCreateOwnedResourcesWithCorrectBindings) {
    using namespace engine;
    {
        const auto color = Texture::createColor(64, 32, TextureFormat::Rgba8Srgb);
        const auto depth = Texture::createDepth(64, 32);
        const auto shadow = Texture::createShadowMap(1024, 1024);
        ASSERT_TRUE(color);
        ASSERT_TRUE(depth);
        ASSERT_TRUE(shadow);
        ASSERT_EQ(device.textureDescs.size(), 3U);
        ASSERT_EQ(device.viewDescs.size(), 3U);
        EXPECT_EQ(color->colorSpace(), TextureColorSpace::Srgb);
        EXPECT_EQ(depth->format(), TextureFormat::Depth32Float);
        EXPECT_EQ(shadow->width(), 1024U);
        EXPECT_EQ(shadow->height(), 1024U);
        EXPECT_EQ(device.textureDescs[0].usage,
                  rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
        EXPECT_EQ(device.textureDescs[1].usage, rhi::TextureUsage::DepthStencilAttachment);
        EXPECT_EQ(device.textureDescs[2].usage,
                  rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled);
        EXPECT_EQ(device.textureDescs[2].format, rhi::PixelFormat::Depth32Float);
        EXPECT_EQ(device.viewDescs[0].aspect, rhi::TextureAspect::Color);
        EXPECT_EQ(device.viewDescs[1].aspect, rhi::TextureAspect::Depth);
        EXPECT_EQ(device.viewDescs[2].aspect, rhi::TextureAspect::Depth);
        EXPECT_EQ(device.samplerDescs[2].addressU, rhi::SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(device.samplerDescs[2].addressV, rhi::SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(shadow->binding().view, shadow->defaultView());
        EXPECT_FALSE(shadow->isAssetBacked());
        const std::vector<std::uint8_t> pixels(64U * 32U * 4U);
        color->upload(pixels);
        EXPECT_EQ(device.textureUploads, 0U);
        EXPECT_EQ(device.destroyedTextures, 0U);
    }
    EXPECT_EQ(device.destroyedTextures, 3U);
}

TEST_F(TextureTest, RenderTextureFactoriesRejectInvalidInputsBeforeAllocating) {
    using namespace engine;
    EXPECT_FALSE(Texture::createColor(0, 32));
    EXPECT_FALSE(Texture::createDepth(32, 0));
    EXPECT_FALSE(Texture::createShadowMap(0, 0));
    EXPECT_FALSE(Texture::createColor(32, 32, TextureFormat::Depth32Float));
    EXPECT_FALSE(Texture::createColor(32, 32, TextureFormat::Depth16Unorm));
    EXPECT_FALSE(Texture::createDepth(32, 32, TextureFormat::Rgba16Float));
    EXPECT_FALSE(Texture::createShadowMap(32, 32, TextureFormat::R8Unorm));
    EXPECT_FALSE(Texture::createColor(32, 32, static_cast<TextureFormat>(999)));
    TextureDesc invalid;
    invalid.width = invalid.height = 32;
    invalid.format = static_cast<TextureFormat>(999);
    EXPECT_FALSE(Texture::create(invalid));
    invalid.format = TextureFormat::Rgba16Float;
    invalid.colorSpace = TextureColorSpace::Srgb;
    EXPECT_FALSE(Texture::create(invalid));
    EXPECT_EQ(device.createdTextures, 0U);
}

TEST_F(TextureTest, ExtendedFormatsUseTheirOwnFormatAndDefaultViewAspect) {
    using namespace engine;
    for (const auto format : {TextureFormat::R8Unorm, TextureFormat::Rg8Unorm,
                              TextureFormat::R16Float, TextureFormat::Rg16Float,
                              TextureFormat::Rgba16Float, TextureFormat::R32Float,
                              TextureFormat::Rg32Float, TextureFormat::Rgba32Float}) {
        SCOPED_TRACE(static_cast<int>(format));
        const auto texture = Texture::createColor(32, 16, format);
        ASSERT_TRUE(texture);
        EXPECT_EQ(texture->format(), format);
        EXPECT_EQ(texture->pixelFormat(), toRhi(format));
        EXPECT_EQ(texture->colorSpace(), TextureColorSpace::Linear);
        EXPECT_EQ(device.textureDescs.back().format, toRhi(format));
        EXPECT_EQ(device.viewDescs.back().format, toRhi(format));
        EXPECT_EQ(device.viewDescs.back().aspect, rhi::TextureAspect::Color);
        EXPECT_EQ(device.viewDescs.back().mipCount, 1U);
    }
    const auto depth = Texture::createDepth(32, 16, TextureFormat::Depth16Unorm);
    const auto shadow = Texture::createShadowMap(32, 16, TextureFormat::Depth16Unorm);
    ASSERT_TRUE(depth);
    ASSERT_TRUE(shadow);
    EXPECT_EQ(depth->format(), TextureFormat::Depth16Unorm);
    EXPECT_EQ(depth->usage(), rhi::TextureUsage::DepthStencilAttachment);
    EXPECT_EQ(shadow->usage(), rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled);
    EXPECT_EQ(device.viewDescs.back().format, rhi::PixelFormat::Depth16Unorm);
    EXPECT_EQ(device.viewDescs.back().aspect, rhi::TextureAspect::Depth);
    EXPECT_EQ(device.textureUploads, 0U);
}

TEST_F(TextureTest, GeneralCreationPreservesFormatMipCountAndSamples) {
    using namespace engine;
    TextureDesc colorDesc{TextureType::Texture2D, TextureFormat::Bgra8Srgb,
        TextureColorSpace::Srgb, 64, 32, 4};
    colorDesc.debugName = "MipmappedColor";
    colorDesc.sampler.addressModeU = TextureAddressMode::ClampToEdge;
    const auto mipmapped = Texture::create(colorDesc);
    TextureDesc depthDesc{TextureType::Texture2D, TextureFormat::Depth32Float,
        TextureColorSpace::Linear, 64, 32};
    depthDesc.usage = rhi::TextureUsage::DepthStencilAttachment;
    depthDesc.samples = rhi::SampleCount::Four;
    const auto multisampled = Texture::create(depthDesc);
    ASSERT_TRUE(mipmapped);
    ASSERT_TRUE(multisampled);
    EXPECT_EQ(mipmapped->format(), TextureFormat::Bgra8Srgb);
    EXPECT_EQ(mipmapped->pixelFormat(), rhi::PixelFormat::Bgra8Srgb);
    EXPECT_EQ(mipmapped->colorSpace(), TextureColorSpace::Srgb);
    EXPECT_EQ(mipmapped->mipCount(), 4U);
    EXPECT_EQ(device.viewDescs[0].mipCount, 4U);
    EXPECT_EQ(device.textureDescs[0].debugName, "MipmappedColor");
    EXPECT_EQ(device.samplerDescs[0].addressU, rhi::SamplerAddressMode::ClampToEdge);
    EXPECT_EQ(multisampled->samples(), rhi::SampleCount::Four);
    EXPECT_EQ(device.textureDescs[1].samples, rhi::SampleCount::Four);
    EXPECT_EQ(multisampled->tiling(), rhi::TextureTiling::Optimal);
    depthDesc.mipCount = 2;
    EXPECT_FALSE(Texture::create(depthDesc));
    EXPECT_EQ(device.createdTextures, 2U);
}

TEST(TextureOwnershipTest, OldDeviceTextureNeverDestroysNewDeviceResources) {
    using namespace engine;
    Ref<Texture> old;
    {
        FakeDevice first;
        old = Texture::create(TextureDesc{TextureType::Texture2D, TextureFormat::Rgba8Unorm,
            TextureColorSpace::Linear, 16, 16});
        ASSERT_TRUE(old);
    }
    FakeDevice second;
    const auto current = Texture::createColor(16, 16);
    ASSERT_TRUE(current);
    const std::vector<std::uint8_t> pixels(16U * 16U * 4U);
    old->upload(pixels);
    old = {};
    EXPECT_EQ(second.destroyedTextures, 0U);
    EXPECT_EQ(second.textureUploads, 0U);
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
    EXPECT_EQ(defaultBinding.view, pointBinding.view);       // same default view
    EXPECT_NE(defaultBinding.sampler, pointBinding.sampler); // overridden sampler
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

TEST(TextureValidationTest, AssetPayloadRejectsRuntimeOptionsItCannotSerialize) {
    using namespace engine;
    TextureDesc desc{TextureType::Texture2D, TextureFormat::Rgba8Unorm,
        TextureColorSpace::Linear, 1, 1};
    const std::vector<std::uint8_t> pixels(4);
    EXPECT_TRUE(validateTexture(desc, pixels));
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    EXPECT_FALSE(validateTexture(desc, pixels));
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination;
    desc.samples = rhi::SampleCount::Four;
    EXPECT_FALSE(validateTexture(desc, pixels));
}

TEST(TextureValidationTest, ExtendedRuntimeFormatsDoNotAcceptRgba8AssetPayloads) {
    using namespace engine;
    const std::vector<std::uint8_t> rgbaBytes(16U * 16U * 4U);
    for (const auto format : {TextureFormat::R8Unorm, TextureFormat::Rgba16Float,
                              TextureFormat::Depth16Unorm}) {
        TextureDesc desc{TextureType::Texture2D, format, TextureColorSpace::Linear, 16, 16};
        EXPECT_FALSE(validateTexture(desc, rgbaBytes));
    }
}

} // namespace

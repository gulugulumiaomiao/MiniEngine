#include "render/gpu/texture/TextureStorage.h"
#include "render/material/Material.h"
#include "render/texture/TextureManager.h"
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

class TextureTest : public ::testing::Test {
protected:
    void SetUp() override {
        TEXTURE_RESOURCE_MANAGER.clear();
        TEXTURE_STORAGE.shutdown();
        ASSERT_TRUE(TEXTURE_STORAGE.initialize(device));
    }

    void TearDown() override {
        TEXTURE_RESOURCE_MANAGER.clear();
        TEXTURE_STORAGE.shutdown();
    }

    FakeDevice device;
};

TEST_F(TextureTest, BuiltinsOwnRhiTextureAndDefaultView) {
    using namespace engine;
    const Ref<Texture> white = TEXTURE_RESOURCE_MANAGER.defaultWhite();
    const Ref<Texture> black = TEXTURE_RESOURCE_MANAGER.defaultBlack();
    const Ref<Texture> normal = TEXTURE_RESOURCE_MANAGER.defaultNormal();
    const Ref<Texture> error = TEXTURE_RESOURCE_MANAGER.errorTexture();

    ASSERT_TRUE(white);
    ASSERT_TRUE(normal);
    ASSERT_TRUE(error);
    EXPECT_NE(white, black);
    EXPECT_NE(black, normal);
    EXPECT_NE(normal, error);
    EXPECT_EQ(white->desc().format, TextureFormat::Rgba8Srgb);
    EXPECT_EQ(normal->desc().format, TextureFormat::Rgba8Unorm);
    EXPECT_EQ(error->desc().width, 2U);
    EXPECT_EQ(error->pixels().size(), 16U);
    ASSERT_NE(TEXTURE_STORAGE.resolve(*white), nullptr);
    ASSERT_NE(TEXTURE_STORAGE.resolve(*black), nullptr);
    ASSERT_NE(TEXTURE_STORAGE.resolve(*normal), nullptr);
    ASSERT_NE(TEXTURE_STORAGE.resolve(*error), nullptr);
    const TextureStorageEntry* whiteGpu = TEXTURE_STORAGE.resolve(*white);
    ASSERT_NE(whiteGpu, nullptr);
    EXPECT_TRUE(whiteGpu->texture);
    EXPECT_TRUE(whiteGpu->defaultView);
    EXPECT_EQ(device.createdTextures, 4U);
    EXPECT_EQ(device.textureUploads, 4U);
}

TEST_F(TextureTest, ViewAndSamplerAreLightweightIndependentBindings) {
    using namespace engine;
    const Ref<Texture> white = TEXTURE_RESOURCE_MANAGER.defaultWhite();
    ASSERT_TRUE(white);

    const rhi::TextureViewDesc viewDesc{.type = rhi::TextureType::Texture2D,
                                        .format = rhi::PixelFormat::Rgba8Srgb,
                                        .baseMip = 0,
                                        .mipCount = 1,
                                        .baseLayer = 0,
                                        .layerCount = 1,
                                        .swizzle = {.r = rhi::SwizzleComponent::B,
                                                    .g = rhi::SwizzleComponent::G,
                                                    .b = rhi::SwizzleComponent::R,
                                                    .a = rhi::SwizzleComponent::A}};
    const TextureStorageEntry* stored = TEXTURE_STORAGE.resolve(*white);
    ASSERT_NE(stored, nullptr);
    const rhi::RID textureHandle = stored->texture;
    const Sampler linear = stored->defaultSampler;
    const TextureView view = TEXTURE_STORAGE.getView(*white, viewDesc);
    const TextureView cachedView = TEXTURE_STORAGE.getView(*white, viewDesc);
    rhi::SamplerDesc pointDesc = linear.desc();
    pointDesc.minFilter = rhi::SamplerFilter::Nearest;
    pointDesc.magFilter = rhi::SamplerFilter::Nearest;
    const Sampler point = Sampler::resolve(device, pointDesc);

    ASSERT_TRUE(view);
    ASSERT_TRUE(linear);
    ASSERT_TRUE(point);
    EXPECT_EQ(view.textureHandle(), textureHandle);
    EXPECT_EQ(view.rhiHandle(), cachedView.rhiHandle());
    EXPECT_NE(linear.rhiHandle(), point.rhiHandle());
    const TextureBinding linearBinding{view, linear};
    const TextureBinding pointBinding{view, point};
    EXPECT_EQ(linearBinding.toRhi().view, view.rhiHandle());
    EXPECT_EQ(pointBinding.toRhi().sampler, point.rhiHandle());
    ASSERT_EQ(device.viewDescs.size(), 1U);
    EXPECT_EQ(device.viewDescs[0], viewDesc);
}

TEST_F(TextureTest, MaterialRetainsTextureByRef) {
    using namespace engine;
    Ref<Texture> texture = TEXTURE_RESOURCE_MANAGER.defaultWhite();
    ASSERT_TRUE(texture);
    const std::uint32_t before = texture.useCount();

    Material material;
    material.textures.emplace("mainTexture", "");
    material.setTexture("mainTexture", texture);

    EXPECT_EQ(material.resolveTexture("mainTexture"), texture);
    EXPECT_EQ(texture.useCount(), before + 1);
    EXPECT_TRUE(TEXTURE_STORAGE.resolveBinding(*texture));
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

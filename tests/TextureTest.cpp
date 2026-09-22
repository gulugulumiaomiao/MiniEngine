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
    VkDevice device() const override { return VK_NULL_HANDLE; }
    VkInstance instance() const override { return VK_NULL_HANDLE; }
    VkPhysicalDevice physicalDevice() const override { return VK_NULL_HANDLE; }
    VkQueue graphicsQueue() const override { return VK_NULL_HANDLE; }
    std::uint32_t graphicsQueueFamily() const override { return 0; }
    VkBuffer resolveBuffer(engine::rhi::RID) const override { return VK_NULL_HANDLE; }
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
    VkImage resolveTexture(engine::rhi::RID) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(engine::rhi::RID) const override {
        return VK_NULL_HANDLE;
    }
    engine::rhi::ResolvedPipeline
    resolvePipeline(engine::rhi::RID) const override {
        return {};
    }
    VkDescriptorSet resolveBindGroup(engine::rhi::RID) const override {
        return VK_NULL_HANDLE;
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
        TEXTURE_MANAGER.shutdown();
        ASSERT_TRUE(TEXTURE_MANAGER.initialize(device));
    }

    void TearDown() override { TEXTURE_MANAGER.shutdown(); }

    FakeDevice device;
};

TEST_F(TextureTest, BuiltinsOwnRhiTextureAndDefaultView) {
    using namespace engine;
    const RID white = TEXTURE_MANAGER.defaultWhite();
    const RID black = TEXTURE_MANAGER.defaultBlack();
    const RID normal = TEXTURE_MANAGER.defaultNormal();
    const RID error = TEXTURE_MANAGER.errorTexture();

    const Texture* whiteTexture = TEXTURE_MANAGER.find(white);
    const Texture* normalTexture = TEXTURE_MANAGER.find(normal);
    const Texture* errorTexture = TEXTURE_MANAGER.find(error);
    ASSERT_TRUE(whiteTexture);
    ASSERT_TRUE(normalTexture);
    ASSERT_TRUE(errorTexture);
    EXPECT_NE(white, black);
    EXPECT_NE(black, normal);
    EXPECT_NE(normal, error);
    EXPECT_EQ(whiteTexture->desc().format, TextureFormat::Rgba8Srgb);
    EXPECT_EQ(normalTexture->desc().format, TextureFormat::Rgba8Unorm);
    EXPECT_EQ(errorTexture->desc().width, 2U);
    EXPECT_EQ(errorTexture->mipData()[0].bytes.size(), 16U);
    EXPECT_TRUE(whiteTexture->rhiHandle());
    EXPECT_TRUE(whiteTexture->defaultView());
    EXPECT_EQ(device.createdTextures, 4U);
    EXPECT_EQ(device.textureUploads, 4U);
}

TEST_F(TextureTest, ViewAndSamplerAreLightweightIndependentBindings) {
    using namespace engine;
    const RID white = TEXTURE_MANAGER.defaultWhite();
    const Texture* texture = TEXTURE_MANAGER.find(white);
    ASSERT_TRUE(texture);

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
    const TextureView view = texture->getView(viewDesc);
    const TextureView cachedView = texture->getView(viewDesc);
    const Sampler linear = Sampler::resolve(device, texture->defaultSamplerDesc());
    rhi::SamplerDesc pointDesc = texture->defaultSamplerDesc();
    pointDesc.minFilter = rhi::SamplerFilter::Nearest;
    pointDesc.magFilter = rhi::SamplerFilter::Nearest;
    const Sampler point = Sampler::resolve(device, pointDesc);

    ASSERT_TRUE(view);
    ASSERT_TRUE(linear);
    ASSERT_TRUE(point);
    EXPECT_EQ(view.textureHandle(), texture->rhiHandle());
    EXPECT_EQ(view.rhiHandle(), cachedView.rhiHandle());
    EXPECT_NE(linear.rhiHandle(), point.rhiHandle());
    const TextureBinding linearBinding{view, linear};
    const TextureBinding pointBinding{view, point};
    EXPECT_EQ(linearBinding.toRhi().view, view.rhiHandle());
    EXPECT_EQ(pointBinding.toRhi().sampler, point.rhiHandle());
    ASSERT_EQ(device.viewDescs.size(), 1U);
    EXPECT_EQ(device.viewDescs[0], viewDesc);
}

TEST_F(TextureTest, MaterialBindsTextureViewAndSamplerInsteadOfTexture) {
    using namespace engine;
    const RID textureHandle = TEXTURE_MANAGER.defaultWhite();
    const Texture* texture = TEXTURE_MANAGER.find(textureHandle);
    ASSERT_TRUE(texture);
    const Sampler sampler = Sampler::resolve(device, texture->defaultSamplerDesc());
    ASSERT_TRUE(sampler);

    Material material;
    material.textures.emplace("mainTexture", "");
    material.setTexture("mainTexture", *texture, sampler);
    const TextureBinding* binding = material.getTextureBinding("mainTexture");
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->view, texture->defaultView());
    EXPECT_EQ(binding->sampler, sampler);
    EXPECT_EQ(binding->toRhi().view, texture->defaultView().rhiHandle());

    const rhi::TextureViewDesc customDesc{.type = rhi::TextureType::Texture2D,
                                          .format = rhi::PixelFormat::Rgba8Srgb,
                                          .baseMip = 0,
                                          .mipCount = 1,
                                          .baseLayer = 0,
                                          .layerCount = 1};
    const TextureView customView = texture->getView(customDesc);
    material.setTexture("mainTexture", customView, sampler);
    binding = material.getTextureBinding("mainTexture");
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->view, customView);
}

TEST_F(TextureTest, CloneCreatesIndependentRhiTexture) {
    using namespace engine;
    const RID source = TEXTURE_MANAGER.defaultWhite();
    const RID clone = TEXTURE_MANAGER.clone(source);
    ASSERT_TRUE(clone);
    ASSERT_NE(source, clone);
    const Texture* sourceTexture = TEXTURE_MANAGER.find(source);
    const Texture* cloneTexture = TEXTURE_MANAGER.find(clone);
    ASSERT_TRUE(sourceTexture);
    ASSERT_TRUE(cloneTexture);
    EXPECT_NE(sourceTexture->rhiHandle(), cloneTexture->rhiHandle());
    EXPECT_FALSE(cloneTexture->isAssetBacked());
}

TEST(TextureValidationTest, RejectsUnimplementedTextureDimensions) {
    using namespace engine;
    TextureDesc desc{
        TextureType::Texture2DArray, TextureFormat::Rgba8Unorm, TextureColorSpace::Linear, 1, 1, 1};
    desc.arrayLayers = 2;
    const std::vector<TextureMipData> mipData{
        {1, 1, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}}};
    EXPECT_FALSE(validateTexture(desc, mipData));
}

} // namespace

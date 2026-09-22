#pragma once
#include "rhi/api/CommandBuffer.h"
#include "rhi/api/Device.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
namespace {
using namespace engine;
// Hands out live handles (generation 1) and records every description so the test can
// assert what the renderer asked the RHI for.
class MockRhiTexture final : public rhi::IRHITexture {
public:
    MockRhiTexture(rhi::TextureDesc desc, rhi::RID defaultView)
        : desc_(std::move(desc)), defaultView_(defaultView) {}

    rhi::TextureType type() const override { return desc_.dimension; }
    rhi::PixelFormat format() const override { return desc_.format; }
    std::uint32_t width() const override { return desc_.width; }
    std::uint32_t height() const override { return desc_.height; }
    std::uint32_t depth() const override { return desc_.depth; }
    std::uint32_t arrayLayers() const override { return desc_.arrayLayers; }
    std::uint32_t mipCount() const override { return desc_.mipCount; }
    rhi::RID defaultView() const override { return defaultView_; }
    rhi::RID createView(const rhi::TextureViewDesc&) override { return defaultView_; }

private:
    rhi::TextureDesc desc_;
    rhi::RID defaultView_;
};

class MockEncoder final : public rhi::ICommandBuffer {
public:
    void begin() override { currentState = rhi::CommandState::Recording; }
    void end() override { currentState = rhi::CommandState::Executable; }
    [[nodiscard]] rhi::CommandState state() const override { return currentState; }
    void resourceBarriers(std::span<const rhi::TextureBarrier> values) override {
        barriers.insert(barriers.end(), values.begin(), values.end());
    }
    void beginRendering(const rhi::RenderingInfo& info) override { renderings.push_back(info); }
    void endRendering() override {}
    void setViewport(const rhi::Viewport& viewport) override { viewports.push_back(viewport); }
    void setScissor(const rhi::Rect& scissor) override { scissors.push_back(scissor); }
    void setCullMode(rhi::CullMode mode) override { cullModes.push_back(mode); }
    void setFrontFace(rhi::FrontFace face) override { frontFaces.push_back(face); }
    void setDepthTestEnable(bool enable) override { depthTestEnables.push_back(enable); }
    void setDepthWriteEnable(bool enable) override { depthWriteEnables.push_back(enable); }
    void setDepthCompareOp(rhi::CompareOp compare) override { depthCompareOps.push_back(compare); }
    void setBlendState(rhi::BlendMode mode) override { blendModes.push_back(mode); }
    void setColorWriteMask(rhi::ColorWriteMask mask) override { colorWriteMasks.push_back(mask); }
    void setPrimitiveTopology(rhi::PrimitiveTopology topology) override {
        primitiveTopologies.push_back(topology);
    }
    void setFillMode(rhi::FillMode mode) override { fillModes.push_back(mode); }
    void bindPipeline(rhi::RID pipeline) override {
        boundPipelines.push_back(pipeline);
    }
    void bindVertexBuffer(std::uint32_t, rhi::RID buffer, std::uint64_t) override {
        boundVertexBuffers.push_back(buffer);
    }
    void
    bindIndexBuffer(rhi::RID buffer, std::uint64_t, rhi::IndexFormat format) override {
        boundIndexBuffers.push_back(buffer);
        indexFormats.push_back(format);
    }
    void bindGroup(std::uint32_t set,
                   rhi::RID group,
                   std::span<const std::uint32_t>) override {
        boundSets.push_back(set);
        boundGroups.push_back(group);
    }
    void draw(const rhi::DrawArguments&) override {}
    void drawIndexed(const rhi::DrawIndexedArguments& arguments) override {
        draws.push_back(arguments);
    }
    void beginDebugLabel(std::string_view, const math::Vec4&) override {}
    void endDebugLabel() override {}
    void copyBuffer(const rhi::BufferCopy& copy) override { bufferCopies.push_back(copy); }
    void copyImage(const rhi::ImageCopy& copy) override { imageCopies.push_back(copy); }
    void copyBufferToImage(const rhi::BufferImageCopy& copy) override {
        bufferImageCopies.push_back(copy);
    }
    void copyImageToBuffer(const rhi::BufferImageCopy& copy) override {
        imageBufferCopies.push_back(copy);
    }
    void updateBuffer(const rhi::BufferUpdate& update) override { bufferUpdates.push_back(update); }
    void updateImage(const rhi::ImageUpdate& update) override { imageUpdates.push_back(update); }

    std::vector<rhi::Viewport> viewports;
    std::vector<rhi::Rect> scissors;
    rhi::CommandState currentState{rhi::CommandState::Initial};
    std::vector<rhi::CullMode> cullModes;
    std::vector<rhi::FrontFace> frontFaces;
    std::vector<bool> depthTestEnables;
    std::vector<bool> depthWriteEnables;
    std::vector<rhi::CompareOp> depthCompareOps;
    std::vector<rhi::BlendMode> blendModes;
    std::vector<rhi::ColorWriteMask> colorWriteMasks;
    std::vector<rhi::PrimitiveTopology> primitiveTopologies;
    std::vector<rhi::FillMode> fillModes;
    std::vector<rhi::RID> boundPipelines;
    std::vector<rhi::RID> boundVertexBuffers;
    std::vector<rhi::RID> boundIndexBuffers;
    std::vector<rhi::IndexFormat> indexFormats;
    std::vector<std::uint32_t> boundSets;
    std::vector<rhi::RID> boundGroups;
    std::vector<rhi::DrawIndexedArguments> draws;
    std::vector<rhi::TextureBarrier> barriers;
    std::vector<rhi::RenderingInfo> renderings;
    std::vector<rhi::BufferCopy> bufferCopies;
    std::vector<rhi::ImageCopy> imageCopies;
    std::vector<rhi::BufferImageCopy> bufferImageCopies;
    std::vector<rhi::BufferImageCopy> imageBufferCopies;
    std::vector<rhi::BufferUpdate> bufferUpdates;
    std::vector<rhi::ImageUpdate> imageUpdates;
};

class MockDevice final : public rhi::IDevice {
public:
    rhi::RID createBuffer(const rhi::BufferDesc& desc) override {
        buffers.push_back(desc);
        return {static_cast<std::uint32_t>(buffers.size()), 1};
    }
    void destroyBuffer(rhi::RID) override { ++destroyedBuffers; }
    void uploadBuffer(rhi::RID, std::span<const std::byte> data, std::uint64_t) override {
        uploadedBytes.push_back(data.size_bytes());
    }

    rhi::RID createTexture(const rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        const rhi::RID handle{static_cast<std::uint32_t>(textures.size()), 1};
        const rhi::RID defaultView{++views, 1};
        textureResources.push_back(std::make_unique<MockRhiTexture>(desc, defaultView));
        defaultTextureViews.emplace(handle.index(), defaultView);
        return handle;
    }
    void destroyTexture(rhi::RID handle) override {
        ++destroyedTextures;
        if (defaultTextureViews.erase(handle.index()) > 0)
            ++destroyedViews;
    }
    void uploadTexture(rhi::RID, std::span<const rhi::TextureUploadRegion>) override {
        ++textureUploads;
    }
    rhi::RID createTextureView(rhi::RID,
                                             const rhi::TextureViewDesc&) override {
        return {++views, 1};
    }
    rhi::RID defaultTextureView(rhi::RID texture) const override {
        const auto found = defaultTextureViews.find(texture.index());
        return found == defaultTextureViews.end() ? rhi::RID{} : found->second;
    }
    void destroyTextureView(rhi::RID) override { ++destroyedViews; }
    rhi::RID createSampler(const rhi::SamplerDesc& desc) override {
        samplers.push_back(desc);
        return {static_cast<std::uint32_t>(samplers.size()), 1};
    }
    void destroySampler(rhi::RID) override { ++destroyedSamplers; }
    rhi::RID createShader(const rhi::ShaderDesc& desc) override {
        shaders.push_back(desc.stage);
        shaderBytecode.emplace_back(desc.bytecode.begin(), desc.bytecode.end());
        return {static_cast<std::uint32_t>(shaders.size()), 1};
    }
    void destroyShader(rhi::RID) override { ++destroyedShaders; }
    rhi::RID
    createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override {
        pipelines.push_back(desc);
        return {static_cast<std::uint32_t>(pipelines.size()), 1};
    }
    void destroyGraphicsPipeline(rhi::RID) override { ++destroyedPipelines; }
    rhi::RID
    createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override {
        layoutEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++layouts, 1};
    }
    void destroyBindGroupLayout(rhi::RID) override { ++destroyedLayouts; }
    rhi::RID createBindGroup(const rhi::BindGroupDesc& desc) override {
        bindGroupEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++bindGroups, 1};
    }
    void destroyBindGroup(rhi::RID) override { ++destroyedBindGroups; }

    std::unique_ptr<rhi::ICommandBuffer> createCommandBuffer() override {
        ++commandBuffers;
        return std::make_unique<MockEncoder>();
    }
    void submitCommand(rhi::ICommandBuffer&, const rhi::SubmitSync&) override { ++submissions; }

    VkDevice device() const override { return VK_NULL_HANDLE; }
    VkInstance instance() const override { return VK_NULL_HANDLE; }
    VkPhysicalDevice physicalDevice() const override { return VK_NULL_HANDLE; }
    VkQueue graphicsQueue() const override { return VK_NULL_HANDLE; }
    std::uint32_t graphicsQueueFamily() const override { return 0; }
    VkBuffer resolveBuffer(rhi::RID) const override { return VK_NULL_HANDLE; }
    rhi::IRHITexture* resolveTextureResource(rhi::RID handle) override {
        return handle.index() > 0 && handle.index() <= textureResources.size()
                   ? textureResources[handle.index() - 1].get()
                   : nullptr;
    }
    const rhi::IRHITexture* resolveTextureResource(rhi::RID handle) const override {
        return handle.index() > 0 && handle.index() <= textureResources.size()
                   ? textureResources[handle.index() - 1].get()
                   : nullptr;
    }
    VkImage resolveTexture(rhi::RID) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(rhi::RID) const override { return VK_NULL_HANDLE; }
    rhi::ResolvedPipeline resolvePipeline(rhi::RID) const override { return {}; }
    VkDescriptorSet resolveBindGroup(rhi::RID) const override { return VK_NULL_HANDLE; }
    void waitIdle() override {}

    std::vector<rhi::BufferDesc> buffers;
    std::vector<rhi::TextureDesc> textures;
    std::vector<std::unique_ptr<MockRhiTexture>> textureResources;
    std::unordered_map<std::uint32_t, rhi::RID> defaultTextureViews;
    std::vector<rhi::SamplerDesc> samplers;
    std::vector<rhi::ShaderStage> shaders;
    std::vector<std::vector<std::byte>> shaderBytecode;
    std::vector<rhi::GraphicsPipelineDesc> pipelines;
    std::vector<rhi::BindGroupLayoutEntry> layoutEntries;
    std::vector<rhi::BindGroupEntry> bindGroupEntries;
    std::vector<std::size_t> uploadedBytes;
    std::uint32_t views{};
    std::uint32_t layouts{};
    std::uint32_t bindGroups{};
    std::uint32_t textureUploads{};
    std::uint32_t commandBuffers{};
    std::uint32_t submissions{};
    int destroyedBuffers{};
    int destroyedTextures{};
    int destroyedViews{};
    int destroyedSamplers{};
    int destroyedShaders{};
    int destroyedPipelines{};
    int destroyedLayouts{};
    int destroyedBindGroups{};
};

} // namespace

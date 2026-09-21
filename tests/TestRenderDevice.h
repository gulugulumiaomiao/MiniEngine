#pragma once
#include "rhi/api/CommandBuffer.h"
#include "rhi/api/Device.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace {
using namespace engine;
// Hands out live handles (generation 1) and records every description so the test can
// assert what the renderer asked the RHI for.
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
    void bindPipeline(rhi::GraphicsPipelineHandle pipeline) override {
        boundPipelines.push_back(pipeline);
    }
    void bindVertexBuffer(std::uint32_t, rhi::BufferHandle buffer, std::uint64_t) override {
        boundVertexBuffers.push_back(buffer);
    }
    void bindIndexBuffer(rhi::BufferHandle buffer, std::uint64_t, rhi::IndexFormat format) override {
        boundIndexBuffers.push_back(buffer);
        indexFormats.push_back(format);
    }
    void bindGroup(std::uint32_t set,
                   rhi::BindGroupHandle group,
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
    std::vector<rhi::GraphicsPipelineHandle> boundPipelines;
    std::vector<rhi::BufferHandle> boundVertexBuffers;
    std::vector<rhi::BufferHandle> boundIndexBuffers;
    std::vector<rhi::IndexFormat> indexFormats;
    std::vector<std::uint32_t> boundSets;
    std::vector<rhi::BindGroupHandle> boundGroups;
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
    rhi::BufferHandle createBuffer(const rhi::BufferDesc& desc) override {
        buffers.push_back(desc);
        return {static_cast<std::uint32_t>(buffers.size()), 1};
    }
    void destroyBuffer(rhi::BufferHandle) override { ++destroyedBuffers; }
    void uploadBuffer(rhi::BufferHandle, std::span<const std::byte> data, std::uint64_t) override {
        uploadedBytes.push_back(data.size_bytes());
    }

    rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        return {static_cast<std::uint32_t>(textures.size()), 1};
    }
    void destroyTexture(rhi::TextureHandle) override { ++destroyedTextures; }
    void uploadTexture(rhi::TextureHandle, std::span<const rhi::TextureUploadRegion>) override {
        ++textureUploads;
    }
    rhi::TextureViewHandle createTextureView(const rhi::TextureViewDesc&) override {
        return {++views, 1};
    }
    void destroyTextureView(rhi::TextureViewHandle) override { ++destroyedViews; }
    rhi::SamplerHandle createSampler(const rhi::SamplerDesc& desc) override {
        samplers.push_back(desc);
        return {static_cast<std::uint32_t>(samplers.size()), 1};
    }
    void destroySampler(rhi::SamplerHandle) override { ++destroyedSamplers; }
    rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override {
        shaders.push_back(desc.stage);
        shaderBytecode.emplace_back(desc.bytecode.begin(), desc.bytecode.end());
        return {static_cast<std::uint32_t>(shaders.size()), 1};
    }
    void destroyShader(rhi::ShaderHandle) override { ++destroyedShaders; }
    rhi::GraphicsPipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override {
        pipelines.push_back(desc);
        return {static_cast<std::uint32_t>(pipelines.size()), 1};
    }
    void destroyGraphicsPipeline(rhi::GraphicsPipelineHandle) override { ++destroyedPipelines; }
    rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override {
        layoutEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++layouts, 1};
    }
    void destroyBindGroupLayout(rhi::BindGroupLayoutHandle) override { ++destroyedLayouts; }
    rhi::BindGroupHandle createBindGroup(const rhi::BindGroupDesc& desc) override {
        bindGroupEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++bindGroups, 1};
    }
    void destroyBindGroup(rhi::BindGroupHandle) override { ++destroyedBindGroups; }

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
    VkBuffer resolveBuffer(rhi::BufferHandle) const override { return VK_NULL_HANDLE; }
    VkImage resolveTexture(rhi::TextureHandle) const override { return VK_NULL_HANDLE; }
    VkImageView resolveTextureView(rhi::TextureViewHandle) const override { return VK_NULL_HANDLE; }
    rhi::ResolvedPipeline resolvePipeline(rhi::GraphicsPipelineHandle) const override { return {}; }
    VkDescriptorSet resolveBindGroup(rhi::BindGroupHandle) const override { return VK_NULL_HANDLE; }
    void waitIdle() override {}

    std::vector<rhi::BufferDesc> buffers;
    std::vector<rhi::TextureDesc> textures;
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




}

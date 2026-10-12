#pragma once
#include "rhi/api/Device.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
namespace {
using namespace engine;
// Hands out live handles (generation 1) and records every description so the test can assert
// what the renderer asked the RHI for. Constructing it registers it as the process-wide active
// device (IDevice singleton), which is how layer-2 resources reach a backend without a manager.
// Command recording is intentionally absent: the RHI command surface is a set of free functions
// that dispatch to the concrete Vulkan backend, so it cannot be exercised through this mock.
class MockDevice final : public rhi::IDevice {
public:
    rhi::RID buffer_allocate_rid(const rhi::BufferDesc& desc) override {
        if (bufferAllocationsUntilFailure == 0) {
            bufferAllocationsUntilFailure = -1;
            return {};
        }
        if (bufferAllocationsUntilFailure > 0)
            --bufferAllocationsUntilFailure;
        buffers.push_back(desc);
        return {static_cast<std::uint32_t>(buffers.size()), 1};
    }
    void buffer_allocate_memory(rhi::RID) override {}
    void buffer_free_memory(rhi::RID) override {}
    void buffer_release_rid(rhi::RID) override { ++destroyedBuffers; }
    rhi::RID buffer_create(const rhi::BufferDesc& desc) override {
        const rhi::RID handle = buffer_allocate_rid(desc);
        buffer_allocate_memory(handle);
        return handle;
    }
    void buffer_destroy(rhi::RID handle) override {
        buffer_free_memory(handle);
        buffer_release_rid(handle);
    }
    void buffer_upload(rhi::RID buffer, std::span<const std::byte> data, std::uint64_t) override {
        uploadedBytes.push_back(data.size_bytes());
        uploadedBuffers[buffer.index()] = {data.begin(), data.end()};
    }
    rhi::RID buffer_acquire_transient(const rhi::BufferDesc& desc) override {
        return buffer_create(desc);
    }

    rhi::RID texture_create(const rhi::TextureDesc& desc) override {
        textures.push_back(desc);
        const rhi::RID handle{static_cast<std::uint32_t>(textures.size()), 1};
        const rhi::RID defaultView{++views, 1};
        defaultTextureViews.emplace(handle.index(), defaultView);
        return handle;
    }
    void texture_destroy(rhi::RID handle) override {
        ++destroyedTextures;
        if (defaultTextureViews.erase(handle.index()) > 0)
            ++destroyedViews;
    }
    void texture_upload(rhi::RID, std::span<const rhi::TextureUploadRegion>) override {
        ++textureUploads;
    }
    rhi::RID texture_view_create(rhi::RID, const rhi::TextureViewDesc&) override {
        return {++views, 1};
    }
    rhi::RID texture_default_view(rhi::RID texture) override {
        const auto found = defaultTextureViews.find(texture.index());
        return found == defaultTextureViews.end() ? rhi::RID{} : found->second;
    }
    void texture_view_destroy(rhi::RID) override { ++destroyedViews; }
    rhi::RID sampler_create(const rhi::SamplerDesc& desc) override {
        samplers.push_back(desc);
        return {static_cast<std::uint32_t>(samplers.size()), 1};
    }
    void sampler_destroy(rhi::RID) override { ++destroyedSamplers; }
    rhi::RID shader_create(const rhi::ShaderDesc& desc) override {
        shaders.push_back(desc.stage);
        shaderBytecode.emplace_back(desc.bytecode.begin(), desc.bytecode.end());
        return {static_cast<std::uint32_t>(shaders.size()), 1};
    }
    void shader_destroy(rhi::RID) override { ++destroyedShaders; }
    rhi::RID pipeline_create(const rhi::GraphicsPipelineDesc& desc) override {
        pipelines.push_back(desc);
        return {static_cast<std::uint32_t>(pipelines.size()), 1};
    }
    void pipeline_destroy(rhi::RID) override { ++destroyedPipelines; }

    rhi::RID render_pass_create(const rhi::RenderPassDesc& desc) override {
        renderPasses.push_back(desc);
        return {static_cast<std::uint32_t>(renderPasses.size()), 1};
    }
    void render_pass_destroy(rhi::RID handle) override {
        destroyedRenderPasses.push_back(handle);
        renderObjectDestructionOrder.push_back("RenderPass");
    }
    rhi::RID framebuffer_create(const rhi::FramebufferDesc& desc) override {
        framebuffers.push_back(desc);
        return {static_cast<std::uint32_t>(framebuffers.size()), 1};
    }
    void framebuffer_destroy(rhi::RID handle) override {
        destroyedFramebuffers.push_back(handle);
        renderObjectDestructionOrder.push_back("Framebuffer");
    }
    rhi::TextureViewInfo texture_view_info(rhi::RID view) const override {
        for (const auto& [index, handle] : defaultTextureViews) {
            if (view != handle) continue;
            const auto& d = textures[index - 1];
            return {{index, 1}, d.format, d.samples, d.usage, d.width, d.height,
                    0, d.mipCount, 0, d.arrayLayers};
        }
        return {};
    }

    rhi::RID shader_allocate_rid() override { return {++shaderRids, 1}; }
    void shader_release_rid(rhi::RID) override {}
    rhi::RID descriptor_set_layout_allocate_rid() override { return {++setLayouts, 1}; }
    void descriptor_set_layout_build(rhi::RID, const rhi::DescriptorSetLayoutDesc&) override {}
    void descriptor_set_layout_destroy(rhi::RID) override { ++destroyedSetLayouts; }
    rhi::RID pipeline_layout_create(const rhi::PipelineLayoutDesc&) override {
        return {++pipelineLayouts, 1};
    }
    void pipeline_layout_destroy(rhi::RID) override { ++destroyedPipelineLayouts; }
    rhi::RID descriptor_set_allocate(const rhi::DescriptorSetAllocateInfo&) override {
        return {++descriptorSets, 1};
    }
    void descriptor_set_update(rhi::RID set,
                               std::span<const rhi::DescriptorWrite> writes) override {
        descriptorWrites[set.index()] = {writes.begin(), writes.end()};
    }
    void descriptor_set_destroy(rhi::RID) override { ++destroyedDescriptorSets; }

    void waitIdle() override {}

    int bufferAllocationsUntilFailure{-1};
    std::vector<rhi::BufferDesc> buffers;
    std::vector<rhi::TextureDesc> textures;
    std::unordered_map<std::uint32_t, rhi::RID> defaultTextureViews;
    std::vector<rhi::SamplerDesc> samplers;
    std::vector<rhi::ShaderStage> shaders;
    std::vector<std::vector<std::byte>> shaderBytecode;
    std::vector<rhi::GraphicsPipelineDesc> pipelines;
    std::vector<rhi::RenderPassDesc> renderPasses;
    std::vector<rhi::FramebufferDesc> framebuffers;
    std::vector<rhi::RID> destroyedRenderPasses;
    std::vector<rhi::RID> destroyedFramebuffers;
    std::vector<std::string> renderObjectDestructionOrder;
    std::vector<std::size_t> uploadedBytes;
    std::unordered_map<std::uint32_t, std::vector<std::byte>> uploadedBuffers;
    std::unordered_map<std::uint32_t, std::vector<rhi::DescriptorWrite>> descriptorWrites;
    std::uint32_t views{};
    std::uint32_t textureUploads{};
    int destroyedBuffers{};
    int destroyedTextures{};
    int destroyedViews{};
    int destroyedSamplers{};
    int destroyedShaders{};
    int destroyedPipelines{};
    std::uint32_t shaderRids{};
    std::uint32_t setLayouts{};
    std::uint32_t pipelineLayouts{};
    std::uint32_t descriptorSets{};
    int destroyedSetLayouts{};
    int destroyedPipelineLayouts{};
    int destroyedDescriptorSets{};
};

} // namespace

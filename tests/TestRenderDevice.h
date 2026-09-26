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
    rhi::RID buffer_create(const rhi::BufferDesc& desc) override {
        buffers.push_back(desc);
        return {static_cast<std::uint32_t>(buffers.size()), 1};
    }
    void buffer_destroy(rhi::RID) override { ++destroyedBuffers; }
    void buffer_upload(rhi::RID, std::span<const std::byte> data, std::uint64_t) override {
        uploadedBytes.push_back(data.size_bytes());
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
    rhi::RID bind_group_layout_create(const rhi::BindGroupLayoutDesc& desc) override {
        layoutEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++layouts, 1};
    }
    void bind_group_layout_destroy(rhi::RID) override { ++destroyedLayouts; }
    rhi::RID bind_group_create(const rhi::BindGroupDesc& desc) override {
        bindGroupEntries.assign(desc.entries.begin(), desc.entries.end());
        return {++bindGroups, 1};
    }
    void bind_group_destroy(rhi::RID) override { ++destroyedBindGroups; }

    void waitIdle() override {}

    std::vector<rhi::BufferDesc> buffers;
    std::vector<rhi::TextureDesc> textures;
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

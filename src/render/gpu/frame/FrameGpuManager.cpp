#include "render/gpu/frame/FrameGpuManager.h"

#include "core/logging/Log.h"
#include "render/material/MaterialLimits.h"
#include "rhi/api/Device.h"

#include <array>
#include <span>

namespace engine {

namespace {

constexpr std::uint64_t kObjectBufferSize =
    sizeof(ObjectDrawData) * FrameGpuManager::kMaxRenderObjects;
constexpr std::uint64_t kInstanceTableSize =
    sizeof(std::uint32_t) * FrameGpuManager::kMaxInstances;

} // namespace

bool FrameGpuManager::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("FrameGpuManager", "Manager is already initialized");
        return false;
    }
    device_ = &device;
    constexpr rhi::ShaderVisibility allGraphics =
        rhi::ShaderVisibility::Vertex | rhi::ShaderVisibility::Fragment;
    const std::array sceneBindings{
        rhi::BindGroupLayoutEntry{0, rhi::BindingType::UniformBuffer, allGraphics},
        rhi::BindGroupLayoutEntry{
            1, rhi::BindingType::StorageBuffer, rhi::ShaderVisibility::Vertex},
        rhi::BindGroupLayoutEntry{
            2, rhi::BindingType::StorageBuffer, rhi::ShaderVisibility::Vertex},
        rhi::BindGroupLayoutEntry{
            3, rhi::BindingType::SampledTexture, rhi::ShaderVisibility::Fragment},
    };
    sceneLayout_ = device_->createBindGroupLayout({sceneBindings, "Scene bind group layout"});

    std::array<rhi::BindGroupLayoutEntry, kMaxMaterialTextures + 1> materialBindings{};
    materialBindings[0] = {0, rhi::BindingType::UniformBuffer, allGraphics};
    for (std::uint32_t binding = 1; binding < materialBindings.size(); ++binding)
        materialBindings[binding] = {binding, rhi::BindingType::SampledTexture, allGraphics};
    materialLayout_ =
        device_->createBindGroupLayout({materialBindings, "Material bind group layout"});

    shadowSampler_ = device_->createSampler({
        .minFilter = rhi::SamplerFilter::Linear,
        .magFilter = rhi::SamplerFilter::Linear,
        .mipmapFilter = rhi::SamplerMipmapFilter::Nearest,
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
    });
    placeholderTexture_ = device_->createTexture({
        .format = rhi::TextureFormat::Depth32Float,
        .width = 1,
        .height = 1,
        .depth = 1,
        .mipCount = 1,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::DepthStencilAttachment,
        .debugName = "Shadow map placeholder",
    });
    placeholderView_ = device_->createTextureView({
        .texture = placeholderTexture_,
        .format = rhi::TextureFormat::Depth32Float,
        .aspect = rhi::TextureAspect::Depth,
        .baseMipLevel = 0,
        .mipCount = 1,
    });
    if (!shadowSampler_ || !placeholderTexture_ || !placeholderView_) {
        Log::fatal("FrameGpuManager", "Cannot create shadow sampling resources");
    }
    for (FrameResources& frame : frames_) {
        frame.sceneBuffer = device_->createBuffer({
            .size = sizeof(SceneDrawData),
            .usage = rhi::BufferUsage::Uniform,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Scene uniforms",
        });
        frame.objectBuffer = device_->createBuffer({
            .size = kObjectBufferSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Object draw data",
        });
        frame.instanceTable = device_->createBuffer({
            .size = kInstanceTableSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Instance table",
        });
        const std::array bindings{
            rhi::BindGroupEntry{.binding = 0,
                                .type = rhi::BindingType::UniformBuffer,
                                .buffer = frame.sceneBuffer,
                                .size = sizeof(SceneDrawData)},
            rhi::BindGroupEntry{.binding = 1,
                                .type = rhi::BindingType::StorageBuffer,
                                .buffer = frame.objectBuffer,
                                .size = kObjectBufferSize},
            rhi::BindGroupEntry{.binding = 2,
                                .type = rhi::BindingType::StorageBuffer,
                                .buffer = frame.instanceTable,
                                .size = kInstanceTableSize},
            rhi::BindGroupEntry{.binding = 3,
                                .type = rhi::BindingType::SampledTexture,
                                .textureView = placeholderView_,
                                .sampler = shadowSampler_},
        };
        frame.sceneBindGroup =
            device_->createBindGroup({sceneLayout_, bindings, "Scene bind group"});
        frame.shadowView = placeholderView_;
    }
    return true;
}

void FrameGpuManager::upload(std::uint32_t frameIndex, const DrawList& drawList) {
    if (!initialized() || frameIndex >= frames_.size())
        return;
    if (drawList.objects.size() > kMaxRenderObjects)
        Log::fatal("FrameGpuManager", "DrawList exceeds kMaxRenderObjects");

    FrameResources& frame = frames_[frameIndex];
    device_->uploadBuffer(frame.sceneBuffer, std::as_bytes(std::span{&drawList.scene, 1}));
    if (!drawList.objects.empty())
        device_->uploadBuffer(frame.objectBuffer, std::as_bytes(std::span{drawList.objects}));
}

void FrameGpuManager::bindShadowMap(std::uint32_t frameIndex, rhi::TextureViewHandle view) {
    if (!initialized() || frameIndex >= frames_.size() || !view)
        return;
    FrameResources& frame = frames_[frameIndex];
    if (frame.shadowView == view)
        return;
    frame.shadowView = view;
    if (frame.sceneBindGroup)
        device_->destroyBindGroup(frame.sceneBindGroup);
    const std::array bindings{
        rhi::BindGroupEntry{.binding = 0,
                            .type = rhi::BindingType::UniformBuffer,
                            .buffer = frame.sceneBuffer,
                            .size = sizeof(SceneDrawData)},
        rhi::BindGroupEntry{.binding = 1,
                            .type = rhi::BindingType::StorageBuffer,
                            .buffer = frame.objectBuffer,
                            .size = kObjectBufferSize},
        rhi::BindGroupEntry{.binding = 2,
                            .type = rhi::BindingType::StorageBuffer,
                            .buffer = frame.instanceTable,
                            .size = kInstanceTableSize},
        rhi::BindGroupEntry{.binding = 3,
                            .type = rhi::BindingType::SampledTexture,
                            .textureView = view,
                            .sampler = shadowSampler_},
    };
    frame.sceneBindGroup =
        device_->createBindGroup({sceneLayout_, bindings, "Scene bind group"});
}

rhi::BindGroupHandle FrameGpuManager::sceneBindGroup(std::uint32_t frameIndex) const {
    if (frameIndex >= frames_.size())
        return {};
    return frames_[frameIndex].sceneBindGroup;
}

void FrameGpuManager::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size())
        Log::fatal("FrameGpuManager", "Invalid frame index");
    frames_[frameIndex].instancesUsed = 0;
}

std::uint32_t FrameGpuManager::reserveInstanceRegion(std::uint32_t frameIndex,
                                                     std::uint32_t count) {
    if (frameIndex >= frames_.size())
        Log::fatal("FrameGpuManager", "Invalid frame index");
    FrameResources& frame = frames_[frameIndex];
    if (frame.instancesUsed + count > kMaxInstances)
        Log::fatal("FrameGpuManager", "Instance table exhausted (kMaxInstances=%u)", kMaxInstances);
    const std::uint32_t baseSlot = frame.instancesUsed;
    frame.instancesUsed += count;
    return baseSlot;
}

void FrameGpuManager::uploadInstanceRegion(std::uint32_t frameIndex,
                                           std::uint32_t baseSlot,
                                           std::span<const std::uint32_t> objectRows) {
    if (frameIndex >= frames_.size())
        Log::fatal("FrameGpuManager", "Invalid frame index");
    if (objectRows.empty())
        return;
    if (baseSlot >= kMaxInstances || objectRows.size() > kMaxInstances - baseSlot)
        Log::fatal("FrameGpuManager", "Instance region out of range");

    FrameResources& frame = frames_[frameIndex];
    device_->uploadBuffer(frame.instanceTable,
                          std::as_bytes(objectRows),
                          static_cast<std::uint64_t>(baseSlot) * sizeof(std::uint32_t));
}

void FrameGpuManager::shutdown() {
    if (!initialized())
        return;
    for (FrameResources& frame : frames_) {
        if (frame.sceneBindGroup)
            device_->destroyBindGroup(frame.sceneBindGroup);
        if (frame.sceneBuffer)
            device_->destroyBuffer(frame.sceneBuffer);
        if (frame.objectBuffer)
            device_->destroyBuffer(frame.objectBuffer);
        if (frame.instanceTable)
            device_->destroyBuffer(frame.instanceTable);
        frame = {};
    }
    if (placeholderView_)
        device_->destroyTextureView(placeholderView_);
    if (placeholderTexture_)
        device_->destroyTexture(placeholderTexture_);
    if (shadowSampler_)
        device_->destroySampler(shadowSampler_);
    if (sceneLayout_)
        device_->destroyBindGroupLayout(sceneLayout_);
    if (materialLayout_)
        device_->destroyBindGroupLayout(materialLayout_);
    placeholderView_ = {};
    placeholderTexture_ = {};
    shadowSampler_ = {};
    sceneLayout_ = {};
    materialLayout_ = {};
    device_ = nullptr;
}

} // namespace engine

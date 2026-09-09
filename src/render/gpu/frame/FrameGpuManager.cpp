#include "render/gpu/frame/FrameGpuManager.h"

#include "core/logging/Log.h"
#include "render/material/MaterialLimits.h"
#include "rhi/api/Device.h"

#include <array>
#include <span>

namespace engine {

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
    };
    sceneLayout_ = device_->createBindGroupLayout({sceneBindings, "Scene bind group layout"});

    std::array<rhi::BindGroupLayoutEntry, kMaxMaterialTextures + 1> materialBindings{};
    materialBindings[0] = {0, rhi::BindingType::UniformBuffer, allGraphics};
    for (std::uint32_t binding = 1; binding < materialBindings.size(); ++binding)
        materialBindings[binding] = {binding, rhi::BindingType::SampledTexture, allGraphics};
    materialLayout_ =
        device_->createBindGroupLayout({materialBindings, "Material bind group layout"});

    constexpr std::uint64_t objectBufferSize = sizeof(ObjectDrawData) * kMaxRenderObjects;
    for (FrameResources& frame : frames_) {
        frame.sceneBuffer = device_->createBuffer({
            .size = sizeof(SceneDrawData),
            .usage = rhi::BufferUsage::Uniform,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Scene uniforms",
        });
        frame.objectBuffer = device_->createBuffer({
            .size = objectBufferSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Object draw data",
        });
        const std::array bindings{
            rhi::BindGroupEntry{.binding = 0,
                                .type = rhi::BindingType::UniformBuffer,
                                .buffer = frame.sceneBuffer,
                                .size = sizeof(SceneDrawData)},
            rhi::BindGroupEntry{.binding = 1,
                                .type = rhi::BindingType::StorageBuffer,
                                .buffer = frame.objectBuffer,
                                .size = objectBufferSize},
        };
        frame.sceneBindGroup =
            device_->createBindGroup({sceneLayout_, bindings, "Scene bind group"});
    }
    return true;
}

rhi::BindGroupHandle FrameGpuManager::upload(std::uint32_t frameIndex, const DrawList& drawList) {
    if (!initialized() || frameIndex >= frames_.size())
        return {};
    if (drawList.objects.size() > kMaxRenderObjects)
        Log::fatal("FrameGpuManager", "DrawList exceeds kMaxRenderObjects");

    FrameResources& frame = frames_[frameIndex];
    device_->uploadBuffer(frame.sceneBuffer, std::as_bytes(std::span{&drawList.scene, 1}));
    if (!drawList.objects.empty())
        device_->uploadBuffer(frame.objectBuffer, std::as_bytes(std::span{drawList.objects}));
    return frame.sceneBindGroup;
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
        frame = {};
    }
    if (sceneLayout_)
        device_->destroyBindGroupLayout(sceneLayout_);
    if (materialLayout_)
        device_->destroyBindGroupLayout(materialLayout_);
    sceneLayout_ = {};
    materialLayout_ = {};
    device_ = nullptr;
}

} // namespace engine

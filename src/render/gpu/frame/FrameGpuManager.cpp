#include "render/gpu/frame/FrameGpuManager.h"

#include "core/logging/Log.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace engine {

namespace {

constexpr std::uint64_t kObjectBufferSize =
    sizeof(ObjectDrawData) * FrameGpuManager::kMaxRenderObjects;
constexpr std::uint64_t kInstanceTableSize = sizeof(std::uint32_t) * FrameGpuManager::kMaxInstances;

} // namespace

bool FrameGpuManager::initialize() {
    if (initialized()) {
        Log::error("FrameGpuManager", "Manager is already initialized");
        return false;
    }
    rhi::IDevice* device = RHI_DEVICE;
    if (!device) {
        Log::error("FrameGpuManager", "No active device for frame GPU initialization");
        return false;
    }
    initialized_ = true;

    // Fixed engine convention for set 0. acquire() dedups on the (binding, type) ordered hash, so
    // this is the same UniformSetLayout instance a program reflects for set 0 (shaders declare this
    // convention), which keeps the shared scene set compatible with every pipeline layout.
    const std::array<ResourceBinding, 4> sceneBindings{{
        {0, "Scene", rhi::ResourceType::UniformBuffer},
        {1, "objects", rhi::ResourceType::StorageBuffer},
        {2, "instanceTable", rhi::ResourceType::StorageBuffer},
        {3, "shadowMap", rhi::ResourceType::CombinedImageSampler},
    }};
    sceneLayout_ = UniformSetLayout::acquire(sceneBindings);

    // Synthetic one-member block whose size matches the packed SceneDrawData blob; the scene UBO is
    // written wholesale via Ubo::setRawBytes (the CPU struct already matches the std140 layout).
    sceneUboLayout_ = makeRef<UboLayout>();
    sceneUboLayout_->addUniform(UboUniform{
        "SceneData", UboUniformType::Float, 0, static_cast<std::uint32_t>(sizeof(SceneDrawData))});
    sceneUboLayout_->finish();

    shadowSampler_ = device->sampler_create({
        .minFilter = rhi::SamplerFilter::Linear,
        .magFilter = rhi::SamplerFilter::Linear,
        .mipmapFilter = rhi::SamplerMipmapFilter::Nearest,
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
    });
    placeholderTexture_ = device->texture_create({
        .format = rhi::PixelFormat::Depth32Float,
        .width = 1,
        .height = 1,
        .depth = 1,
        .mipCount = 1,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::DepthStencilAttachment,
        .debugName = "Shadow map placeholder",
    });
    placeholderView_ = device->texture_default_view(placeholderTexture_);
    if (!shadowSampler_ || !placeholderTexture_ || !placeholderView_) {
        Log::fatal("FrameGpuManager", "Cannot create shadow sampling resources");
    }

    for (FrameResources& frame : frames_) {
        frame.objectBuffer = device->buffer_create({
            .size = kObjectBufferSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Object draw data",
        });
        frame.instanceTable = device->buffer_create({
            .size = kInstanceTableSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Instance table",
        });
        frame.sceneSet =
            makeRef<UniformSet>(sceneLayout_, std::vector<Ref<UboLayout>>{sceneUboLayout_});
        frame.sceneSet->setResourceAt(
            1, makeRef<StorageBufferResource>(frame.objectBuffer, kObjectBufferSize));
        frame.sceneSet->setResourceAt(
            2, makeRef<StorageBufferResource>(frame.instanceTable, kInstanceTableSize));
        frame.sceneSet->setResourceAt(3, Texture::adoptBinding(placeholderView_, shadowSampler_));
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
    if (Ubo* sceneUbo = frame.sceneSet->uboAt(0))
        sceneUbo->setRawBytes(std::as_bytes(std::span{&drawList.scene, 1}));
    if (rhi::IDevice* device = RHI_DEVICE; !drawList.objects.empty() && device)
        device->buffer_upload(frame.objectBuffer, std::as_bytes(std::span{drawList.objects}));
    frame.sceneSet->upload();
}

void FrameGpuManager::bindShadowMap(std::uint32_t frameIndex, rhi::RID view) {
    if (!initialized() || frameIndex >= frames_.size() || !view)
        return;
    FrameResources& frame = frames_[frameIndex];
    if (frame.shadowView == view)
        return;
    frame.shadowView = view;
    frame.sceneSet->setResourceAt(3, Texture::adoptBinding(view, shadowSampler_));
    frame.sceneSet->upload();
}

const Ref<UniformSet>& FrameGpuManager::sceneUniformSet(std::uint32_t frameIndex) const {
    static const Ref<UniformSet> kEmpty;
    return frameIndex < frames_.size() ? frames_[frameIndex].sceneSet : kEmpty;
}

void FrameGpuManager::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size())
        Log::fatal("FrameGpuManager", "Invalid frame index");
    frames_[frameIndex].retained.clear();
    frames_[frameIndex].instancesUsed = 0;
}

void FrameGpuManager::retain(std::uint32_t frameIndex, Ref<RefCounted> resource) {
    if (!initialized_ || frameIndex >= frames_.size() || !resource)
        return;
    frames_[frameIndex].retained.push_back(std::move(resource));
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
    if (rhi::IDevice* device = RHI_DEVICE)
        device->buffer_upload(frame.instanceTable,
                              std::as_bytes(objectRows),
                              static_cast<std::uint64_t>(baseSlot) * sizeof(std::uint32_t));
}

void FrameGpuManager::shutdown() {
    if (!initialized())
        return;
    rhi::IDevice* device = RHI_DEVICE;
    for (FrameResources& frame : frames_) {
        frame.sceneSet.reset(); // frees the descriptor set + the scene UBO's buffer
        if (frame.objectBuffer && device)
            device->buffer_destroy(frame.objectBuffer);
        if (frame.instanceTable && device)
            device->buffer_destroy(frame.instanceTable);
        frame = {};
    }
    if (placeholderTexture_ && device)
        device->texture_destroy(placeholderTexture_);
    // The set-0 layout instance lives in the UniformSetLayout cache; it is torn down by
    // UniformSetLayout::clearCache() during engine shutdown (before the device is destroyed).
    sceneLayout_.reset();
    sceneUboLayout_.reset();
    placeholderView_ = {};
    placeholderTexture_ = {};
    shadowSampler_ = {};
    initialized_ = false;
}

} // namespace engine

#include "render/gpu/global_uniform/GlobalUniformGpuManager.h"

#include "core/logging/Log.h"
#include "render/global_uniform/GlobalUniformManager.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace engine {

GlobalUniformGpuManager::GlobalUniformGpuManager() = default;
GlobalUniformGpuManager::~GlobalUniformGpuManager() = default;

bool GlobalUniformGpuManager::initialize(std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("GlobalUniformGpuManager", "Manager is already initialized");
        return false;
    }
    initialized_ = true;
    frames_.resize(frameCount);
    return true;
}

void GlobalUniformGpuManager::ensureLayout() {
    const GlobalUniformManager& globals = GLOBAL_UNIFORM_MANAGER;
    const std::uint32_t textureCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(globals.textureProperties().size(), kMaxGlobalTextures));
    const bool hasUbo = !globals.uniformBlockLayout().members.empty();
    const std::uint64_t uboSize = globals.uniformBytes().size();

    if (!hasUbo && textureCount == 0) {
        layout_.reset();
        uboLayout_.reset();
        layoutHasUbo_ = false;
        layoutTextureCount_ = 0;
        layoutUboSize_ = 0;
        for (FrameResources& frame : frames_)
            frame.set.reset();
        return;
    }
    if (layout_ && layoutHasUbo_ == hasUbo && layoutTextureCount_ == textureCount &&
        layoutUboSize_ == uboSize) {
        return; // unchanged
    }

    // Mirrors ShaderGenerator::generateGlobalDeclarations: an optional UBO at binding 0 then
    // combined-image-samplers at 1..M. acquire() dedups against the reflected set-2 layout.
    std::vector<ResourceBinding> bindings;
    bindings.reserve(textureCount + 1);
    if (hasUbo)
        bindings.push_back({0, "Global", rhi::ResourceType::UniformBuffer});
    for (std::uint32_t i = 0; i < textureCount; ++i)
        bindings.push_back(
            {i + 1, "globalTexture" + std::to_string(i), rhi::ResourceType::CombinedImageSampler});
    layout_ = UniformSetLayout::acquire(bindings);

    if (hasUbo) {
        uboLayout_ = makeRef<UboLayout>();
        uboLayout_->addUniform(UboUniform{
            "GlobalData", UboUniformType::Float, 0, static_cast<std::uint32_t>(uboSize)});
        uboLayout_->finish();
    } else {
        uboLayout_.reset();
    }
    layoutHasUbo_ = hasUbo;
    layoutTextureCount_ = textureCount;
    layoutUboSize_ = uboSize;
    for (FrameResources& frame : frames_)
        frame.set.reset(); // rebuild against the new layout
}

void GlobalUniformGpuManager::beginFrame(std::uint32_t frameIndex) {
    if (!initialized() || frameIndex >= frames_.size())
        return;
    updateFrame(frameIndex);
}

const Ref<UniformSet>& GlobalUniformGpuManager::resolve(std::uint32_t frameIndex) const {
    static const Ref<UniformSet> kEmpty;
    return frameIndex < frames_.size() ? frames_[frameIndex].set : kEmpty;
}

void GlobalUniformGpuManager::updateFrame(std::uint32_t frameIndex) {
    ensureLayout();
    FrameResources& frame = frames_[frameIndex];
    if (!layout_) {
        frame.set.reset();
        return;
    }
    if (!frame.set) {
        std::vector<Ref<UboLayout>> uboLayouts;
        if (uboLayout_)
            uboLayouts.push_back(uboLayout_);
        frame.set = makeRef<UniformSet>(layout_, std::move(uboLayouts));
    }

    const GlobalUniformManager& globals = GLOBAL_UNIFORM_MANAGER;
    if (layoutHasUbo_) {
        if (Ubo* ubo = frame.set->uboAt(0))
            ubo->setRawBytes(globals.uniformBytes());
    }
    // Textures occupy positions [hasUbo ? 1 : 0] .. in the sorted-binding resource array.
    std::uint32_t pos = layoutHasUbo_ ? 1U : 0U;
    std::uint32_t filled = 0;
    for (const ShaderPropertyDesc& property : globals.textureProperties()) {
        if (filled >= layoutTextureCount_)
            break;
        const std::string* reference = globals.findTexture(property.name);
        Ref<Texture> texture = resolveTextureReference(reference ? *reference : "");
        if (!texture)
            texture = resolveTextureReference("");
        if (texture)
            frame.set->setResourceAt(pos, texture);
        ++pos;
        ++filled;
    }
    frame.set->upload();
}

void GlobalUniformGpuManager::shutdown() {
    if (!initialized())
        return;
    for (FrameResources& frame : frames_)
        frame.set.reset();
    frames_.clear();
    layout_.reset();
    uboLayout_.reset();
    layoutHasUbo_ = false;
    layoutTextureCount_ = 0;
    layoutUboSize_ = 0;
    initialized_ = false;
}

} // namespace engine

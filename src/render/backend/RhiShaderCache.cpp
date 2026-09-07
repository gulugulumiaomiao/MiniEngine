#include "render/backend/RhiShaderCache.h"

#include "rhi/api/Device.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace engine {

RhiShaderCache::RhiShaderCache(rhi::IDevice& device, CompiledShaderCache& compiledShaders)
    : device_(device), compiledShaders_(compiledShaders) {}

RhiShaderCache::~RhiShaderCache() {
    clear();
}

rhi::ShaderHandle RhiShaderCache::getOrCreate(CompiledShaderHandle handle) {
    const CompiledShader& shader = compiledShaders_.resolve(handle);
    if (const auto found = entries_.find(shader.id); found != entries_.end()) {
        return slots_[found->second].shader;
    }
    auto slot = std::ranges::find_if(slots_, [](const Slot& value) { return !value.shader; });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index = static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    slot->shader = device_.createShader({
        .stage = shader.stage == ShaderStage::Vertex ? rhi::ShaderStage::Vertex
                                                     : rhi::ShaderStage::Fragment,
        .bytecode = shader.bytecode,
        .debugName = "CompiledShader/" + std::to_string(shader.id),
    });
    slot->compiledId = shader.id;
    entries_[shader.id] = index;
    return slot->shader;
}

void RhiShaderCache::invalidate(std::span<const CompiledShaderId> shaders,
                                std::uint64_t retireSerial) {
    for (const CompiledShaderId id : shaders) {
        const auto found = entries_.find(id);
        if (found == entries_.end())
            continue;
        Slot& slot = slots_[found->second];
        retired_.push_back({slot.shader, retireSerial});
        slot.shader = {};
        slot.compiledId = 0;
        entries_.erase(found);
    }
}

void RhiShaderCache::collect(std::uint64_t completedSerial) {
    std::erase_if(retired_, [&](const RetiredModule& retired) {
        if (retired.serial > completedSerial)
            return false;
        device_.destroyShader(retired.shader);
        return true;
    });
}

void RhiShaderCache::clear() {
    entries_.clear();
    for (const RetiredModule& retired : retired_) {
        device_.destroyShader(retired.shader);
    }
    retired_.clear();
    for (Slot& slot : slots_) {
        device_.destroyShader(slot.shader);
        slot = {};
    }
}

} // namespace engine

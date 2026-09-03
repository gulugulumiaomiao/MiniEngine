#include "rhi/vulkan/RhiShaderCache.h"

#include "core/Log.h"
#include "rhi/vulkan/ShaderModule.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace engine {

RhiShaderCache::RhiShaderCache(VkDevice device,
                               CompiledShaderCache& compiledShaders)
    : device_(device), compiledShaders_(compiledShaders) {}

RhiShaderCache::~RhiShaderCache() = default;

rhi::ShaderHandle RhiShaderCache::getOrCreate(CompiledShaderHandle handle) {
    const CompiledShader& shader = compiledShaders_.resolve(handle);
    if (const auto found = entries_.find(shader.id); found != entries_.end()) {
        return {found->second, slots_[found->second].generation};
    }
    auto slot = std::ranges::find_if(slots_, [](const Slot& value) {
        return !value.module;
    });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    const std::string name = "CompiledShader/" + std::to_string(shader.id);
    slot->module =
        std::make_unique<ShaderModule>(device_, shader.bytecode, name);
    slot->compiledId = shader.id;
    entries_[shader.id] = index;
    return {index, slot->generation};
}

VkShaderModule RhiShaderCache::resolve(rhi::ShaderHandle handle) const {
    if (handle.index >= slots_.size()) {
        Log::fatal("RhiShaderCache", "Invalid shader handle");
    }
    const Slot& slot = slots_[handle.index];
    if (!slot.module || slot.generation != handle.generation) {
        Log::fatal("RhiShaderCache", "Stale shader handle");
    }
    return slot.module->handle();
}

void RhiShaderCache::invalidate(std::span<const CompiledShaderId> shaders,
                                std::uint64_t retireSerial) {
    for (const CompiledShaderId id : shaders) {
        const auto found = entries_.find(id);
        if (found == entries_.end()) continue;
        Slot& slot = slots_[found->second];
        retired_.push_back({std::move(slot.module), retireSerial});
        slot.compiledId = 0;
        ++slot.generation;
        entries_.erase(found);
    }
}

void RhiShaderCache::collect(std::uint64_t completedSerial) {
    std::erase_if(retired_, [completedSerial](const RetiredModule& retired) {
        return retired.serial <= completedSerial;
    });
}

void RhiShaderCache::clear() {
    entries_.clear();
    retired_.clear();
    for (Slot& slot : slots_) {
        if (slot.module) {
            slot.module.reset();
            ++slot.generation;
        }
        slot.compiledId = 0;
    }
}

} // namespace engine

#include "rhi/vulkan/PipelineCache.h"

#include "core/Log.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "rhi/vulkan/GraphicsPipeline.h"
#include "rhi/vulkan/RhiShaderCache.h"

#include <algorithm>
#include <iterator>
#include <span>

namespace engine {

PipelineCache::PipelineCache(VkDevice device, VkDescriptorSetLayout sceneLayout,
                             VkDescriptorSetLayout materialLayout,
                             CompiledShaderCache& compiledShaders,
                             ShaderProgramCache& programs,
                             RhiShaderCache& shaders)
    : device_(device), sceneLayout_(sceneLayout), materialLayout_(materialLayout),
      compiledShaders_(compiledShaders), programs_(programs), shaders_(shaders) {}

std::uint64_t PipelineCache::makeKey(const ShaderProgram& program,
                                     const ShaderPass& pass,
                                     const VertexLayout& vertexLayout,
                                     VkFormat colorFormat) {
    const RenderStateDesc& state = pass.renderState();
    ShaderHash key = program.id;
    auto append = [&key](const auto& value) {
        key = hashBytes({reinterpret_cast<const std::byte*>(&value), sizeof(value)}, key);
    };
    append(program.layout.id);
    append(colorFormat);
    append(state.cull); append(state.frontFace); append(state.fill);
    append(state.topology); append(state.depthWrite); append(state.depthTest);
    append(state.blend);
    key = hashString(state.colorMask, key);
    append(vertexLayout.stride);
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        append(attribute.location); append(attribute.offset);
        append(attribute.format); append(attribute.semantic);
    }
    return key;
}

rhi::GraphicsPipelineHandle PipelineCache::getOrCreate(
    const Shader& shader, const ShaderPass& pass,
    const ShaderVariantKey& variant,
    const VertexLayout& vertexLayout,
    VkFormat colorFormat) {
    const ShaderProgramHandle programHandle =
        programs_.getOrCreate(shader, pass, variant);
    if (!programHandle) {
        Log::error("PipelineCache", "Shader program is unavailable for pass: %s",
                   pass.name().c_str());
        return {};
    }
    const ShaderProgram& program = programs_.resolve(programHandle);
    const std::uint64_t key = makeKey(program, pass, vertexLayout, colorFormat);
    if (const auto found = entries_.find(key); found != entries_.end()) {
        const Slot& slot = slots_[found->second];
        return {found->second, slot.generation};
    }
    auto slot = std::ranges::find_if(slots_, [](const Slot& item) {
        return !item.alive;
    });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    const CompiledShader& vertex = compiledShaders_.resolve(program.vertex);
    const CompiledShader& fragment = compiledShaders_.resolve(program.fragment);
    const rhi::ShaderHandle vertexHandle = shaders_.getOrCreate(program.vertex);
    const rhi::ShaderHandle fragmentHandle = shaders_.getOrCreate(program.fragment);
    slot->pipeline = std::make_unique<GraphicsPipeline>(
        device_, colorFormat, sceneLayout_, materialLayout_, pass.renderState(),
        vertexLayout, shaders_.resolve(vertexHandle), vertex.entryPoint,
        shaders_.resolve(fragmentHandle), fragment.entryPoint);
    slot->program = program.id;
    slot->vertex = program.vertexId;
    slot->fragment = program.fragmentId;
    slot->key = key;
    slot->alive = true;
    entries_.emplace(slot->key, index);
    return {index, slot->generation};
}

const GraphicsPipeline& PipelineCache::resolve(
    rhi::GraphicsPipelineHandle handle) const {
    if (handle.index >= slots_.size()) {
        Log::fatal("PipelineCache", "Invalid graphics pipeline handle");
    }
    const Slot& slot = slots_[handle.index];
    if (!slot.alive || slot.generation != handle.generation || !slot.pipeline) {
        Log::fatal("PipelineCache", "Stale graphics pipeline handle");
    }
    return *slot.pipeline;
}

void PipelineCache::clear() {
    entries_.clear();
    for (Slot& slot : slots_) {
        if (slot.alive) {
            slot.pipeline.reset();
            slot.key = 0;
            slot.program = 0;
            slot.vertex = 0;
            slot.fragment = 0;
            slot.alive = false;
            ++slot.generation;
        }
    }
}

void PipelineCache::invalidate(std::span<const CompiledShaderId> shaders,
                               std::uint64_t retireSerial) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        Slot& slot = slots_[entry->second];
        const bool affected = std::ranges::find(shaders, slot.vertex) != shaders.end() ||
                              std::ranges::find(shaders, slot.fragment) != shaders.end();
        if (!affected) { ++entry; continue; }
        retired_.push_back({std::move(slot.pipeline), retireSerial});
        slot.alive = false;
        slot.key = 0;
        slot.program = 0;
        slot.vertex = 0;
        slot.fragment = 0;
        ++slot.generation;
        entry = entries_.erase(entry);
    }
}

void PipelineCache::collect(std::uint64_t completedSerial) {
    std::erase_if(retired_, [completedSerial](const RetiredPipeline& retired) {
        return retired.serial <= completedSerial;
    });
}

} // namespace engine

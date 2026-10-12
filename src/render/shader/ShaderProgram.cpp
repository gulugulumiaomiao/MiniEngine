#include "render/shader/ShaderProgram.h"
#include "render/shader/ShaderCompilePipeline.h"

#include "core/logging/Log.h"
#include "render/shader/Shader.h" // ShaderDescriptorBinding/Type, ShaderUniformMember
#include "render/uniform/Ubo.h"
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <map>
#include <utility>

namespace engine {
namespace {

// Layer-1 reflection descriptor type -> backend-agnostic rhi descriptor type. This mapping lives
// here (layer 2) because render/shader/Shader.h must stay rhi-free.
[[nodiscard]] rhi::ResourceType toResourceType(ShaderDescriptorType type) {
    switch (type) {
    case ShaderDescriptorType::UniformBuffer: return rhi::ResourceType::UniformBuffer;
    case ShaderDescriptorType::StorageBuffer: return rhi::ResourceType::StorageBuffer;
    case ShaderDescriptorType::Sampler: return rhi::ResourceType::Sampler;
    case ShaderDescriptorType::SampledImage: return rhi::ResourceType::SampledImage;
    case ShaderDescriptorType::CombinedImageSampler: return rhi::ResourceType::CombinedImageSampler;
    case ShaderDescriptorType::InputAttachment: return rhi::ResourceType::InputAttachment;
    case ShaderDescriptorType::Unknown: return rhi::ResourceType::UniformBuffer;
    }
    return rhi::ResourceType::UniformBuffer;
}

[[nodiscard]] bool isBufferType(ShaderDescriptorType type) {
    return type == ShaderDescriptorType::UniformBuffer;
}

// Builds a UboLayout from a reflected uniform/storage block's members (already typed + std140-sized
// by the reflection pass).
[[nodiscard]] Ref<UboLayout> buildUboLayout(const ShaderDescriptorBinding& binding) {
    Ref<UboLayout> layout = makeRef<UboLayout>();
    for (const ShaderUniformMember& member : binding.members)
        layout->addUniform(UboUniform{member.name, member.type, member.offset, member.size});
    layout->finish();
    return layout;
}

} // namespace

Ref<ShaderProgram> ShaderProgram::create(const CompiledShader& vertex,
                                         const CompiledShader& fragment,
                                         ShaderProgramId id) {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device) {
        Log::error("ShaderProgram", "No active device for shader-program creation");
        return {};
    }
    Ref<ShaderProgram> program(new ShaderProgram());
    program->id_ = id;
    program->deviceUid_ = device->uid();
    program->vertexEntry_ = vertex.entryPoint;
    program->fragmentEntry_ = fragment.entryPoint;
    program->vertex_ = device->shader_create(
        rhi::ShaderDesc{rhi::ShaderStage::Vertex, vertex.bytecode, "ShaderProgram.vs"});
    program->fragment_ = device->shader_create(
        rhi::ShaderDesc{rhi::ShaderStage::Fragment, fragment.bytecode, "ShaderProgram.fs"});

    // Merge vs+fs descriptors keyed by (set, binding). The two stages are validated upstream to
    // agree on each shared binding's type, so first-wins is safe; std::map keeps (set, binding)
    // ordered so each set's bindings come out ascending.
    std::map<std::pair<std::uint32_t, std::uint32_t>, const ShaderDescriptorBinding*> merged;
    for (const ShaderDescriptorBinding& descriptor : vertex.reflection.descriptors)
        merged.emplace(std::make_pair(descriptor.set, descriptor.binding), &descriptor);
    for (const ShaderDescriptorBinding& descriptor : fragment.reflection.descriptors)
        merged.emplace(std::make_pair(descriptor.set, descriptor.binding), &descriptor);

    std::uint32_t setCount = 0;
    for (const auto& [key, binding] : merged)
        setCount = std::max(setCount, key.first + 1);

    std::vector<Ref<UniformSetLayout>> setLayouts;
    setLayouts.reserve(setCount);
    program->setUboLayouts_.assign(setCount, {});
    program->bindings_.assign(setCount, {});
    for (std::uint32_t set = 0; set < setCount; ++set) {
        std::vector<ResourceBinding> bindings;
        std::vector<Ref<UboLayout>> uboLayouts;
        for (const auto& [key, binding] : merged) {
            if (key.first != set)
                continue;
            bindings.push_back(
                ResourceBinding{binding->binding, binding->name, toResourceType(binding->type)});
            if (isBufferType(binding->type))
                uboLayouts.push_back(buildUboLayout(*binding));
        }
        program->bindings_[set] = bindings;
        setLayouts.push_back(UniformSetLayout::acquire(bindings));
        program->setUboLayouts_[set] = std::move(uboLayouts);
    }
    program->pipelineLayout_ = PipelineLayout::acquire(std::move(setLayouts));
    return program;
}

ShaderProgram::~ShaderProgram() {
    // setUboLayouts_ / pipelineLayout_ release through their Ref members (cached layouts persist in
    // their pools). Only the vs/fs shader modules are owned outright; skip teardown if the creating
    // device is no longer active (its pools are already gone).
    if (rhi::IDevice* device = RHI_DEVICE; device && device->uid() == deviceUid_) {
        if (vertex_)
            device->shader_destroy(vertex_);
        if (fragment_)
            device->shader_destroy(fragment_);
    }
    vertex_ = {};
    fragment_ = {};
}

} // namespace engine

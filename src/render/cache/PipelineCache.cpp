#include "render/cache/PipelineCache.h"

#include "core/logging/Log.h"
#include "render/cache/RhiShaderCache.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <utility>

namespace engine {
namespace {

rhi::VertexFormat toRhi(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32: return rhi::VertexFormat::Float32;
    case VertexFormat::Vec2Float32: return rhi::VertexFormat::Vec2Float32;
    case VertexFormat::Vec3Float32: return rhi::VertexFormat::Vec3Float32;
    case VertexFormat::Vec4Float32: return rhi::VertexFormat::Vec4Float32;
    case VertexFormat::UInt16x4: return rhi::VertexFormat::UInt16x4;
    case VertexFormat::UInt8x4Normalized: return rhi::VertexFormat::UInt8x4Normalized;
    }
    Log::fatal("PipelineCache", "Unsupported Mesh vertex format");
}

rhi::CullMode toRhi(CullMode mode) {
    switch (mode) {
    case CullMode::Off: return rhi::CullMode::None;
    case CullMode::Front: return rhi::CullMode::Front;
    case CullMode::Back: return rhi::CullMode::Back;
    }
    return rhi::CullMode::None;
}

rhi::CompareOp toRhi(DepthCompare compare) {
    switch (compare) {
    case DepthCompare::Never: return rhi::CompareOp::Never;
    case DepthCompare::Less: return rhi::CompareOp::Less;
    case DepthCompare::LessEqual: return rhi::CompareOp::LessEqual;
    case DepthCompare::Equal: return rhi::CompareOp::Equal;
    case DepthCompare::Greater: return rhi::CompareOp::Greater;
    case DepthCompare::GreaterEqual: return rhi::CompareOp::GreaterEqual;
    case DepthCompare::Always: return rhi::CompareOp::Always;
    }
    return rhi::CompareOp::Always;
}

rhi::ColorWriteMask toRhiColorMask(std::string_view mask) {
    rhi::ColorWriteMask result = rhi::ColorWriteMask::None;
    if (mask.find('R') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Red;
    if (mask.find('G') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Green;
    if (mask.find('B') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Blue;
    if (mask.find('A') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Alpha;
    return result;
}

} // namespace

PipelineCache::PipelineCache(rhi::IDevice& device,
                             rhi::BindGroupLayoutHandle sceneLayout,
                             rhi::BindGroupLayoutHandle materialLayout,
                             CompiledShaderCache& compiledShaders,
                             ShaderProgramCache& programs,
                             RhiShaderCache& shaders)
    : device_(device), sceneLayout_(sceneLayout), materialLayout_(materialLayout),
      compiledShaders_(compiledShaders), programs_(programs), shaders_(shaders) {}

std::uint64_t PipelineCache::makeKey(const ShaderProgram& program,
                                     const ShaderPass& pass,
                                     const VertexLayout& vertexLayout,
                                     rhi::TextureFormat colorFormat) {
    const RenderStateDesc& state = pass.renderState();
    ShaderHash key = program.id;
    auto append = [&key](const auto& value) {
        key = hashBytes({reinterpret_cast<const std::byte*>(&value), sizeof(value)}, key);
    };
    append(program.layout.id);
    append(colorFormat);
    append(state.cull);
    append(state.frontFace);
    append(state.fill);
    append(state.topology);
    append(state.depthWrite);
    append(state.depthTest);
    append(state.blend);
    key = hashString(state.colorMask, key);
    for (const VertexBinding& binding : vertexLayout.bindings) {
        append(binding.binding);
        append(binding.stride);
        append(binding.inputRate);
    }
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        append(attribute.location);
        append(attribute.offset);
        append(attribute.binding);
        append(attribute.format);
        append(attribute.semantic.type);
        append(attribute.semantic.index);
    }
    return key;
}

std::uint64_t PipelineCache::makeFallbackKey(const Shader& shader,
                                             const ShaderPass& pass,
                                             const ShaderVariantKey& variant,
                                             const VertexLayout& vertexLayout,
                                             rhi::TextureFormat colorFormat) {
    ShaderHash key = hashString(shader.assetPath().string());
    key = hashString(pass.name(), key);
    auto append = [&key](const auto& value) {
        key = hashBytes({reinterpret_cast<const std::byte*>(&value), sizeof(value)}, key);
    };
    append(pass.type());
    append(variant.keywordBits);
    append(variant.meshFeatureBits);
    append(variant.platformFeatureBits);
    append(colorFormat);
    for (const VertexBinding& binding : vertexLayout.bindings) {
        append(binding.binding);
        append(binding.stride);
        append(binding.inputRate);
    }
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        append(attribute.location);
        append(attribute.offset);
        append(attribute.binding);
        append(attribute.format);
        append(attribute.semantic.type);
        append(attribute.semantic.index);
    }
    return key;
}

rhi::GraphicsPipelineDesc PipelineCache::makeDesc(const ShaderPass& pass,
                                                  const VertexLayout& vertexLayout,
                                                  rhi::TextureFormat colorFormat,
                                                  rhi::ShaderHandle vertexShader,
                                                  std::string vertexEntry,
                                                  rhi::ShaderHandle fragmentShader,
                                                  std::string fragmentEntry) const {
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.vertexEntry = std::move(vertexEntry);
    desc.fragmentShader = fragmentShader;
    desc.fragmentEntry = std::move(fragmentEntry);
    desc.bindGroupLayouts = {sceneLayout_, materialLayout_};
    desc.colorFormats = {colorFormat};

    desc.vertexBindings.reserve(vertexLayout.bindings.size());
    for (const VertexBinding& binding : vertexLayout.bindings) {
        desc.vertexBindings.push_back({binding.binding,
                                       binding.stride,
                                       binding.inputRate == VertexInputRate::Vertex
                                           ? rhi::VertexInputRate::Vertex
                                           : rhi::VertexInputRate::Instance});
    }
    desc.vertexAttributes.reserve(vertexLayout.attributes.size());
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        desc.vertexAttributes.push_back(
            {attribute.location, attribute.binding, toRhi(attribute.format), attribute.offset});
    }

    const RenderStateDesc& state = pass.renderState();
    desc.topology = state.topology == PrimitiveTopology::TriangleList
                        ? rhi::PrimitiveTopology::TriangleList
                        : rhi::PrimitiveTopology::LineList;
    desc.raster.cull = toRhi(state.cull);
    desc.raster.frontFace = state.frontFace == FrontFace::Clockwise
                                ? rhi::FrontFace::Clockwise
                                : rhi::FrontFace::CounterClockwise;
    desc.raster.fill =
        state.fill == FillMode::Solid ? rhi::FillMode::Solid : rhi::FillMode::Wireframe;
    desc.depthStencil.depthTestEnable = state.depthTest != DepthCompare::Always || state.depthWrite;
    desc.depthStencil.depthWriteEnable = state.depthWrite;
    desc.depthStencil.depthCompare = toRhi(state.depthTest);
    switch (state.blend) {
    case BlendMode::Off: desc.blend.mode = rhi::BlendMode::Off; break;
    case BlendMode::Alpha: desc.blend.mode = rhi::BlendMode::Alpha; break;
    case BlendMode::Additive: desc.blend.mode = rhi::BlendMode::Additive; break;
    case BlendMode::PremultipliedAlpha: desc.blend.mode = rhi::BlendMode::PremultipliedAlpha; break;
    }
    desc.blend.colorWriteMask = toRhiColorMask(state.colorMask);
    return desc;
}

rhi::GraphicsPipelineHandle PipelineCache::getOrCreate(const Shader& shader,
                                                       const ShaderPass& pass,
                                                       const ShaderVariantKey& variant,
                                                       const VertexLayout& vertexLayout,
                                                       rhi::TextureFormat colorFormat) {
    const std::uint64_t fallbackKey =
        makeFallbackKey(shader, pass, variant, vertexLayout, colorFormat);
    const ShaderProgramHandle programHandle = programs_.getOrCreate(shader, pass, variant);
    if (!programHandle) {
        if (const auto previous = fallbackEntries_.find(fallbackKey);
            previous != fallbackEntries_.end()) {
            const Slot& slot = slots_[previous->second];
            if (slot.alive && slot.pipeline) {
                Log::error("PipelineCache",
                           "Keeping the previous pipeline after Shader reload "
                           "failure: %s/%s",
                           shader.name().c_str(),
                           pass.name().c_str());
                return slot.pipeline;
            }
        }
        Log::error(
            "PipelineCache", "Shader program is unavailable for pass: %s", pass.name().c_str());
        return {};
    }

    const ShaderProgram& program = programs_.resolve(programHandle);
    const std::uint64_t key = makeKey(program, pass, vertexLayout, colorFormat);
    if (const auto found = entries_.find(key); found != entries_.end()) {
        const Slot& slot = slots_[found->second];
        fallbackEntries_.insert_or_assign(fallbackKey, found->second);
        return slot.pipeline;
    }

    auto slot = std::ranges::find_if(slots_, [](const Slot& item) { return !item.alive; });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index = static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    const CompiledShader& vertex = compiledShaders_.resolve(program.vertex);
    const CompiledShader& fragment = compiledShaders_.resolve(program.fragment);
    const rhi::ShaderHandle vertexHandle = shaders_.getOrCreate(program.vertex);
    const rhi::ShaderHandle fragmentHandle = shaders_.getOrCreate(program.fragment);
    slot->pipeline = device_.createGraphicsPipeline(makeDesc(pass,
                                                             vertexLayout,
                                                             colorFormat,
                                                             vertexHandle,
                                                             vertex.entryPoint,
                                                             fragmentHandle,
                                                             fragment.entryPoint));
    slot->program = program.id;
    slot->vertex = program.vertexId;
    slot->fragment = program.fragmentId;
    slot->key = key;
    slot->alive = true;
    entries_.emplace(slot->key, index);
    fallbackEntries_.insert_or_assign(fallbackKey, index);
    return slot->pipeline;
}

void PipelineCache::clear() {
    entries_.clear();
    fallbackEntries_.clear();
    for (const RetiredPipeline& retired : retired_) {
        device_.destroyGraphicsPipeline(retired.pipeline);
    }
    retired_.clear();
    for (Slot& slot : slots_) {
        if (slot.alive)
            device_.destroyGraphicsPipeline(slot.pipeline);
        slot = {};
    }
}

void PipelineCache::invalidate(std::span<const CompiledShaderId> shaders,
                               std::uint64_t retireSerial) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        Slot& slot = slots_[entry->second];
        const bool affected = std::ranges::find(shaders, slot.vertex) != shaders.end() ||
                              std::ranges::find(shaders, slot.fragment) != shaders.end();
        if (!affected) {
            ++entry;
            continue;
        }
        std::erase_if(fallbackEntries_,
                      [index = entry->second](const auto& item) { return item.second == index; });
        retired_.push_back({slot.pipeline, retireSerial});
        slot = {};
        entry = entries_.erase(entry);
    }
}

void PipelineCache::collect(std::uint64_t completedSerial) {
    std::erase_if(retired_, [&](const RetiredPipeline& retired) {
        if (retired.serial > completedSerial)
            return false;
        device_.destroyGraphicsPipeline(retired.pipeline);
        return true;
    });
}

} // namespace engine

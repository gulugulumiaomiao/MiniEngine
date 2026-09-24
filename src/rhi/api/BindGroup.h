#pragma once

#include "rhi/api/RhiTypes.h" // RID

#include <cstdint>
#include <span>
#include <string>

namespace engine::rhi {

enum class ShaderVisibility : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Fragment = 1U << 1U,
};

constexpr ShaderVisibility operator|(ShaderVisibility left, ShaderVisibility right) {
    return static_cast<ShaderVisibility>(static_cast<std::uint32_t>(left) |
                                         static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(ShaderVisibility value, ShaderVisibility flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class BindingType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
};

struct BindGroupLayoutEntry {
    std::uint32_t binding{};
    BindingType type{BindingType::UniformBuffer};
    ShaderVisibility visibility{ShaderVisibility::None};
};

struct BindGroupLayoutDesc {
    std::span<const BindGroupLayoutEntry> entries;
    std::string debugName;
};

struct BindGroupEntry {
    std::uint32_t binding{};
    BindingType type{BindingType::UniformBuffer};
    RID buffer;
    std::uint64_t offset{};
    std::uint64_t size{};
    RID textureView;
    RID sampler;
};

struct BindGroupDesc {
    RID layout;
    std::span<const BindGroupEntry> entries;
    std::string debugName;
};

} // namespace engine::rhi

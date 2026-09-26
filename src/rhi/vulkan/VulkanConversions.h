#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <utility>

// Single home for every RHI <-> Vulkan enum translation. The concrete resource objects and the
// device call these overloads instead of keeping private copies in their own translation units,
// so each mapping exists exactly once and an unsupported value is reported uniformly. Composite
// state (ResourceState -> stage/access/layout, BlendMode -> equation) stays with its only user,
// the command buffer.
namespace engine::rhi::vulkan {

// Formats.
[[nodiscard]] VkFormat toVulkan(PixelFormat format);
[[nodiscard]] PixelFormat toRhi(VkFormat format);
[[nodiscard]] VkFormat toVulkan(VertexFormat format);
[[nodiscard]] VkComponentSwizzle toVulkan(SwizzleComponent component);

// Buffers.
[[nodiscard]] VkBufferUsageFlags toVulkan(BufferUsage usage);
[[nodiscard]] std::pair<VmaMemoryUsage, VmaAllocationCreateFlags> toVulkan(MemoryUsage usage);

// Textures.
[[nodiscard]] VkImageUsageFlags toVulkan(TextureUsage usage);
[[nodiscard]] VkSampleCountFlagBits toVulkan(SampleCount samples);

// Samplers.
[[nodiscard]] VkFilter toVulkan(SamplerFilter filter);
[[nodiscard]] VkSamplerAddressMode toVulkan(SamplerAddressMode mode);
[[nodiscard]] VkBorderColor toVulkan(SamplerBorderColor color);
[[nodiscard]] VkCompareOp toVulkan(CompareOp compare);

// Bindings.
[[nodiscard]] VkDescriptorType toVulkan(BindingType type);
[[nodiscard]] VkShaderStageFlags toVulkan(ShaderVisibility visibility);

// Dynamic pipeline states.
[[nodiscard]] VkCullModeFlags toVulkan(CullMode mode);
[[nodiscard]] VkFrontFace toVulkan(FrontFace face);
[[nodiscard]] VkColorComponentFlags toVulkan(ColorWriteMask mask);

} // namespace engine::rhi::vulkan

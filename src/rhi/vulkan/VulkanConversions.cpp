#include "rhi/vulkan/VulkanConversions.h"

#include "core/logging/Log.h"

namespace engine::rhi::vulkan {
namespace {

constexpr std::string_view kLogSource{"VulkanConversions"};

} // namespace

VkFormat toVulkan(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case PixelFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case PixelFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case PixelFormat::Undefined: break;
    }
    Log::fatal(kLogSource, "Unsupported RHI pixel format");
}

PixelFormat toRhi(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return PixelFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return PixelFormat::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return PixelFormat::Bgra8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return PixelFormat::Bgra8Srgb;
    default: break;
    }
    Log::fatal(kLogSource, "Unsupported Vulkan format");
}

VkFormat toVulkan(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Vec2Float32: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Vec3Float32: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Vec4Float32: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UInt16x4: return VK_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::UInt8x4Normalized: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    Log::fatal(kLogSource, "Unsupported RHI vertex format");
}

VkComponentSwizzle toVulkan(SwizzleComponent component) {
    switch (component) {
    case SwizzleComponent::Identity: return VK_COMPONENT_SWIZZLE_IDENTITY;
    case SwizzleComponent::Zero: return VK_COMPONENT_SWIZZLE_ZERO;
    case SwizzleComponent::One: return VK_COMPONENT_SWIZZLE_ONE;
    case SwizzleComponent::R: return VK_COMPONENT_SWIZZLE_R;
    case SwizzleComponent::G: return VK_COMPONENT_SWIZZLE_G;
    case SwizzleComponent::B: return VK_COMPONENT_SWIZZLE_B;
    case SwizzleComponent::A: return VK_COMPONENT_SWIZZLE_A;
    }
    Log::fatal(kLogSource, "Unsupported RHI component swizzle");
}

VkBufferUsageFlags toVulkan(BufferUsage usage) {
    VkBufferUsageFlags result = 0;
    if (hasFlag(usage, BufferUsage::Vertex))
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Index))
        result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Uniform))
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Storage))
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::TransferSource))
        result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, BufferUsage::TransferDestination))
        result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return result;
}

std::pair<VmaMemoryUsage, VmaAllocationCreateFlags> toVulkan(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal: return {VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0};
    case MemoryUsage::Upload:
        return {VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT};
    case MemoryUsage::Readback:
        return {VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT};
    }
    Log::fatal(kLogSource, "Unsupported RHI memory usage");
}

VkImageUsageFlags toVulkan(TextureUsage usage) {
    VkImageUsageFlags result{};
    if (hasFlag(usage, TextureUsage::Sampled))
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, TextureUsage::TransferSource))
        result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, TextureUsage::TransferDestination))
        result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, TextureUsage::ColorAttachment))
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasFlag(usage, TextureUsage::DepthStencilAttachment))
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return result;
}

VkSampleCountFlagBits toVulkan(SampleCount samples) {
    switch (samples) {
    case SampleCount::One: return VK_SAMPLE_COUNT_1_BIT;
    case SampleCount::Two: return VK_SAMPLE_COUNT_2_BIT;
    case SampleCount::Four: return VK_SAMPLE_COUNT_4_BIT;
    case SampleCount::Eight: return VK_SAMPLE_COUNT_8_BIT;
    }
    Log::fatal(kLogSource, "Unsupported RHI sample count");
}

VkFilter toVulkan(SamplerFilter filter) {
    return filter == SamplerFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode toVulkan(SamplerAddressMode mode) {
    switch (mode) {
    case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    Log::fatal(kLogSource, "Unsupported RHI sampler address mode");
}

VkBorderColor toVulkan(SamplerBorderColor color) {
    switch (color) {
    case SamplerBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case SamplerBorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case SamplerBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    Log::fatal(kLogSource, "Unsupported RHI sampler border color");
}

VkCompareOp toVulkan(CompareOp compare) {
    switch (compare) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    Log::fatal(kLogSource, "Unsupported RHI compare operation");
}

VkDescriptorType toVulkan(BindingType type) {
    switch (type) {
    case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    Log::fatal(kLogSource, "Unsupported RHI binding type");
}

VkShaderStageFlags toVulkan(ShaderVisibility visibility) {
    VkShaderStageFlags result{};
    if (hasFlag(visibility, ShaderVisibility::Vertex))
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(visibility, ShaderVisibility::Fragment))
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    return result;
}

VkCullModeFlags toVulkan(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    Log::fatal(kLogSource, "Unsupported RHI cull mode");
}

VkFrontFace toVulkan(FrontFace face) {
    return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkColorComponentFlags toVulkan(ColorWriteMask mask) {
    VkColorComponentFlags result = 0;
    if (hasFlag(mask, ColorWriteMask::Red))
        result |= VK_COLOR_COMPONENT_R_BIT;
    if (hasFlag(mask, ColorWriteMask::Green))
        result |= VK_COLOR_COMPONENT_G_BIT;
    if (hasFlag(mask, ColorWriteMask::Blue))
        result |= VK_COLOR_COMPONENT_B_BIT;
    if (hasFlag(mask, ColorWriteMask::Alpha))
        result |= VK_COLOR_COMPONENT_A_BIT;
    return result;
}

} // namespace engine::rhi::vulkan

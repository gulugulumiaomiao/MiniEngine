#include "rhi/vulkan/VulkanGraphicsPipeline.h"

#include "core/logging/Log.h"

#include <array>
#include <vector>

namespace engine::rhi::vulkan {
namespace {

VkCullModeFlags toVulkan(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
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
    return VK_COMPARE_OP_ALWAYS;
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
    Log::fatal("VulkanGraphicsPipeline", "Unsupported vertex format");
}

VkFormat toVulkan(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::Undefined: break;
    }
    Log::fatal("VulkanGraphicsPipeline", "Unsupported attachment format");
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

void applyBlend(BlendMode mode, VkPipelineColorBlendAttachmentState& blend) {
    if (mode == BlendMode::Off) {
        blend.blendEnable = VK_FALSE;
        return;
    }
    blend.blendEnable = VK_TRUE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.dstColorBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.srcColorBlendFactor =
        mode == BlendMode::Alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

} // namespace

VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    VkDevice device,
    const GraphicsPipelineDesc& desc,
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    std::span<const VkDescriptorSetLayout> descriptorLayouts)
    : device_(device) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = desc.vertexEntry.c_str();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = desc.fragmentEntry.c_str();

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    vertexBindings.reserve(desc.vertexBindings.size());
    for (const VertexBindingDesc& binding : desc.vertexBindings) {
        vertexBindings.push_back({binding.binding,
                                  binding.stride,
                                  binding.inputRate == VertexInputRate::Vertex
                                      ? VK_VERTEX_INPUT_RATE_VERTEX
                                      : VK_VERTEX_INPUT_RATE_INSTANCE});
    }
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    vertexAttributes.reserve(desc.vertexAttributes.size());
    for (const VertexAttributeDesc& attribute : desc.vertexAttributes) {
        vertexAttributes.push_back(
            {attribute.location, attribute.binding, toVulkan(attribute.format), attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexBindings.size());
    vertexInput.pVertexBindingDescriptions = vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = desc.topology == PrimitiveTopology::TriangleList
                                 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                 : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode =
        desc.raster.fill == FillMode::Solid ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE;
    rasterizer.cullMode = toVulkan(desc.raster.cull);
    rasterizer.frontFace = desc.raster.frontFace == FrontFace::Clockwise
                               ? VK_FRONT_FACE_CLOCKWISE
                               : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size());
    for (VkPipelineColorBlendAttachmentState& attachment : blendAttachments) {
        attachment.colorWriteMask = toVulkan(desc.blend.colorWriteMask);
        applyBlend(desc.blend.mode, attachment);
    }
    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable;
    depthStencil.depthCompareOp = toVulkan(desc.depthStencil.depthCompare);

    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<std::uint32_t>(descriptorLayouts.size());
    layoutInfo.pSetLayouts = descriptorLayouts.data();
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        Log::fatal("VulkanGraphicsPipeline", "vkCreatePipelineLayout failed");
    }

    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (TextureFormat format : desc.colorFormats) {
        colorFormats.push_back(toVulkan(format));
    }
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    if (desc.depthFormat != TextureFormat::Undefined) {
        renderingInfo.depthAttachmentFormat = toVulkan(desc.depthFormat);
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout_;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) !=
        VK_SUCCESS) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
        Log::fatal("VulkanGraphicsPipeline", "vkCreateGraphicsPipelines failed");
    }
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, layout_, nullptr);
}

} // namespace engine::rhi::vulkan

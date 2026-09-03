#include "rhi/vulkan/GraphicsPipeline.h"

#include "core/Log.h"

#include "renderer/Mesh.h"
#include "renderer/Shader.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine {
namespace {

VkCullModeFlags toVulkan(CullMode mode) {
    switch (mode) {
    case CullMode::Off: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp toVulkan(DepthCompare compare) {
    switch (compare) {
    case DepthCompare::Never: return VK_COMPARE_OP_NEVER;
    case DepthCompare::Less: return VK_COMPARE_OP_LESS;
    case DepthCompare::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case DepthCompare::Equal: return VK_COMPARE_OP_EQUAL;
    case DepthCompare::Greater: return VK_COMPARE_OP_GREATER;
    case DepthCompare::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case DepthCompare::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkColorComponentFlags colorMask(const std::string& mask) {
    VkColorComponentFlags flags = 0;
    if (mask.find('R') != std::string::npos) flags |= VK_COLOR_COMPONENT_R_BIT;
    if (mask.find('G') != std::string::npos) flags |= VK_COLOR_COMPONENT_G_BIT;
    if (mask.find('B') != std::string::npos) flags |= VK_COLOR_COMPONENT_B_BIT;
    if (mask.find('A') != std::string::npos) flags |= VK_COLOR_COMPONENT_A_BIT;
    return flags;
}

void applyBlend(BlendMode mode, VkPipelineColorBlendAttachmentState& blend) {
    if (mode == BlendMode::Off) {
        blend.blendEnable = VK_FALSE;
        return;
    }
    blend.blendEnable = VK_TRUE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.dstColorBlendFactor = mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE
                                                            : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.srcColorBlendFactor = mode == BlendMode::Alpha ? VK_BLEND_FACTOR_SRC_ALPHA
                                                         : VK_BLEND_FACTOR_ONE;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE
                                                            : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
    Log::fatal("GraphicsPipeline", "Unsupported VertexFormat");
}

} // namespace

GraphicsPipeline::GraphicsPipeline(VkDevice device, VkFormat colorFormat,
                                   VkDescriptorSetLayout sceneLayout,
                                   VkDescriptorSetLayout materialLayout,
                                   const RenderStateDesc& state,
                                   const VertexLayout& vertexLayout,
                                   VkShaderModule vertexShader,
                                   std::string_view vertexEntry,
                                   VkShaderModule fragmentShader,
                                   std::string_view fragmentEntry)
    : device_(device) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = vertexEntry.data();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = fragmentEntry.data();

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkVertexInputBindingDescription vertexBinding{0, vertexLayout.stride,
                                                        VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    vertexAttributes.reserve(vertexLayout.attributes.size());
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        vertexAttributes.push_back({attribute.location, 0, toVulkan(attribute.format),
                                    attribute.offset});
    }
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = state.topology == PrimitiveTopology::TriangleList
                                 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                 : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = state.fill == FillMode::Solid ? VK_POLYGON_MODE_FILL
                                                           : VK_POLYGON_MODE_LINE;
    rasterizer.cullMode = toVulkan(state.cull);
    rasterizer.frontFace = state.frontFace == FrontFace::Clockwise
                               ? VK_FRONT_FACE_CLOCKWISE
                               : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = colorMask(state.colorMask);
    applyBlend(state.blend, blendAttachment);
    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable =
        state.depthTest != DepthCompare::Always || state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVulkan(state.depthTest);

    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    const std::array descriptorLayouts{sceneLayout, materialLayout};
    layoutInfo.setLayoutCount = static_cast<std::uint32_t>(descriptorLayouts.size());
    layoutInfo.pSetLayouts = descriptorLayouts.data();
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        Log::fatal("GraphicsPipeline", "vkCreatePipelineLayout failed");
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

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

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &pipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
        Log::fatal("GraphicsPipeline", "vkCreateGraphicsPipelines failed");
    }
}

GraphicsPipeline::~GraphicsPipeline() {
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, layout_, nullptr);
}

} // namespace engine

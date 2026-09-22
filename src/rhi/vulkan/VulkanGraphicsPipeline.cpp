#include "rhi/vulkan/VulkanGraphicsPipeline.h"

#include "core/logging/Log.h"

#include <array>
#include <vector>

namespace engine::rhi::vulkan {
namespace {

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

VkFormat toVulkan(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case PixelFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case PixelFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case PixelFormat::Undefined: break;
    }
    Log::fatal("VulkanGraphicsPipeline", "Unsupported attachment format");
}

} // namespace

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VkDevice device,
                                               const GraphicsPipelineDesc& desc,
                                               VkShaderModule vertexShader,
                                               VkShaderModule fragmentShader,
                                               VkPipelineLayout layout,
                                               VkPipelineCache cache)
    : device_(device), layout_(layout) {
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
    vertexBindings.reserve(desc.vertexStreams.size());
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    vertexAttributes.reserve(desc.vertexStreams.size());
    for (const VertexStreamDesc& stream : desc.vertexStreams) {
        vertexBindings.push_back({stream.binding,
                                  stream.stride,
                                  stream.inputRate == VertexInputRate::Vertex
                                      ? VK_VERTEX_INPUT_RATE_VERTEX
                                      : VK_VERTEX_INPUT_RATE_INSTANCE});
        vertexAttributes.push_back({stream.location, stream.binding, toVulkan(stream.format), 0});
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
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blend and depth/stencil states are fully dynamic; use benign defaults
    // for pipeline creation and let the command buffer configure them per batch.
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size());
    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR,
                                       VK_DYNAMIC_STATE_CULL_MODE,
                                       VK_DYNAMIC_STATE_FRONT_FACE,
                                       VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                       VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                       VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
                                       VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
                                       VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
                                       VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
                                       VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                                       VK_DYNAMIC_STATE_POLYGON_MODE_EXT};
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (PixelFormat format : desc.colorFormats) {
        colorFormats.push_back(toVulkan(format));
    }
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    if (desc.depthFormat != PixelFormat::Undefined) {
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

    if (vkCreateGraphicsPipelines(device_, cache, 1, &pipelineInfo, nullptr, &pipeline_) !=
        VK_SUCCESS) {
        Log::fatal("VulkanGraphicsPipeline", "vkCreateGraphicsPipelines failed");
    }
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
    vkDestroyPipeline(device_, pipeline_, nullptr);
}

} // namespace engine::rhi::vulkan

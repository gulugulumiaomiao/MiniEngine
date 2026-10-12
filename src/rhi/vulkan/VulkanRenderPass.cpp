#include "rhi/vulkan/VulkanRenderPass.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanSynchronization.h"
#include <utility>

namespace engine::rhi::vulkan {

VulkanRenderPass::VulkanRenderPass(VkDevice device, RenderPassDesc desc)
    : device_(device), desc_(std::move(desc)) {
    if (desc_.attachments.empty() || desc_.subpasses.empty())
        Log::fatal("VulkanRenderPass", "Render pass must have attachments and subpasses");
    std::vector<VkAttachmentDescription> attachments;
    for (const auto& a : desc_.attachments) {
        if (a.format == PixelFormat::Undefined || a.finalState == ResourceState::Undefined)
            Log::fatal("VulkanRenderPass", "Invalid attachment description");
        attachments.push_back({0, toVulkan(a.format), toVulkan(a.samples), mapLoadOp(a.loadOp),
                               mapStoreOp(a.storeOp), VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                               VK_ATTACHMENT_STORE_OP_DONT_CARE, mapState(a.initialState).layout,
                               mapState(a.finalState).layout});
    }
    const auto reference = [&](const AttachmentReference& ref) {
        if (ref.attachment != kUnusedAttachment && ref.attachment >= attachments.size())
            Log::fatal("VulkanRenderPass", "Attachment index out of range");
        return VkAttachmentReference{ref.attachment, mapState(ref.state).layout};
    };
    struct References {
        std::vector<VkAttachmentReference> colors, inputs;
        VkAttachmentReference depth{};
    };
    std::vector<References> refs(desc_.subpasses.size());
    std::vector<VkSubpassDescription> subpasses(desc_.subpasses.size());
    for (std::size_t i = 0; i < subpasses.size(); ++i) {
        const auto& s = desc_.subpasses[i];
        auto& r = refs[i];
        for (const auto& a : s.colors) r.colors.push_back(reference(a));
        for (const auto& a : s.inputs) r.inputs.push_back(reference(a));
        r.depth = reference(s.depth);
        for (const auto a : s.preserves)
            if (a >= attachments.size()) Log::fatal("VulkanRenderPass", "Invalid preserve index");
        auto& native = subpasses[i];
        native.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        native.colorAttachmentCount = static_cast<std::uint32_t>(r.colors.size());
        native.pColorAttachments = r.colors.data();
        native.inputAttachmentCount = static_cast<std::uint32_t>(r.inputs.size());
        native.pInputAttachments = r.inputs.data();
        native.pDepthStencilAttachment = s.depth.attachment == kUnusedAttachment ? nullptr : &r.depth;
        native.preserveAttachmentCount = static_cast<std::uint32_t>(s.preserves.size());
        native.pPreserveAttachments = s.preserves.data();
    }
    std::vector<VkSubpassDependency> dependencies;
    for (const auto& d : desc_.dependencies) {
        if (d.source >= subpasses.size() || d.destination >= subpasses.size() ||
            d.source >= d.destination)
            Log::fatal("VulkanRenderPass", "Invalid subpass dependency");
        const auto before = mapState(d.before);
        auto after = mapState(d.after);
        auto destinationAccess = filterAccess(after.access, d.destinationAccess);
        if (d.destinationStoreState != ResourceState::Undefined) {
            const auto store = mapState(d.destinationStoreState);
            after.stage |= store.stage;
            destinationAccess |= filterAccess(store.access, AccessMode::Write);
        }
        dependencies.push_back({d.source, d.destination, before.stage, after.stage,
                                filterAccess(before.access, d.sourceAccess),
                                destinationAccess,
                                d.byRegion ? VK_DEPENDENCY_BY_REGION_BIT : VkDependencyFlags{0}});
    }
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = static_cast<std::uint32_t>(subpasses.size());
    info.pSubpasses = subpasses.data();
    info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    if (vkCreateRenderPass(device_, &info, nullptr, &handle_) != VK_SUCCESS)
        Log::fatal("VulkanRenderPass", "vkCreateRenderPass failed");
}

VulkanRenderPass::~VulkanRenderPass() { vkDestroyRenderPass(device_, handle_, nullptr); }

VulkanFramebuffer::VulkanFramebuffer(VkDevice device, FramebufferDesc desc, VkRenderPass renderPass,
                                     std::span<const VkImageView> views)
    : device_(device), desc_(std::move(desc)) {
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = desc_.width;
    info.height = desc_.height;
    info.layers = desc_.layers;
    if (vkCreateFramebuffer(device_, &info, nullptr, &handle_) != VK_SUCCESS)
        Log::fatal("VulkanFramebuffer", "vkCreateFramebuffer failed");
}

VulkanFramebuffer::~VulkanFramebuffer() { vkDestroyFramebuffer(device_, handle_, nullptr); }

} // namespace engine::rhi::vulkan

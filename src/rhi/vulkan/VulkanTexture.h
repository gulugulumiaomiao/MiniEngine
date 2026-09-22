#pragma once

#include "rhi/api/Texture.h"
#include "rhi/api/TextureView.h"

#include <vk_mem_alloc.h>

#include <unordered_map>
#include <vector>

namespace engine::rhi::vulkan {

class VulkanDevice;

class VulkanTexture final : public IRHITexture {
public:
    VulkanTexture(VulkanDevice& device,
                  VmaAllocator allocator,
                  const TextureDesc& desc,
                  VkFormat nativeFormat,
                  VkImageUsageFlags nativeUsage);
    VulkanTexture(VulkanDevice& device,
                  VkImage externalImage,
                  const TextureDesc& desc,
                  VkFormat nativeFormat);
    ~VulkanTexture() override;

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) = delete;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    [[nodiscard]] VkImage handle() const { return image_; }
    [[nodiscard]] VkFormat nativeFormat() const { return nativeFormat_; }
    [[nodiscard]] TextureType type() const override { return desc_.dimension; }
    [[nodiscard]] PixelFormat format() const override { return desc_.format; }
    [[nodiscard]] std::uint32_t width() const override { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const override { return desc_.height; }
    [[nodiscard]] std::uint32_t depth() const override { return desc_.depth; }
    [[nodiscard]] std::uint32_t arrayLayers() const override { return desc_.arrayLayers; }
    [[nodiscard]] std::uint32_t mipCount() const override { return desc_.mipCount; }
    [[nodiscard]] RID defaultView() const override { return defaultView_; }
    [[nodiscard]] RID createView(const TextureViewDesc& desc) override;

    [[nodiscard]] bool uploaded() const { return uploaded_; }
    void markUploaded() { uploaded_ = true; }
    void setDefaultView(RID view) { defaultView_ = view; }

    [[nodiscard]] RID findView(const TextureViewDesc& desc) const;
    void cacheView(const TextureViewDesc& desc, RID view);
    void removeView(RID view);
    [[nodiscard]] std::vector<RID> viewHandles() const;

private:
    VulkanDevice* device_{};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkFormat nativeFormat_{VK_FORMAT_UNDEFINED};
    TextureDesc desc_;
    RID defaultView_;
    std::unordered_map<TextureViewDesc, RID, TextureViewDescHash> views_;
    bool uploaded_{};
};

} // namespace engine::rhi::vulkan

#pragma once

#include "rhi/api/Device.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace engine::rhi::vulkan {

class VulkanDevice;

class VulkanSwapchain final : public ISwapchain {
public:
    VulkanSwapchain(VulkanDevice& device, const SwapchainDesc& desc);
    ~VulkanSwapchain() override;

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    [[nodiscard]] FrameStatus beginFrame() override;
    [[nodiscard]] FrameStatus endFrame() override;
    void resize(std::uint32_t width, std::uint32_t height) override;

    [[nodiscard]] RID commandBuffer() override;
    [[nodiscard]] RID currentTexture() const override;
    [[nodiscard]] RID currentTextureView() const override;
    [[nodiscard]] ResourceState currentTextureState() const override;
    [[nodiscard]] PixelFormat format() const override;
    [[nodiscard]] std::uint32_t width() const override { return extent_.width; }
    [[nodiscard]] std::uint32_t height() const override { return extent_.height; }
    [[nodiscard]] std::uint32_t frameIndex() const override { return currentFrame_; }
    [[nodiscard]] std::uint32_t imageCount() const override {
        return static_cast<std::uint32_t>(images_.size());
    }

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    struct Support {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    struct Frame {
        // Registers the pooled VkCommandBuffer in the device's command-buffer pool (owned =
        // false, so the wrapper never frees it); recording goes through this RID.
        RID commandBufferRid{};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        VkFence inFlight{VK_NULL_HANDLE};
    };

    [[nodiscard]] Support querySupport() const;
    void create();
    void destroy();
    void createFrameResources();

    [[nodiscard]] VkDevice device() const;

    VulkanDevice& device_;
    SwapchainDesc desc_;
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<RID> textureHandles_;
    std::vector<RID> textureViewHandles_;
    std::vector<bool> imageInitialized_;
    std::vector<VkSemaphore> renderFinished_;
    std::array<Frame, kFramesInFlight> frames_{};
    // The current frame's command buffer RID (into the device's command-buffer pool).
    RID commandBufferRid_{};
    std::uint32_t currentFrame_{};
    std::uint32_t imageIndex_{};
    bool frameOpen_{};
};

} // namespace engine::rhi::vulkan

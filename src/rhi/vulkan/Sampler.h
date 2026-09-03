#pragma once

#include <vulkan/vulkan.h>

namespace engine {

class Sampler final {
public:
    explicit Sampler(VkDevice device, VkFilter filter = VK_FILTER_LINEAR);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&&) = delete;
    Sampler& operator=(Sampler&&) = delete;

    [[nodiscard]] VkSampler handle() const { return sampler_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkSampler sampler_{VK_NULL_HANDLE};
};

} // namespace engine

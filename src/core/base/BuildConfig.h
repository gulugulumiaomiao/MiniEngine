#pragma once

#include <string_view>

#if defined(MINI_DEBUG) + defined(MINI_RELEASE) + defined(MINI_PUBLISH) != 1
#error "Exactly one of MINI_DEBUG, MINI_RELEASE or MINI_PUBLISH must be defined"
#endif

namespace engine::build {

#if defined(MINI_PUBLISH)
inline constexpr std::string_view kConfiguration = "Publish";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Publish]";
#elif defined(MINI_RELEASE)
inline constexpr std::string_view kConfiguration = "Release";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Release]";
#else
inline constexpr std::string_view kConfiguration = "Debug";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Debug]";
#endif

} // namespace engine::build

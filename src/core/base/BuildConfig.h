#pragma once

#include <string_view>

#if defined(MINI_DEBUG) && defined(MINI_RELEASE)
#error "MINI_DEBUG and MINI_RELEASE cannot both be defined"
#elif !defined(MINI_DEBUG) && !defined(MINI_RELEASE)
#error "The build must define either MINI_DEBUG or MINI_RELEASE"
#endif

namespace engine::build {

#if defined(MINI_DEBUG)
inline constexpr std::string_view kConfiguration = "Debug";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Debug]";
#else
inline constexpr std::string_view kConfiguration = "Release";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Release]";
#endif

} // namespace engine::build

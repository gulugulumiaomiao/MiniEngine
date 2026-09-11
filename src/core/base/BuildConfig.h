#pragma once

#include <string_view>

#if defined(MINI_DEBUG) && defined(MINI_RELEASE)
#error "MINI_DEBUG and MINI_RELEASE cannot both be defined"
#elif !defined(MINI_DEBUG) && !defined(MINI_RELEASE)
#error "The build must define either MINI_DEBUG or MINI_RELEASE"
#endif

namespace engine::build {

// MINI_DEBUG covers the Debug and Release configurations; Publish defines
// MINI_RELEASE instead. NDEBUG separates the optimized build from Debug.
#if defined(MINI_RELEASE)
inline constexpr std::string_view kConfiguration = "Publish";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Publish]";
#elif defined(NDEBUG)
inline constexpr std::string_view kConfiguration = "Release";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Release]";
#else
inline constexpr std::string_view kConfiguration = "Debug";
inline constexpr std::string_view kWindowTitle = "Mini Vulkan Engine [Debug]";
#endif

} // namespace engine::build

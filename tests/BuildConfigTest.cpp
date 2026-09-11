#include "core/base/BuildConfig.h"

#include <cstdio>
#include <string_view>

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main() {
#if defined(MINI_DEBUG)
    if (engine::build::kConfiguration != "Debug")
        return fail("Debug build must report Debug configuration");
    if (engine::build::kWindowTitle != "Mini Vulkan Engine [Debug]")
        return fail("Debug build must report Debug window title");
#elif defined(MINI_RELEASE)
    if (engine::build::kConfiguration != "Release")
        return fail("Release build must report Release configuration");
    if (engine::build::kWindowTitle != "Mini Vulkan Engine [Release]")
        return fail("Release build must report Release window title");
#elif defined(MINI_PUBLISH)
    if (engine::build::kConfiguration != "Publish")
        return fail("Publish build must report Publish configuration");
    if (engine::build::kWindowTitle != "Mini Vulkan Engine [Publish]")
        return fail("Publish build must report Publish window title");
#else
#error "Exactly one of MINI_DEBUG, MINI_RELEASE or MINI_PUBLISH must be defined"
#endif
    return 0;
}

#include "core/base/BuildConfig.h"

#include <cstdio>
#include <string_view>

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

// Counterpart to BuildConfigTest: this target links MiniEngineEditor, so MINI_EDITOR
// must be visible here. Together the two tests pin down the dual library wiring, which
// is what keeps the editor-only engine code out of the shipped game.
int main() {
#if !defined(MINI_EDITOR)
#error "BuildConfigEditorTest must be built against the MiniEngineEditor variant"
#endif
    if (!engine::build::kEditor)
        return fail("MiniEngineEditor must report an editor build");

    // The editor macro is orthogonal to the configuration macros.
#if defined(MINI_DEBUG)
    if (engine::build::kConfiguration != "Debug")
        return fail("Debug build must report Debug configuration");
#elif defined(MINI_RELEASE)
    if (engine::build::kConfiguration != "Release")
        return fail("Release build must report Release configuration");
#elif defined(MINI_PUBLISH)
    if (engine::build::kConfiguration != "Publish")
        return fail("Publish build must report Publish configuration");
#else
#error "Exactly one of MINI_DEBUG, MINI_RELEASE or MINI_PUBLISH must be defined"
#endif
    return 0;
}

#pragma once

#include "core/filesystem/FileSystem.h"
#include "asset/manager/AssetManager.h"

#include <filesystem>

namespace engine::test {

inline void shutdownAssetEnvironment() {
    ASSET_MANAGER.shutdown();
    (void)FILE_SYSTEM.unmount("assets");
    (void)FILE_SYSTEM.unmount("library");
}

[[nodiscard]] inline bool
initializeAssetEnvironment(const std::filesystem::path& assetRoot,
                           bool assetReadOnly = false,
                           AssetManagerMode mode = AssetManagerMode::Development) {
    shutdownAssetEnvironment();
    const std::filesystem::path normalizedRoot =
        std::filesystem::absolute(assetRoot).lexically_normal();
    // Only the asset scheme exists: the engine's built-in content (Error Material,
    // built-in Shader) ships inside the mounted asset root, exactly as it does inside a
    // project's assets/ after the built-in content copy.
    if (!FILE_SYSTEM.mountDirectory("assets", normalizedRoot, assetReadOnly) ||
        !FILE_SYSTEM.mountDirectory("library", normalizedRoot.parent_path() / "library", false)) {
        shutdownAssetEnvironment();
        return false;
    }
    if (!ASSET_MANAGER.initialize(mode)) {
        shutdownAssetEnvironment();
        return false;
    }
    return true;
}

} // namespace engine::test

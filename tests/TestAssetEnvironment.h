#pragma once

#include "core/filesystem/FileSystem.h"
#include "asset/manager/AssetManager.h"

#include <filesystem>

namespace engine::test {

inline void shutdownAssetEnvironment() {
    ASSET_MANAGER.shutdown();
    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
}

[[nodiscard]] inline bool
initializeAssetEnvironment(const std::filesystem::path& assetRoot,
                           bool assetReadOnly = false,
                           AssetManagerMode mode = AssetManagerMode::Development) {
    shutdownAssetEnvironment();
    const std::filesystem::path normalizedRoot =
        std::filesystem::absolute(assetRoot).lexically_normal();
    if (!FILE_SYSTEM.mountDirectory("asset", normalizedRoot, assetReadOnly) ||
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

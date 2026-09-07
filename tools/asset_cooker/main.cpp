#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"

#include <filesystem>

int main(int argc, char** argv) {
    using namespace engine;
    if (argc != 3) {
        Log::error("MiniAssetCooker", "Usage: MiniAssetCooker <asset-root> <library-root>");
        return 1;
    }
    const std::filesystem::path assetRoot = std::filesystem::absolute(argv[1]).lexically_normal();
    const std::filesystem::path libraryRoot = std::filesystem::absolute(argv[2]).lexically_normal();
    if (!FILE_SYSTEM.mountDirectory("asset", assetRoot, false) ||
        !FILE_SYSTEM.mountDirectory("library", libraryRoot, false) ||
        !ASSET_IMPORT_PIPELINE.initialize()) {
        Log::error("MiniAssetCooker", "Cannot initialize asset import");
        return 2;
    }
    const bool success = ASSET_IMPORT_PIPELINE.scanAll();
    ASSET_IMPORT_PIPELINE.shutdown();
    ASSET_DATABASE.shutdown();
    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    return success ? 0 : 3;
}

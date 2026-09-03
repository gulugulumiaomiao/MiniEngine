#pragma once

#include "asset/AssetMeta.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct AssetImportContext {
    AssetMeta meta;
    VirtualPath sourcePath;
    VirtualPath metaPath;
    VirtualPath artifactPath;
    std::vector<VirtualPath> includePaths;
};

struct AssetImportResult {
    bool success{};
    AssetType type{AssetType::Unknown};
    VirtualPath artifactPath;
    std::vector<VirtualPath> dependencies;
    std::string error;

    [[nodiscard]] static AssetImportResult failed(AssetType type,
                                                  std::string error);
    [[nodiscard]] static AssetImportResult succeeded(
        AssetType type, VirtualPath artifactPath,
        std::vector<VirtualPath> dependencies = {});
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    [[nodiscard]] virtual AssetType assetType() const = 0;
    [[nodiscard]] virtual std::uint32_t version() const = 0;
    [[nodiscard]] virtual AssetImportResult import(
        const AssetImportContext& context) const = 0;
};

} // namespace engine

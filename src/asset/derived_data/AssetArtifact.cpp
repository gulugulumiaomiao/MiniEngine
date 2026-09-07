#include "asset/derived_data/AssetArtifact.h"

#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"

namespace engine {
namespace {

constexpr std::uint32_t kMagic = 0x5452414dU;
constexpr std::uint16_t kContainerVersion = 2;

} // namespace

std::vector<std::byte> serializeAssetArtifact(const AssetArtifact& artifact) {
    BinaryWriter writer;
    std::uint32_t magic = kMagic;
    std::uint16_t containerVersion = kContainerVersion;
    std::uint16_t type = static_cast<std::uint16_t>(artifact.assetType);
    std::uint32_t assetVersion = artifact.version;
    std::uint64_t idHigh = artifact.assetId.high();
    std::uint64_t idLow = artifact.assetId.low();
    std::string sourcePath = artifact.sourcePath.string();
    std::vector<std::byte> payload = artifact.payload;
    if (!writer.transfer("magic", magic) ||
        !writer.transfer("container_version", containerVersion) ||
        !writer.transfer("asset_type", type) || !writer.transfer("asset_version", assetVersion) ||
        !writer.transfer("id_high", idHigh) || !writer.transfer("id_low", idLow) ||
        !writer.transfer("source_path", sourcePath) || !writer.transfer("payload", payload))
        return {};
    return writer.takeBytes();
}

std::optional<AssetArtifact> parseAssetArtifact(const VirtualPath& artifactPath,
                                                std::span<const std::byte> source) {
    BinaryReader reader{source};
    std::uint32_t magic{};
    std::uint16_t containerVersion{};
    std::uint16_t encodedType{};
    std::uint32_t assetVersion{};
    std::uint64_t idHigh{};
    std::uint64_t idLow{};
    std::string sourcePath;
    std::vector<std::byte> payload;
    if (!reader.transfer("magic", magic) || magic != kMagic ||
        !reader.transfer("container_version", containerVersion) ||
        containerVersion != kContainerVersion || !reader.transfer("asset_type", encodedType) ||
        !reader.transfer("asset_version", assetVersion) || !reader.transfer("id_high", idHigh) ||
        !reader.transfer("id_low", idLow) || !reader.transfer("source_path", sourcePath) ||
        !reader.transfer("payload", payload)) {
        Log::error(
            "AssetArtifact", "Invalid binary Artifact header: %s", artifactPath.string().c_str());
        return std::nullopt;
    }
    const AssetType assetType = static_cast<AssetType>(encodedType);
    const AssetId assetId{idHigh, idLow};
    VirtualPath parsedSource{sourcePath};
    if (!assetId.valid() ||
        (assetType != AssetType::Shader && assetType != AssetType::Material &&
         assetType != AssetType::Mesh && assetType != AssetType::Scene) ||
        !parsedSource.valid() || !reader.finished()) {
        Log::error(
            "AssetArtifact", "Invalid binary Artifact contents: %s", artifactPath.string().c_str());
        return std::nullopt;
    }
    return AssetArtifact{
        assetVersion, assetId, assetType, std::move(parsedSource), std::move(payload)};
}

bool saveAssetArtifact(const VirtualPath& path, const AssetArtifact& artifact) {
    if (!path.valid() || !artifact.assetId.valid() || artifact.assetType == AssetType::Unknown ||
        !artifact.sourcePath.valid()) {
        Log::error("AssetArtifact", "Cannot save invalid Artifact: %s", path.string().c_str());
        return false;
    }
    const std::vector<std::byte> content = serializeAssetArtifact(artifact);
    return FILE_SYSTEM.writeBinaryAtomic(path, content);
}

std::optional<AssetArtifact> loadAssetArtifact(const VirtualPath& path) {
    const auto source = FILE_SYSTEM.readBinary(path);
    return source ? parseAssetArtifact(path, *source) : std::nullopt;
}

} // namespace engine

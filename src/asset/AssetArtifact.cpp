#include "asset/AssetArtifact.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"

#include <nlohmann/json.hpp>

namespace engine {
namespace {

using Json = nlohmann::json;

} // namespace

std::string serializeAssetArtifact(const AssetArtifact& artifact) {
    Json payload = Json::parse(artifact.payload, nullptr, false);
    if (payload.is_discarded()) {
        payload = artifact.payload;
    }
    return Json{{"version", artifact.version},
                {"asset_id", artifact.assetId.toString()},
                {"asset_type", assetTypeName(artifact.assetType)},
                {"source_path", artifact.sourcePath.string()},
                {"payload", std::move(payload)}}
               .dump(2) +
           '\n';
}

std::optional<AssetArtifact>
parseAssetArtifact(const VirtualPath& artifactPath, std::string_view source) {
    const Json root = Json::parse(source, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        Log::error("AssetArtifact", "Invalid Artifact JSON: %s",
                   artifactPath.string().c_str());
        return std::nullopt;
    }
    const auto version = root.find("version");
    const auto id = root.find("asset_id");
    const auto type = root.find("asset_type");
    const auto sourcePath = root.find("source_path");
    const auto payload = root.find("payload");
    if (version == root.end() || !version->is_number_unsigned() ||
        id == root.end() || !id->is_string() || type == root.end() ||
        !type->is_string() || sourcePath == root.end() ||
        !sourcePath->is_string() || payload == root.end()) {
        Log::error("AssetArtifact", "Artifact fields are invalid: %s",
                   artifactPath.string().c_str());
        return std::nullopt;
    }
    const auto parsedId = AssetId::parse(id->get_ref<const std::string&>());
    const AssetType parsedType =
        assetTypeFromName(type->get_ref<const std::string&>());
    VirtualPath parsedSource{sourcePath->get_ref<const std::string&>()};
    if (!parsedId || parsedType == AssetType::Unknown || !parsedSource.valid()) {
        Log::error("AssetArtifact", "Artifact identity is invalid: %s",
                   artifactPath.string().c_str());
        return std::nullopt;
    }
    return AssetArtifact{version->get<std::uint32_t>(), *parsedId, parsedType,
                         std::move(parsedSource), payload->dump()};
}

bool saveAssetArtifact(const VirtualPath& path,
                       const AssetArtifact& artifact) {
    if (!path.valid() || !artifact.assetId.valid() ||
        artifact.assetType == AssetType::Unknown ||
        !artifact.sourcePath.valid()) {
        Log::error("AssetArtifact", "Cannot save invalid Artifact: %s",
                   path.string().c_str());
        return false;
    }
    const std::string content = serializeAssetArtifact(artifact);
    return FILE_SYSTEM.writeTextAtomic(path, content);
}

std::optional<AssetArtifact> loadAssetArtifact(const VirtualPath& path) {
    const auto source = FILE_SYSTEM.readText(path);
    return source ? parseAssetArtifact(path, *source) : std::nullopt;
}

} // namespace engine

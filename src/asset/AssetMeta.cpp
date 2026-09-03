#include "asset/AssetMeta.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"

#include <nlohmann/json.hpp>

namespace engine {
namespace {

using Json = nlohmann::json;

} // namespace

AssetType inferAssetType(const VirtualPath& sourcePath) {
    const std::string& path = sourcePath.relativePath();
    if (path.ends_with(".shader.json")) {
        return AssetType::Shader;
    }
    if (path.ends_with(".material.json")) {
        return AssetType::Material;
    }
    return AssetType::Unknown;
}

VirtualPath assetMetaPath(const VirtualPath& sourcePath) {
    return sourcePath.valid()
               ? VirtualPath{sourcePath.string() + ".meta"}
               : VirtualPath{};
}

std::optional<AssetMeta> parseAssetMeta(const VirtualPath& metaPath,
                                        std::string_view source) {
    const Json root = Json::parse(source, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        Log::error("AssetMeta", "Invalid Meta JSON: %s",
                   metaPath.string().c_str());
        return std::nullopt;
    }
    const auto version = root.find("version");
    const auto id = root.find("asset_id");
    const auto type = root.find("asset_type");
    if (version == root.end() || !version->is_number_unsigned() ||
        id == root.end() || !id->is_string() || type == root.end() ||
        !type->is_string()) {
        Log::error("AssetMeta", "Meta fields are missing or invalid: %s",
                   metaPath.string().c_str());
        return std::nullopt;
    }

    const auto parsedId = AssetId::parse(id->get_ref<const std::string&>());
    const AssetType parsedType =
        assetTypeFromName(type->get_ref<const std::string&>());
    if (!parsedId || parsedType == AssetType::Unknown) {
        Log::error("AssetMeta", "Meta identity is invalid: %s",
                   metaPath.string().c_str());
        return std::nullopt;
    }

    return AssetMeta{version->get<std::uint32_t>(), *parsedId, parsedType};
}

std::string serializeAssetMeta(const AssetMeta& meta) {
    const Json root{{"version", meta.version},
                    {"asset_id", meta.assetId.toString()},
                    {"asset_type", assetTypeName(meta.assetType)}};
    return root.dump(2) + '\n';
}

std::optional<AssetMeta> loadAssetMeta(const VirtualPath& metaPath) {
    const auto source = FILE_SYSTEM.readText(metaPath);
    return source ? parseAssetMeta(metaPath, *source) : std::nullopt;
}

bool saveAssetMeta(const VirtualPath& metaPath, const AssetMeta& meta) {
    if (!metaPath.valid() || !meta.assetId.valid() ||
        meta.assetType == AssetType::Unknown) {
        Log::error("AssetMeta", "Cannot save invalid Meta: %s",
                   metaPath.string().c_str());
        return false;
    }
    const std::string content = serializeAssetMeta(meta);
    return FILE_SYSTEM.writeTextAtomic(metaPath, content);
}

std::optional<AssetMeta> createAssetMeta(const VirtualPath& sourcePath) {
    const AssetType type = inferAssetType(sourcePath);
    if (!sourcePath.valid() || type == AssetType::Unknown) {
        Log::error("AssetMeta", "Cannot infer asset type: %s",
                   sourcePath.string().c_str());
        return std::nullopt;
    }
    AssetMeta meta{1, AssetId::generate(), type};
    if (!saveAssetMeta(assetMetaPath(sourcePath), meta)) {
        return std::nullopt;
    }
    return meta;
}

} // namespace engine

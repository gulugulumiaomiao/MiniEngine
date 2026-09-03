#pragma once

#include "asset/AssetId.h"
#include "core/io/VirtualPath.h"

#include <utility>

namespace engine {

enum class AssetType {
    Unknown,
    Shader,
    Material,
};

[[nodiscard]] constexpr const char* assetTypeName(AssetType type) {
    switch (type) {
    case AssetType::Shader:
        return "Shader";
    case AssetType::Material:
        return "Material";
    default:
        return "Unknown";
    }
}

[[nodiscard]] AssetType assetTypeFromName(std::string_view name);

class Asset {
public:
    virtual ~Asset() = default;

    [[nodiscard]] const AssetId& assetId() const { return assetId_; }
    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] virtual AssetType type() const = 0;

    void setAssetIdentity(AssetId id, VirtualPath path) {
        assetId_ = id;
        assetPath_ = std::move(path);
    }
    void setAssetPath(VirtualPath path) { assetPath_ = std::move(path); }

protected:
    Asset() = default;
    Asset(AssetId id, VirtualPath path)
        : assetId_(id), assetPath_(std::move(path)) {}

private:
    AssetId assetId_;
    VirtualPath assetPath_;
};

} // namespace engine

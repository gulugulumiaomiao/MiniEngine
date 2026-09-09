#pragma once

#include "asset/base/AssetId.h"
#include "core/serialization/Transferable.h"
#include "core/filesystem/VirtualPath.h"

#include <utility>

namespace engine {

enum class AssetType {
    Unknown,
    Shader,
    Material,
    Mesh,
    Scene,
    Texture,
};

[[nodiscard]] constexpr const char* assetTypeName(AssetType type) {
    switch (type) {
    case AssetType::Shader: return "Shader";
    case AssetType::Material: return "Material";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Texture: return "Texture";
    case AssetType::Scene: return "Scene";
    default: return "Unknown";
    }
}

[[nodiscard]] AssetType assetTypeFromName(std::string_view name);

class Asset : public Transferable {
public:
    ~Asset() override = default;

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
    Asset(AssetId id, VirtualPath path) : assetId_(id), assetPath_(std::move(path)) {}

private:
    AssetId assetId_;
    VirtualPath assetPath_;
};

} // namespace engine

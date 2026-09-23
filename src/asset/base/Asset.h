#pragma once

#include "asset/base/AssetId.h"
#include "core/base/RefCounted.h"
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
    // ScriptedImporter/DefaultImporter 产出的免转换资产（源字节透传）。
    Generic,
};

[[nodiscard]] constexpr const char* assetTypeName(AssetType type) {
    switch (type) {
    case AssetType::Shader: return "Shader";
    case AssetType::Material: return "Material";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Texture: return "Texture";
    case AssetType::Scene: return "Scene";
    case AssetType::Generic: return "Generic";
    default: return "Unknown";
    }
}

[[nodiscard]] AssetType assetTypeFromName(std::string_view name);

// Serialized asset definition (artifact payload). Owned through Ref<Asset> via intrusive
// RefCounted; the AssetManager cache holds a strong Ref, so cached assets are shared and
// immutable for their whole resident lifetime (consumers must not mutate or move out of
// them). Non-copyable/non-movable by virtue of RefCounted.
class Asset : public Transferable, public RefCounted {
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

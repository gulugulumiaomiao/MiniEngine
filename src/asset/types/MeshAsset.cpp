#include "asset/types/MeshAsset.h"
#include "asset/runtime/MeshAssetConversion.h"
#include "core/serialization/Transfer.h"
namespace engine {
std::optional<MeshCreateInfo> MeshAsset::buildRuntimeInfo() const {
    auto info = toRuntimeMesh(data.desc);
    info.data = toRuntimeMesh(data.meshData);
    if (!validateMesh(info))
        return {};
    return info;
}
Ref<Mesh> MeshAsset::instantiate() {
    if (!instance_ || !instance_->isValid()) {
        auto info = buildRuntimeInfo();
        if (!info)
            return {};
        if (instance_) {
            if (!instance_->rebuild(std::move(*info)))
                return {};
        } else
            instance_ = Mesh::create(std::move(*info));
    }
    return instance_;
}
Ref<Mesh> MeshAsset::clone() const {
    auto info = buildRuntimeInfo();
    return info ? Mesh::create(std::move(*info)) : Ref<Mesh>{};
}
bool MeshAsset::transfer(Transfer& archive) {
    if (archive.writing())
        return buildRuntimeInfo().has_value() && data.transfer(archive);
    MeshAsset decoded;
    if (!decoded.data.transfer(archive))
        return false;
    auto info = decoded.buildRuntimeInfo();
    if (!info || (instance_ && !instance_->rebuild(std::move(*info))))
        return false;
    data = std::move(decoded.data);
    return true;
}
} // namespace engine

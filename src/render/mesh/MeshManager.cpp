#include "render/mesh/MeshManager.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <limits>
#include <utility>

namespace engine {

MeshHandle MeshManager::load(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return {};
    }
    if (const MeshHandle existing = handleFor(meshPath); existing) {
        return existing;
    }
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    return asset ? insert(asset->instantiate()) : MeshHandle{};
}

bool MeshManager::replace(MeshHandle handle, Mesh mesh) {
    Mesh* current = find(handle);
    if (!current) {
        Log::error("MeshManager", "Cannot replace an invalid MeshHandle");
        return false;
    }
    if (current->assetPath() != mesh.assetPath() || !validate(mesh)) {
        Log::error(
            "MeshManager", "Replacement Mesh is invalid: %s", mesh.assetPath().string().c_str());
        return false;
    }
    if (current->version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::error("MeshManager", "Mesh version overflow");
        return false;
    }
    mesh.version_ = current->version_ + 1;
    mesh.dirty_ = true;
    *current = std::move(mesh);
    return true;
}

bool MeshManager::replace(const VirtualPath& meshPath) {
    const MeshHandle handle = handleFor(meshPath);
    if (!handle)
        return true;
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    return asset && replace(handle, asset->instantiate());
}

bool MeshManager::validate(const Mesh& mesh) const {
    return validateMesh(mesh.desc(), mesh.data());
}

} // namespace engine

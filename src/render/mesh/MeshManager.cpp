#include "render/mesh/MeshManager.h"

#include "core/logging/Log.h"
#include "render/mesh/MeshBuilder.h"

#include <limits>
#include <utility>

namespace engine {

MeshHandle MeshManager::createRuntime(const MeshBuildRecipe& recipe) {
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshManager", "Cannot build runtime primitive Mesh: %s", recipe.name.c_str());
        return {};
    }
    return insert(Mesh{built->desc, built->data, recipe});
}

bool MeshManager::rebuildRuntime(MeshHandle handle, const MeshBuildRecipe& recipe) {
    const Mesh* current = find(handle);
    if (!current || current->assetPath().valid()) {
        Log::error("MeshManager", "Cannot rebuild a non-runtime MeshHandle");
        return false;
    }
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshManager", "Cannot rebuild runtime primitive Mesh: %s", recipe.name.c_str());
        return false;
    }
    return replace(handle, Mesh{built->desc, built->data, recipe});
}

bool MeshManager::destroyRuntime(MeshHandle handle) {
    const Mesh* mesh = find(handle);
    if (!mesh || mesh->assetPath().valid()) {
        Log::error("MeshManager", "Cannot destroy a non-runtime MeshHandle");
        return false;
    }
    return destroy(handle);
}

MeshHandle MeshManager::insert(Mesh mesh) {
    if (!validate(mesh))
        return {};

    const VirtualPath assetPath = mesh.assetPath();
    if (assetPath.valid()) {
        if (const MeshHandle existing = findHandle(assetPath); existing)
            return existing;
    }
    const MeshHandle handle = meshes_.insert(std::move(mesh));
    if (assetPath.valid())
        assetIndex_.insert_or_assign(assetPath, handle);
    return handle;
}

Mesh* MeshManager::find(const VirtualPath& meshPath) {
    return const_cast<Mesh*>(std::as_const(*this).find(meshPath));
}

const Mesh* MeshManager::find(const VirtualPath& meshPath) const {
    return meshes_.find(findHandle(meshPath));
}

MeshHandle MeshManager::findHandle(const VirtualPath& meshPath) const {
    const auto found = assetIndex_.find(meshPath);
    if (found == assetIndex_.end() || !meshes_.find(found->second))
        return {};
    return found->second;
}

bool MeshManager::destroy(MeshHandle handle) {
    Mesh* mesh = find(handle);
    if (!mesh)
        return false;
    if (mesh->assetPath().valid()) {
        const auto indexed = assetIndex_.find(mesh->assetPath());
        if (indexed != assetIndex_.end() && indexed->second == handle)
            assetIndex_.erase(indexed);
    }
    if (!meshes_.release(handle))
        return false;
    if (destroyObserver_)
        destroyObserver_(handle);
    return true;
}

void MeshManager::clear() {
    if (destroyObserver_) {
        meshes_.forEachHandle([this](MeshHandle handle, const Mesh&) { destroyObserver_(handle); });
    }
    assetIndex_.clear();
    meshes_.clear();
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

bool MeshManager::validate(const Mesh& mesh) {
    return validateMesh(mesh.desc(), mesh.data());
}

} // namespace engine

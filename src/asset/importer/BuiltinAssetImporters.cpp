#include "asset/importer/BuiltinAssetImporters.h"

#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/MaterialAssetImporter.h"
#include "asset/importer/MeshAssetImporter.h"
#include "asset/importer/SceneAssetImporter.h"
#include "asset/importer/ShaderAssetImporter.h"

#include <memory>

namespace engine {

bool registerBuiltinAssetImporters(AssetImporterRegistry& registry) {
    if (!registry.registerImporter(std::make_unique<ShaderAssetImporter>())) {
        return false;
    }
    if (!registry.registerImporter(std::make_unique<MaterialAssetImporter>())) {
        return false;
    }
    if (!registry.registerImporter(std::make_unique<MeshAssetImporter>())) {
        return false;
    }
    return registry.registerImporter(std::make_unique<SceneAssetImporter>());
}

} // namespace engine

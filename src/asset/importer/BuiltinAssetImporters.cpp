#include "asset/importer/BuiltinAssetImporters.h"

#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/MaterialAssetImporter.h"
#include "asset/importer/ShaderAssetImporter.h"

#include <memory>

namespace engine {

bool registerBuiltinAssetImporters(AssetImporterRegistry& registry) {
    if (!registry.registerImporter(std::make_unique<ShaderAssetImporter>())) {
        return false;
    }
    return registry.registerImporter(std::make_unique<MaterialAssetImporter>());
}

} // namespace engine

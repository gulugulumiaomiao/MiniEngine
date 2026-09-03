#pragma once

namespace engine {

class AssetImporterRegistry;

[[nodiscard]] bool registerBuiltinAssetImporters(
    AssetImporterRegistry& registry);

} // namespace engine

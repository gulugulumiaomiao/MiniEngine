#include "asset/importer/ShaderAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderIncludeResolver.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

struct DependencyCollector {
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;
    std::vector<VirtualPath> dependencies;
    std::string error;

    [[nodiscard]] bool collect(const VirtualPath& path) {
        const std::string key = path.string();
        if (visited.contains(key)) {
            return true;
        }
        if (!visiting.insert(key).second) {
            error = "Cyclic include dependency: " + key;
            return false;
        }
        const auto source = FILE_SYSTEM.readText(path);
        if (!source) {
            visiting.erase(key);
            error = "Cannot read shader dependency: " + key;
            return false;
        }

        dependencies.push_back(path);
        std::string_view remaining{*source};
        while (!remaining.empty()) {
            const std::size_t lineEnd = remaining.find('\n');
            const std::string_view line = remaining.substr(0, lineEnd);
            remaining = lineEnd == std::string_view::npos
                            ? std::string_view{}
                            : remaining.substr(lineEnd + 1);
            const std::size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos ||
                line.compare(first, 8, "#include") != 0) {
                continue;
            }
            const std::size_t quote = line.find('"', first + 8);
            const std::size_t endQuote =
                quote == std::string::npos ? std::string::npos
                                           : line.find('"', quote + 1);
            if (quote == std::string::npos || endQuote == std::string::npos) {
                visiting.erase(key);
                error = "Malformed include in: " + key;
                return false;
            }
            const std::string include{
                line.substr(quote + 1, endQuote - quote - 1)};
            const auto resolved = ShaderIncludeResolver::resolve(path, include);
            if (!resolved) {
                visiting.erase(key);
                error = "Cannot resolve include " + include + " from " + key;
                return false;
            }
            if (!collect(*resolved)) {
                visiting.erase(key);
                return false;
            }
        }
        visiting.erase(key);
        visited.insert(key);
        return true;
    }
};

[[nodiscard]] AssetImportResult failImport(std::string error) {
    Log::error("ShaderAssetImporter", "%s", error.c_str());
    return AssetImportResult::failed(AssetType::Shader, std::move(error));
}

} // namespace

AssetImportResult ShaderAssetImporter::import(
    const AssetImportContext& context) const {
    if (context.meta.assetType != AssetType::Shader ||
        !context.meta.assetId.valid() || !context.sourcePath.valid() ||
        !context.artifactPath.valid()) {
        return failImport("Invalid Shader import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return failImport("Cannot read ShaderAsset: " +
                          context.sourcePath.string());
    }
    const std::shared_ptr<ShaderAsset> shader =
        detail::parseShaderAsset(context.sourcePath, *source);
    if (!shader) {
        return failImport("Cannot parse ShaderAsset: " +
                          context.sourcePath.string());
    }

    DependencyCollector collector;
    for (const SubShaderDesc& subShader : shader->subShaders) {
        for (const ShaderPassAsset& pass : subShader.passes) {
            if (!collector.collect(pass.pass.program.vertexSource) ||
                !collector.collect(pass.pass.program.fragmentSource)) {
                return failImport(std::move(collector.error));
            }
        }
    }

    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return failImport("Cannot create Shader Artifact directory: " +
                          context.artifactPath.parent().string());
    }
    BinaryWriter writer;
    if (!shader->transfer(writer)) {
        return failImport("Cannot serialize Shader Artifact: " +
                          context.sourcePath.string());
    }
    const AssetArtifact artifact{1, context.meta.assetId, AssetType::Shader,
                                 context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return failImport("Cannot save Shader Artifact: " +
                          context.artifactPath.string());
    }
    return AssetImportResult::succeeded(
        AssetType::Shader, context.artifactPath,
        std::move(collector.dependencies));
}

} // namespace engine

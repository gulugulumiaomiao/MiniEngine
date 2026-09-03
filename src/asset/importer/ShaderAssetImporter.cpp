#include "asset/importer/ShaderAssetImporter.h"

#include "asset/AssetArtifact.h"
#include "core/Log.h"
#include "core/io/FileSystem.h"
#include "renderer/Shader.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

struct DependencyCollector {
    explicit DependencyCollector(std::span<const VirtualPath> paths)
        : includePaths(paths) {}

    std::span<const VirtualPath> includePaths;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;
    std::vector<VirtualPath> dependencies;
    std::string error;

    [[nodiscard]] std::optional<VirtualPath> resolveInclude(
        const VirtualPath& includingFile, std::string_view include) const {
        VirtualPath candidate = includingFile.parent().joined(include);
        if (FILE_SYSTEM.isFile(candidate)) {
            return candidate;
        }
        for (const VirtualPath& root : includePaths) {
            candidate = root.joined(include);
            if (FILE_SYSTEM.isFile(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

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
        std::istringstream lines{*source};
        std::string line;
        while (std::getline(lines, line)) {
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
            const std::string include =
                line.substr(quote + 1, endQuote - quote - 1);
            const auto resolved = resolveInclude(path, include);
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

    DependencyCollector collector{context.includePaths};
    for (const SubShaderDesc& subShader : shader->subShaders) {
        for (const ShaderPassAsset& pass : subShader.passes) {
            if (!collector.collect(pass.pass.program.vertexSource) ||
                !collector.collect(pass.pass.program.fragmentSource)) {
                return failImport(std::move(collector.error));
            }
        }
    }

    const nlohmann::json root = nlohmann::json::parse(*source, nullptr, false);
    if (root.is_discarded()) {
        return failImport("Cannot canonicalize ShaderAsset JSON: " +
                          context.sourcePath.string());
    }
    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return failImport("Cannot create Shader Artifact directory: " +
                          context.artifactPath.parent().string());
    }
    const AssetArtifact artifact{1, context.meta.assetId, AssetType::Shader,
                                 context.sourcePath, root.dump()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return failImport("Cannot save Shader Artifact: " +
                          context.artifactPath.string());
    }
    return AssetImportResult::succeeded(
        AssetType::Shader, context.artifactPath,
        std::move(collector.dependencies));
}

} // namespace engine

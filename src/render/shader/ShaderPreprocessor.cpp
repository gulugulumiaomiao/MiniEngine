#include "render/shader/ShaderPreprocessor.h"

#include "core/filesystem/FileSystem.h"
#include "core/hash.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <sstream>
#include <tuple>
#include <utility>

namespace engine {

bool ShaderPreprocessor::containsPath(std::span<const VirtualPath> paths,
                                      const VirtualPath& candidate) {
    return std::ranges::any_of(paths,
                               [&candidate](const VirtualPath& path) { return path == candidate; });
}

std::optional<VirtualPath>
ShaderPreprocessor::resolveInclude(const VirtualPath& includingFile,
                                   std::string_view include,
                                   bool local,
                                   std::span<const VirtualPath> searchPaths,
                                   std::vector<VirtualPath>& attempted) {
    if (include.find("://") != std::string_view::npos) {
        VirtualPath path{include};
        attempted.push_back(path);
        return path.valid() && FILE_SYSTEM.isFile(path) ? std::optional<VirtualPath>{path}
                                                        : std::nullopt;
    }
    if (local) {
        const VirtualPath path = includingFile.parent().joined(include);
        attempted.push_back(path);
        if (path.valid() && FILE_SYSTEM.isFile(path))
            return path;
    }
    for (const VirtualPath& root : searchPaths) {
        const VirtualPath path = root.joined(include);
        attempted.push_back(path);
        if (path.valid() && FILE_SYSTEM.isFile(path))
            return path;
    }
    return std::nullopt;
}

bool ShaderPreprocessor::preprocessSource(const VirtualPath& path,
                                          std::string_view content,
                                          std::span<const VirtualPath> searchPaths,
                                          std::unordered_set<std::string>& visiting,
                                          PreprocessedShader& result) {
    const std::string key = path.string();
    if (!visiting.insert(key).second) {
        Log::error("ShaderPreprocessor", "Cyclic include dependency: %s", key.c_str());
        return false;
    }
    if (!containsPath(result.dependencies, path))
        result.dependencies.push_back(path);

    std::istringstream lines{std::string{content}};
    std::string line;
    std::uint32_t lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 8, "#include") != 0) {
            result.source += line + '\n';
            continue;
        }
        const std::size_t delimiter = line.find_first_of("\"<", first + 8);
        if (delimiter == std::string::npos) {
            Log::error("ShaderPreprocessor", "Malformed include in: %s", key.c_str());
            return false;
        }
        const bool local = line[delimiter] == '"';
        const char closing = local ? '"' : '>';
        const std::size_t end = line.find(closing, delimiter + 1);
        if (end == std::string::npos) {
            Log::error("ShaderPreprocessor", "Malformed include in: %s", key.c_str());
            return false;
        }
        const std::string include = line.substr(delimiter + 1, end - delimiter - 1);
        std::vector<VirtualPath> attempted;
        const auto resolved = resolveInclude(path, include, local, searchPaths, attempted);
        if (!resolved) {
            std::string searched;
            for (const VirtualPath& candidate : attempted)
                searched += "\n  " + candidate.string();
            Log::error("ShaderPreprocessor",
                       "Cannot resolve include %s from %s. Searched:%s",
                       include.c_str(),
                       key.c_str(),
                       searched.c_str());
            return false;
        }
        const auto included = FILE_SYSTEM.readText(*resolved);
        if (!included) {
            Log::error("ShaderPreprocessor", "Cannot read include: %s", resolved->string().c_str());
            return false;
        }
        result.source += "#line 1 \"" + resolved->string() + "\"\n";
        if (!preprocessSource(*resolved, *included, searchPaths, visiting, result))
            return false;
        result.source += "#line " + std::to_string(lineNumber + 1) + " \"" + path.string() + "\"\n";
    }
    visiting.erase(key);
    return true;
}

ShaderPreprocessor::ShaderPreprocessor(ShaderPreprocessorConfig config,
                                       FileDependencyGraph& dependencies)
    : config_(std::move(config)), dependencies_(dependencies) {
    for (const VirtualPath& root : config_.includeSearchPaths) {
        if (!root.valid() || !FILE_SYSTEM.isMounted(root.scheme()) ||
            !FILE_SYSTEM.isDirectory(root)) {
            configValid_ = false;
            Log::error("ShaderPreprocessor",
                       "Include search path is not a mounted directory: %s",
                       root.string().c_str());
        }
    }
}

std::shared_ptr<PreprocessedShader>
ShaderPreprocessor::process(const ShaderPreprocessRequest& request) {
    if (!configValid_ || !request.source.sourcePath.valid() || request.source.source.empty()) {
        Log::error("ShaderPreprocessor", "Shader stage source is missing");
        return {};
    }
    std::vector<ShaderDefine> defines = request.defines;
    std::ranges::sort(defines, [](const ShaderDefine& left, const ShaderDefine& right) {
        return std::tie(left.name, left.value) < std::tie(right.name, right.value);
    });
    Hash64 requestHash = hashString(request.source.sourcePath.string());
    requestHash = hashString(request.source.source, requestHash);
    hashAppend(requestHash, request.source.stage);
    for (const ShaderDefine& define : defines) {
        requestHash = hashString(define.name, requestHash);
        requestHash = hashString(define.value, requestHash);
    }
    for (const VirtualPath& root : config_.includeSearchPaths)
        requestHash = hashString(root.string(), requestHash);
    if (const auto found = cache_.find(requestHash); found != cache_.end())
        return found->second;

    auto result = std::make_shared<PreprocessedShader>();
    result->sourcePath = request.source.sourcePath;
    result->stage = request.source.stage;
    result->entryPoint = request.source.entryPoint;
    std::unordered_set<std::string> visiting;
    if (!preprocessSource(request.source.sourcePath,
                          request.source.source,
                          config_.includeSearchPaths,
                          visiting,
                          *result)) {
        return {};
    }
    std::string defineSource;
    for (const ShaderDefine& define : defines)
        defineSource += "#define " + define.name + " " + define.value + "\n";
    if (result->source.starts_with("#version")) {
        const std::size_t lineEnd = result->source.find('\n');
        result->source.insert(lineEnd == std::string::npos ? result->source.size() : lineEnd + 1,
                              defineSource);
    } else {
        result->source.insert(0, defineSource);
    }
    result->sourceHash = hashString(result->source);
    result->sourceHash = hashString(result->entryPoint, result->sourceHash);
    hashAppend(result->sourceHash, result->stage);
    result->cachePath =
        VirtualPath{"shader-preprocess://" + hashToHex(result->sourceHash) + ".glsl"};
    dependencies_.replaceDependencies(result->cachePath, result->dependencies);
    cache_[requestHash] = result;
    return result;
}

void ShaderPreprocessor::invalidate(std::span<const VirtualPath> paths) {
    std::erase_if(cache_, [&](const auto& entry) {
        if (!containsPath(paths, entry.second->cachePath))
            return false;
        dependencies_.remove(entry.second->cachePath);
        return true;
    });
}

void ShaderPreprocessor::clear() {
    for (const auto& [hash, shader] : cache_) {
        (void)hash;
        dependencies_.remove(shader->cachePath);
    }
    cache_.clear();
}

} // namespace engine

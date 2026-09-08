#pragma once

#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"

#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

class FileDependencyGraph final : public Singleton<FileDependencyGraph> {
public:
    void replaceDependencies(const VirtualPath& output, std::span<const VirtualPath> inputs);
    void remove(const VirtualPath& output);

    [[nodiscard]] std::vector<VirtualPath> dependenciesOf(const VirtualPath& output) const;
    [[nodiscard]] std::vector<VirtualPath> directDependentsOf(const VirtualPath& input) const;
    [[nodiscard]] std::vector<VirtualPath> transitiveDependentsOf(const VirtualPath& input) const;

    void notifyChanged(const VirtualPath& path);
    [[nodiscard]] std::vector<VirtualPath> consumeChangedFiles();

    void clear();

private:
    friend class Singleton<FileDependencyGraph>;
    FileDependencyGraph() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<VirtualPath>> dependencies_;
    std::unordered_map<std::string, std::unordered_set<std::string>> dependents_;
    std::unordered_set<std::string> changed_;
};

} // namespace engine

#define FILE_DEPENDENCY_GRAPH (::engine::FileDependencyGraph::instance())

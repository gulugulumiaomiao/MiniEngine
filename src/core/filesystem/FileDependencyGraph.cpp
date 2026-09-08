#include "core/filesystem/FileDependencyGraph.h"

#include <algorithm>

namespace engine {
namespace {

std::vector<VirtualPath> normalized(std::span<const VirtualPath> paths) {
    std::vector<VirtualPath> result;
    result.reserve(paths.size());
    for (const VirtualPath& path : paths) {
        if (path.valid())
            result.emplace_back(path.string());
    }
    std::ranges::sort(result, {}, &VirtualPath::string);
    result.erase(std::ranges::unique(result, {}, &VirtualPath::string).begin(), result.end());
    return result;
}

} // namespace

void FileDependencyGraph::replaceDependencies(const VirtualPath& output,
                                              std::span<const VirtualPath> inputs) {
    if (!output.valid())
        return;
    const std::string outputKey = output.string();
    const std::vector<VirtualPath> replacement = normalized(inputs);
    std::scoped_lock lock{mutex_};
    if (const auto old = dependencies_.find(outputKey); old != dependencies_.end()) {
        for (const VirtualPath& input : old->second) {
            auto reverse = dependents_.find(input.string());
            if (reverse == dependents_.end())
                continue;
            reverse->second.erase(outputKey);
            if (reverse->second.empty())
                dependents_.erase(reverse);
        }
    }
    dependencies_[outputKey] = replacement;
    for (const VirtualPath& input : replacement)
        dependents_[input.string()].insert(outputKey);
}

void FileDependencyGraph::remove(const VirtualPath& output) {
    if (!output.valid())
        return;
    std::scoped_lock lock{mutex_};
    const auto found = dependencies_.find(output.string());
    if (found == dependencies_.end())
        return;
    for (const VirtualPath& input : found->second) {
        auto reverse = dependents_.find(input.string());
        if (reverse == dependents_.end())
            continue;
        reverse->second.erase(output.string());
        if (reverse->second.empty())
            dependents_.erase(reverse);
    }
    dependencies_.erase(found);
}

std::vector<VirtualPath>
FileDependencyGraph::dependenciesOf(const VirtualPath& output) const {
    std::scoped_lock lock{mutex_};
    const auto found = dependencies_.find(output.string());
    return found == dependencies_.end() ? std::vector<VirtualPath>{} : found->second;
}

std::vector<VirtualPath>
FileDependencyGraph::directDependentsOf(const VirtualPath& input) const {
    std::scoped_lock lock{mutex_};
    const auto found = dependents_.find(input.string());
    if (found == dependents_.end())
        return {};
    std::vector<VirtualPath> result;
    result.reserve(found->second.size());
    for (const std::string& path : found->second)
        result.emplace_back(path);
    std::ranges::sort(result, {}, &VirtualPath::string);
    return result;
}

std::vector<VirtualPath>
FileDependencyGraph::transitiveDependentsOf(const VirtualPath& input) const {
    std::scoped_lock lock{mutex_};
    std::vector<VirtualPath> result;
    std::vector<std::string> pending{input.string()};
    std::unordered_set<std::string> visited{input.string()};
    while (!pending.empty()) {
        const std::string current = std::move(pending.back());
        pending.pop_back();
        const auto found = dependents_.find(current);
        if (found == dependents_.end())
            continue;
        for (const std::string& dependent : found->second) {
            if (!visited.insert(dependent).second)
                continue;
            result.emplace_back(dependent);
            pending.push_back(dependent);
        }
    }
    std::ranges::sort(result, {}, &VirtualPath::string);
    return result;
}

void FileDependencyGraph::notifyChanged(const VirtualPath& path) {
    if (!path.valid())
        return;
    std::scoped_lock lock{mutex_};
    changed_.insert(path.string());
}

std::vector<VirtualPath> FileDependencyGraph::consumeChangedFiles() {
    std::scoped_lock lock{mutex_};
    std::vector<VirtualPath> result;
    result.reserve(changed_.size());
    for (const std::string& path : changed_)
        result.emplace_back(path);
    changed_.clear();
    std::ranges::sort(result, {}, &VirtualPath::string);
    return result;
}

void FileDependencyGraph::clear() {
    std::scoped_lock lock{mutex_};
    dependencies_.clear();
    dependents_.clear();
    changed_.clear();
}

} // namespace engine

#pragma once

#include "core/base/HandlePool.h"
#include "core/filesystem/VirtualPath.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace engine {

template <typename Resource, typename HandleType> class InstanceManager {
public:
    virtual ~InstanceManager() = default;

    [[nodiscard]] HandleType insert(Resource resource) {
        if (!validate(resource))
            return {};

        const VirtualPath path = pathOf(resource);
        if (path.valid()) {
            const auto indexed = pathIndex_.find(path.string());
            if (indexed != pathIndex_.end() && pool_.find(indexed->second)) {
                return indexed->second;
            }
        }

        const HandleType handle = pool_.insert(std::move(resource));
        if (path.valid())
            pathIndex_.insert_or_assign(path.string(), handle);
        return handle;
    }

    [[nodiscard]] virtual HandleType load(const VirtualPath& path) = 0;

    bool destroy(HandleType handle) {
        Resource* resource = pool_.find(handle);
        if (!resource)
            return false;

        const VirtualPath path = pathOf(*resource);
        if (path.valid()) {
            const auto indexed = pathIndex_.find(path.string());
            if (indexed != pathIndex_.end() && indexed->second == handle) {
                pathIndex_.erase(indexed);
            }
        }
        return pool_.release(handle);
    }

    [[nodiscard]] Resource* find(HandleType handle) { return pool_.find(handle); }

    [[nodiscard]] const Resource* find(HandleType handle) const { return pool_.find(handle); }

    [[nodiscard]] Resource* find(const VirtualPath& path) {
        return const_cast<Resource*>(std::as_const(*this).find(path));
    }

    [[nodiscard]] const Resource* find(const VirtualPath& path) const {
        const auto indexed = pathIndex_.find(path.string());
        return indexed != pathIndex_.end() ? pool_.find(indexed->second) : nullptr;
    }

    virtual void clear() {
        pathIndex_.clear();
        pool_.clear();
    }

    [[nodiscard]] std::size_t size() const { return pool_.size(); }

protected:
    InstanceManager() = default;

    template <typename Function> void forEach(Function&& function) {
        pool_.forEach(std::forward<Function>(function));
    }

    [[nodiscard]] HandleType handleFor(const VirtualPath& path) const {
        const auto indexed = pathIndex_.find(path.string());
        return indexed != pathIndex_.end() && pool_.find(indexed->second) ? indexed->second
                                                                          : HandleType{};
    }

    [[nodiscard]] virtual const VirtualPath& pathOf(const Resource& resource) const = 0;
    [[nodiscard]] virtual bool validate(const Resource&) const { return true; }

private:
    HandlePool<Resource, HandleType> pool_;
    std::unordered_map<std::string, HandleType> pathIndex_;
};

} // namespace engine

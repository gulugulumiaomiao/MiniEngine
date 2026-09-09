#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace engine {

template <typename Key, typename Resource> class IGpuCache {
public:
    using Entry = std::pair<Key, Resource>;
    using Predicate = std::function<bool(const Key&, const Resource&)>;

    virtual ~IGpuCache() = default;

    [[nodiscard]] virtual const Resource* find(const Key& key) const = 0;
    [[nodiscard]] virtual std::optional<Resource> put(Key key, Resource resource) = 0;
    [[nodiscard]] virtual std::optional<Resource> remove(const Key& key) = 0;
    [[nodiscard]] virtual std::vector<Entry> extractIf(const Predicate& predicate) = 0;
    [[nodiscard]] virtual std::vector<Entry> extractAll() = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
};

} // namespace engine

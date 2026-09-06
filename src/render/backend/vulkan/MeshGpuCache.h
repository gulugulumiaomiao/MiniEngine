#pragma once

#include "render/renderer/DrawList.h"
#include "render/mesh/Mesh.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

class VulkanBackend;

class MeshGpuCache final {
public:
    explicit MeshGpuCache(VulkanBackend& backend) : backend_(backend) {}
    ~MeshGpuCache();

    [[nodiscard]] MeshDrawInfo prepare(MeshHandle handle, Mesh& mesh);
    void invalidate(MeshHandle handle);
    void clear();
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        MeshDrawInfo drawInfo;
        std::uint64_t meshVersion{};
    };

    [[nodiscard]] static std::uint64_t key(MeshHandle handle);
    void destroy(Entry& entry);

    VulkanBackend& backend_;
    std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace engine

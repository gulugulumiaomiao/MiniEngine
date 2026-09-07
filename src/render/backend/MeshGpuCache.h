#pragma once

#include "render/mesh/Mesh.h"
#include "render/renderer/DrawList.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

namespace rhi {
class IDevice;
}

class MeshGpuCache final {
    public:
    explicit MeshGpuCache(rhi::IDevice& device) : device_(device) {}
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

    rhi::IDevice& device_;
    std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace engine

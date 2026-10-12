#pragma once
#include "core/base/Ref.h"
#include "render/mesh/MeshTypes.h"
#include "rhi/api/ResourceDesc.h"
namespace engine {
struct MeshVertexBuffer {
    std::uint32_t binding{};
    rhi::RID buffer;
    bool operator==(const MeshVertexBuffer&) const = default;
};
// An immutable buffer identity snapshot; frames can retain it independently of Mesh rebuilds.
class MeshGpuBuffers final : public RefCounted {
public:
    ~MeshGpuBuffers() override;
    bool currentDevice() const;
    const std::vector<MeshVertexBuffer>& vertexBuffers() const { return vertexBuffers_; }
    rhi::RID indexBuffer() const { return indexBuffer_; }
    std::uint64_t deviceUid() const { return deviceUid_; }

private:
    friend class Mesh;
    MeshGpuBuffers() = default;
    static Ref<MeshGpuBuffers>
    create(MeshUsage usage, std::string_view debugName, const MeshData& data);
    bool matches(const MeshData& data) const;
    bool upload(const MeshData& data);
    struct StreamCapacity {
        std::uint32_t binding;
        std::uint32_t vertexCount;
        std::size_t bytes;
    };
    std::vector<StreamCapacity> capacities_;
    std::vector<MeshVertexBuffer> vertexBuffers_;
    rhi::RID indexBuffer_;
    std::uint32_t indexCount_{};
    std::size_t indexBytes_{};
    MeshUsage usage_{};
    std::uint64_t deviceUid_{};
};
} // namespace engine

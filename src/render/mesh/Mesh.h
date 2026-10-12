#pragma once
#include "render/mesh/MeshTypes.h"
#include "render/mesh/MeshGpuBuffers.h"
namespace engine {
class Mesh final : public RefCounted {
public:
    static Ref<Mesh> create(MeshCreateInfo info);
    bool rebuild(MeshCreateInfo info);
    bool upload(const MeshData& data);
    bool ensureGpuResources() const;
    Ref<Mesh> clone() const;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    std::uint64_t instanceId() const { return instanceId_; }
    std::uint64_t revision() const { return revision_; }
    bool keepsCpuCopy() const { return cpuData_.has_value(); }
    bool isValid() const { return ensureGpuResources(); }
    const std::vector<MeshVertexBuffer>& vertexBuffers() const;
    rhi::RID indexBuffer() const {
        return ensureGpuResources() ? buffers_->indexBuffer() : rhi::RID{};
    }
    const Ref<MeshGpuBuffers>& gpuBuffers() const {
        (void)ensureGpuResources();
        return buffers_;
    }
    rhi::IndexFormat indexFormat() const {
        return indexType_ == IndexType::UInt16 ? rhi::IndexFormat::UInt16
                                               : rhi::IndexFormat::UInt32;
    }
    MeshTopology topology() const { return topology_; }
    const std::vector<SubMesh>& subMeshes() const { return subMeshes_; }
    const VertexLayout& vertexLayout() const { return vertexLayout_; }
    const MeshBounds& bounds() const { return bounds_; }
    MeshUsage usage() const { return usage_; }

private:
    Mesh();
    MeshCreateInfo snapshot() const;
    std::string debugName_;
    VertexLayout vertexLayout_;
    IndexType indexType_{IndexType::UInt32};
    MeshUsage usage_{MeshUsage::Static};
    MeshTopology topology_{MeshTopology::TriangleList};
    std::vector<SubMesh> subMeshes_;
    MeshBounds bounds_;
    std::optional<MeshData> cpuData_;
    mutable Ref<MeshGpuBuffers> buffers_;
    std::uint64_t instanceId_{};
    std::uint64_t revision_{};
};
} // namespace engine

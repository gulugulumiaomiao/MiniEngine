#include "render/mesh/Mesh.h"
#include "core/logging/Log.h"
#include <atomic>
#include <utility>
namespace engine {
Mesh::Mesh() {
    static std::atomic<std::uint64_t> next{1};
    instanceId_ = next.fetch_add(1, std::memory_order_relaxed);
}
Ref<Mesh> Mesh::create(MeshCreateInfo info) {
    Ref<Mesh> mesh{new Mesh};
    return mesh->rebuild(std::move(info)) ? mesh : Ref<Mesh>{};
}
bool Mesh::rebuild(MeshCreateInfo info) {
    if (!validateMesh(info))
        return false;
    auto buffers = MeshGpuBuffers::create(info.usage, info.debugName, info.data);
    if (!buffers)
        return false;
    std::optional<MeshData> cpu;
    if (info.keepCpuCopy)
        cpu = std::move(info.data);
    debugName_ = std::move(info.debugName);
    vertexLayout_ = std::move(info.vertexLayout);
    indexType_ = info.indexType;
    usage_ = info.usage;
    topology_ = info.topology;
    subMeshes_ = std::move(info.subMeshes);
    bounds_ = info.bounds;
    cpuData_ = std::move(cpu);
    buffers_ = std::move(buffers);
    ++revision_;
    return true;
}
bool Mesh::upload(const MeshData& data) {
    if (!validateMesh(snapshot(), data) || !buffers_ || !buffers_->matches(data))
        return false;
    std::optional<MeshData> cpu;
    if (cpuData_.has_value())
        cpu = data;
    if (!buffers_->currentDevice() || usage_ == MeshUsage::Stream || buffers_->refCount() > 1) {
        auto replacement = MeshGpuBuffers::create(usage_, debugName_, data);
        if (!replacement)
            return false;
        buffers_ = std::move(replacement);
    } else if (!buffers_->upload(data))
        return false;
    cpuData_ = std::move(cpu);
    ++revision_;
    return true;
}
bool Mesh::ensureGpuResources() const {
    if (buffers_ && buffers_->currentDevice())
        return true;
    if (!cpuData_)
        return false;
    auto replacement = MeshGpuBuffers::create(usage_, debugName_, *cpuData_);
    if (!replacement)
        return false;
    buffers_ = std::move(replacement);
    return true;
}
const std::vector<MeshVertexBuffer>& Mesh::vertexBuffers() const {
    static const std::vector<MeshVertexBuffer> empty;
    return ensureGpuResources() ? buffers_->vertexBuffers() : empty;
}
Ref<Mesh> Mesh::clone() const {
    if (!cpuData_) {
        Log::warn("Mesh", "clone requires keepCpuCopy; supply fresh create info otherwise");
        return {};
    }
    auto info = snapshot();
    info.data = *cpuData_;
    return Mesh::create(std::move(info));
}
MeshCreateInfo Mesh::snapshot() const {
    MeshCreateInfo info;
    info.debugName = debugName_;
    info.vertexLayout = vertexLayout_;
    info.indexType = indexType_;
    info.usage = usage_;
    info.topology = topology_;
    info.subMeshes = subMeshes_;
    info.bounds = bounds_;
    info.keepCpuCopy = cpuData_.has_value();
    return info;
}
} // namespace engine

#include "render/backend/MeshGpuCache.h"

#include "rhi/api/Device.h"

#include <string>

namespace engine {

MeshGpuCache::~MeshGpuCache() {
    clear();
}

std::uint64_t MeshGpuCache::key(MeshHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

void MeshGpuCache::destroy(Entry& entry) {
    for (const DrawItem::VertexBuffer& vertex : entry.drawInfo.vertexBuffers) {
        device_.destroyBuffer(vertex.buffer);
    }
    device_.destroyBuffer(entry.drawInfo.indexBuffer);
    entry = {};
}

MeshDrawInfo MeshGpuCache::prepare(MeshHandle handle, Mesh& mesh) {
    if (!handle || !validateMesh(mesh.desc(), mesh.data()))
        return {};
    const std::uint64_t cacheKey = key(handle);
    if (const auto found = entries_.find(cacheKey);
        found != entries_.end() && found->second.meshVersion == mesh.version()) {
        return found->second.drawInfo;
    }

    // A previous frame may still reference these buffers. Deferred retirement
    // can replace this conservative synchronization without changing the cache.
    if (const auto found = entries_.find(cacheKey); found != entries_.end()) {
        device_.waitIdle();
        destroy(found->second);
        entries_.erase(found);
    }

    Entry entry;
    entry.meshVersion = mesh.version();
    entry.drawInfo.vertexBuffers.reserve(mesh.data().vertexStreams.size());
    for (const VertexStream& stream : mesh.data().vertexStreams) {
        const rhi::BufferHandle buffer = device_.createBuffer({
            .size = stream.bytes.size(),
            .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
            .memoryUsage = rhi::MemoryUsage::DeviceLocal,
            .debugName = mesh.desc().debugName + ".vertex." + std::to_string(stream.binding),
        });
        device_.uploadBuffer(buffer, stream.bytes);
        entry.drawInfo.vertexBuffers.push_back({stream.binding, buffer});
    }

    entry.drawInfo.indexBuffer = device_.createBuffer({
        .size = mesh.data().indices.size(),
        .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
        .memoryUsage = rhi::MemoryUsage::DeviceLocal,
        .debugName = mesh.desc().debugName + ".index",
    });
    device_.uploadBuffer(entry.drawInfo.indexBuffer, mesh.data().indices);
    entry.drawInfo.indexFormat = mesh.desc().indexType == IndexType::UInt16
                                     ? rhi::IndexFormat::UInt16
                                     : rhi::IndexFormat::UInt32;
    entry.drawInfo.subMeshes.reserve(mesh.desc().subMeshes.size());
    for (const SubMesh& subMesh : mesh.desc().subMeshes) {
        entry.drawInfo.subMeshes.push_back(
            {subMesh.firstIndex, subMesh.indexCount, subMesh.vertexOffset, subMesh.materialSlot});
    }
    mesh.markClean();
    const MeshDrawInfo result = entry.drawInfo;
    entries_.emplace(cacheKey, std::move(entry));
    return result;
}

void MeshGpuCache::invalidate(MeshHandle handle) {
    const auto found = entries_.find(key(handle));
    if (found == entries_.end())
        return;
    device_.waitIdle();
    destroy(found->second);
    entries_.erase(found);
}

void MeshGpuCache::clear() {
    for (auto& [unused, entry] : entries_) {
        (void)unused;
        destroy(entry);
    }
    entries_.clear();
}

} // namespace engine

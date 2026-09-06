#include "render/backend/vulkan/MeshGpuCache.h"

#include "core/logging/Log.h"
#include "render/backend/vulkan/VulkanBackend.h"

#include <vk_mem_alloc.h>

namespace engine {

MeshGpuCache::~MeshGpuCache() { clear(); }

std::uint64_t MeshGpuCache::key(MeshHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) |
           handle.index;
}

void MeshGpuCache::destroy(Entry& entry) {
    for (const DrawItem::VertexBuffer& vertex : entry.drawInfo.vertexBuffers) {
        backend_.destroyGpuBuffer(vertex.buffer);
    }
    backend_.destroyGpuBuffer(entry.drawInfo.indexBuffer);
    entry = {};
}

MeshDrawInfo MeshGpuCache::prepare(MeshHandle handle, Mesh& mesh) {
    if (!handle || !validateMesh(mesh.desc(), mesh.data())) return {};
    const std::uint64_t cacheKey = key(handle);
    if (const auto found = entries_.find(cacheKey);
        found != entries_.end() && found->second.meshVersion == mesh.version()) {
        return found->second.drawInfo;
    }

    // A previous frame may still reference these buffers. This first version
    // favors simple, correct lifetime handling; deferred retirement can be
    // introduced without changing the cache API.
    if (const auto found = entries_.find(cacheKey); found != entries_.end()) {
        backend_.waitIdle();
        destroy(found->second);
        entries_.erase(found);
    }

    Entry entry;
    entry.meshVersion = mesh.version();
    entry.drawInfo.vertexBuffers.reserve(mesh.data().vertexStreams.size());
    for (const VertexStream& stream : mesh.data().vertexStreams) {
        const VkDeviceSize byteSize = stream.bytes.size();
        const rhi::BufferHandle staging = backend_.createGpuBuffer(
            byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        backend_.requireBuffer(staging).upload(stream.bytes);
        const rhi::BufferHandle buffer = backend_.createGpuBuffer(
            byteSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        backend_.uploadBuffer(staging, buffer, byteSize);
        backend_.destroyGpuBuffer(staging);
        entry.drawInfo.vertexBuffers.push_back({stream.binding, buffer});
    }

    const VkDeviceSize indexBytes = mesh.data().indices.size();
    const rhi::BufferHandle indexStaging = backend_.createGpuBuffer(
        indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    backend_.requireBuffer(indexStaging).upload(mesh.data().indices);
    entry.drawInfo.indexBuffer = backend_.createGpuBuffer(
        indexBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    backend_.uploadBuffer(indexStaging, entry.drawInfo.indexBuffer, indexBytes);
    backend_.destroyGpuBuffer(indexStaging);
    entry.drawInfo.indexFormat = mesh.desc().indexType == IndexType::UInt16
                                     ? rhi::IndexFormat::UInt16
                                     : rhi::IndexFormat::UInt32;
    entry.drawInfo.subMeshes.reserve(mesh.desc().subMeshes.size());
    for (const SubMesh& subMesh : mesh.desc().subMeshes) {
        entry.drawInfo.subMeshes.push_back(
            {subMesh.firstIndex, subMesh.indexCount, subMesh.vertexOffset,
             subMesh.materialSlot});
    }
    mesh.markClean();
    const MeshDrawInfo result = entry.drawInfo;
    entries_.emplace(cacheKey, std::move(entry));
    return result;
}

void MeshGpuCache::invalidate(MeshHandle handle) {
    const auto found = entries_.find(key(handle));
    if (found == entries_.end()) return;
    backend_.waitIdle();
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

#include "render/gpu/mesh/MeshStorageFactory.h"

#include "render/mesh/Mesh.h"
#include "rhi/api/Device.h"

#include <string>

namespace engine {

bool MeshStorageFactory::create(const MeshStorageCreateInfo& request,
                                MeshStorageEntry& destination) {
    const Mesh& mesh = request.mesh;
    if (!validateMesh(mesh.desc(), mesh.data()))
        return false;

    MeshStorageEntry uploaded;
    uploaded.keepCpuCopy = mesh.desc().keepCpuCopy;
    MeshStorageLod baseLod;
    baseLod.minimumScreenCoverage = 0.0F;
    baseLod.surfaces.reserve(mesh.desc().subMeshes.size());
    for (std::uint32_t index = 0; index < mesh.desc().subMeshes.size(); ++index)
        baseLod.surfaces.push_back({index, mesh.desc().subMeshes[index].materialSlot});
    uploaded.lods.push_back(std::move(baseLod));
    uploaded.drawInfo.vertexBuffers.reserve(mesh.data().vertexStreams.size());
    for (const VertexStream& stream : mesh.data().vertexStreams) {
        const rhi::RID buffer = device_.buffer_create({
            .size = stream.bytes.size(),
            .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
            .memoryUsage = rhi::MemoryUsage::DeviceLocal,
            .debugName = mesh.desc().debugName + ".vertex." + std::to_string(stream.binding),
        });
        device_.buffer_upload(buffer, stream.bytes);
        uploaded.drawInfo.vertexBuffers.push_back({stream.binding, buffer});
    }
    uploaded.drawInfo.indexBuffer = device_.buffer_create({
        .size = mesh.data().indices.size(),
        .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
        .memoryUsage = rhi::MemoryUsage::DeviceLocal,
        .debugName = mesh.desc().debugName + ".index",
    });
    device_.buffer_upload(uploaded.drawInfo.indexBuffer, mesh.data().indices);
    uploaded.drawInfo.indexFormat = mesh.desc().indexType == IndexType::UInt16
                                        ? rhi::IndexFormat::UInt16
                                        : rhi::IndexFormat::UInt32;
    uploaded.drawInfo.subMeshes.reserve(mesh.desc().subMeshes.size());
    for (const SubMesh& subMesh : mesh.desc().subMeshes) {
        uploaded.drawInfo.subMeshes.push_back(
            {subMesh.firstIndex, subMesh.indexCount, subMesh.vertexOffset, subMesh.materialSlot});
    }
    release(destination);
    destination = std::move(uploaded);
    return true;
}

void MeshStorageFactory::release(MeshStorageEntry& resource) {
    for (const DrawItem::VertexBuffer& vertex : resource.drawInfo.vertexBuffers)
        device_.buffer_destroy(vertex.buffer);
    if (resource.drawInfo.indexBuffer)
        device_.buffer_destroy(resource.drawInfo.indexBuffer);
    resource = {};
}

} // namespace engine

#include "render/gpu/mesh/MeshGpuFactory.h"

#include "render/mesh/Mesh.h"
#include "rhi/api/Device.h"

#include <string>
#include <cstring>
#include <vector>

namespace engine {

bool MeshGpuFactory::create(const MeshGpuCreateInfo& request, MeshGpuResource& destination) {
    const Mesh& mesh = request.mesh;
    if (!validateMesh(mesh.desc(), mesh.data()))
        return false;

    MeshGpuResource uploaded;
    uploaded.drawInfo.vertexBuffers.reserve(mesh.data().vertexStreams.size());
    for (const VertexStream& stream : mesh.data().vertexStreams) {
        const rhi::BufferHandle buffer = device_.createBuffer({
            .size = stream.bytes.size(),
            .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
            .memoryUsage = rhi::MemoryUsage::DeviceLocal,
            .debugName = mesh.desc().debugName + ".vertex." + std::to_string(stream.binding),
        });
        device_.uploadBuffer(buffer, stream.bytes);
        uploaded.drawInfo.vertexBuffers.push_back({stream.binding, buffer});
    }
    std::vector<std::byte> normalizedIndices;
    std::span<const std::byte> indexBytes = mesh.data().indices;
    if (mesh.desc().indexType == IndexType::UInt16) {
        normalizedIndices.resize(static_cast<std::size_t>(mesh.data().indexCount) * sizeof(std::uint32_t));
        for (std::uint32_t index = 0; index < mesh.data().indexCount; ++index) {
            std::uint16_t source{};
            std::memcpy(&source,
                        mesh.data().indices.data() + static_cast<std::size_t>(index) * sizeof(source),
                        sizeof(source));
            const std::uint32_t destination = source;
            std::memcpy(normalizedIndices.data() + static_cast<std::size_t>(index) * sizeof(destination),
                        &destination,
                        sizeof(destination));
        }
        indexBytes = normalizedIndices;
    }
    uploaded.drawInfo.indexBuffer = device_.createBuffer({
        .size = indexBytes.size(),
        .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
        .memoryUsage = rhi::MemoryUsage::DeviceLocal,
        .debugName = mesh.desc().debugName + ".index",
    });
    device_.uploadBuffer(uploaded.drawInfo.indexBuffer, indexBytes);
    uploaded.drawInfo.indexFormat = rhi::IndexFormat::UInt32;
    uploaded.drawInfo.subMeshes.reserve(mesh.desc().subMeshes.size());
    for (const SubMesh& subMesh : mesh.desc().subMeshes) {
        uploaded.drawInfo.subMeshes.push_back(
            {subMesh.firstIndex, subMesh.indexCount, subMesh.vertexOffset, subMesh.materialSlot});
    }
    release(destination);
    destination = std::move(uploaded);
    return true;
}

void MeshGpuFactory::release(MeshGpuResource& resource) {
    for (const DrawItem::VertexBuffer& vertex : resource.drawInfo.vertexBuffers)
        device_.destroyBuffer(vertex.buffer);
    if (resource.drawInfo.indexBuffer)
        device_.destroyBuffer(resource.drawInfo.indexBuffer);
    resource = {};
}

} // namespace engine

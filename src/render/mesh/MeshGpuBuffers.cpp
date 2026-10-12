#include "render/mesh/MeshGpuBuffers.h"
#include "rhi/api/Device.h"
#include <algorithm>
namespace engine {
Ref<MeshGpuBuffers>
MeshGpuBuffers::create(MeshUsage usage, std::string_view debugName, const MeshData& data) {
    auto* device = RHI_DEVICE;
    if (!device)
        return {};
    Ref<MeshGpuBuffers> result{new MeshGpuBuffers};
    result->deviceUid_ = device->uid();
    result->usage_ = usage;
    result->indexCount_ = data.indexCount;
    result->indexBytes_ = data.indices.size();
    const bool transient = usage == MeshUsage::Stream;
    const auto allocate = [&](rhi::BufferDesc info) {
        if (transient)
            return device->buffer_acquire_transient(info);
        auto rid = device->buffer_allocate_rid(info);
        if (rid)
            device->buffer_allocate_memory(rid);
        return rid;
    };
    const auto memory =
        usage == MeshUsage::Static ? rhi::MemoryUsage::DeviceLocal : rhi::MemoryUsage::Upload;
    for (const auto& stream : data.vertexStreams) {
        const auto rid = allocate(
            {.size = stream.bytes.size(),
             .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
             .memoryUsage = memory,
             .debugName = std::string{debugName} + ".vertex." + std::to_string(stream.binding)});
        if (!rid)
            return {};
        result->vertexBuffers_.push_back({stream.binding, rid});
        result->capacities_.push_back({stream.binding, stream.vertexCount, stream.bytes.size()});
    }
    result->indexBuffer_ =
        allocate({.size = data.indices.size(),
                  .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
                  .memoryUsage = memory,
                  .debugName = std::string{debugName} + ".index"});
    if (!result->indexBuffer_ || !result->upload(data))
        return {};
    return result;
}
MeshGpuBuffers::~MeshGpuBuffers() {
    if (usage_ == MeshUsage::Stream || !currentDevice())
        return;
    auto* device = RHI_DEVICE;
    for (const auto& vertex : vertexBuffers_)
        device->buffer_destroy(vertex.buffer);
    if (indexBuffer_)
        device->buffer_destroy(indexBuffer_);
}
bool MeshGpuBuffers::currentDevice() const {
    return RHI_DEVICE && RHI_DEVICE->uid() == deviceUid_;
}
bool MeshGpuBuffers::matches(const MeshData& data) const {
    if (indexCount_ != data.indexCount || indexBytes_ != data.indices.size() ||
        capacities_.size() != data.vertexStreams.size())
        return false;
    return std::ranges::all_of(capacities_, [&](const auto& capacity) {
        const auto* stream = data.findVertexStream(capacity.binding);
        return stream && stream->vertexCount == capacity.vertexCount &&
               stream->bytes.size() == capacity.bytes;
    });
}
bool MeshGpuBuffers::upload(const MeshData& data) {
    if (!currentDevice() || !matches(data))
        return false;
    for (const auto& vertex : vertexBuffers_) {
        const auto* stream = data.findVertexStream(vertex.binding);
        RHI_DEVICE->buffer_upload(vertex.buffer, stream->bytes);
    }
    RHI_DEVICE->buffer_upload(indexBuffer_, data.indices);
    return true;
}
} // namespace engine

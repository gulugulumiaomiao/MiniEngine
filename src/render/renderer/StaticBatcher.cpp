#include "render/renderer/StaticBatcher.h"

#include "core/math/hash.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/MeshManager.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <span>
#include <vector>

namespace engine {
namespace {

const VertexStream* findStream(const MeshData& data, std::uint32_t binding) {
    const auto found = std::ranges::find_if(
        data.vertexStreams, [binding](const VertexStream& stream) { return stream.binding == binding; });
    return found == data.vertexStreams.end() ? nullptr : &*found;
}

void transformStream(std::vector<std::byte>& bytes,
                     std::size_t start,
                     std::uint32_t vertexCount,
                     const VertexLayout& layout,
                     std::uint32_t binding,
                     const math::Mat44& world) {
    const VertexBinding* bindingDesc = layout.findBinding(binding);
    if (!bindingDesc || bindingDesc->stride == 0) {
        return;
    }
    const math::Mat33 normals = math::normalMatrix(world);
    for (const VertexAttribute& attribute : layout.attributes) {
        if (attribute.binding != binding) {
            continue;
        }
        for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
            std::byte* address = bytes.data() + start +
                                 static_cast<std::size_t>(vertex) * bindingDesc->stride +
                                 attribute.offset;
            if (attribute.semantic.type == VertexSemanticType::Position &&
                attribute.format == VertexFormat::Vec3Float32) {
                math::Vec3 value;
                std::memcpy(&value, address, sizeof(value));
                value = math::transformPoint(world, value);
                std::memcpy(address, &value, sizeof(value));
            } else if (attribute.semantic.type == VertexSemanticType::Normal &&
                       attribute.format == VertexFormat::Vec3Float32) {
                math::Vec3 value;
                std::memcpy(&value, address, sizeof(value));
                value = math::normalize(normals * value, math::Vec3{0.0F, 1.0F, 0.0F});
                std::memcpy(address, &value, sizeof(value));
            } else if (attribute.semantic.type == VertexSemanticType::Tangent &&
                       attribute.format == VertexFormat::Vec4Float32) {
                math::Vec4 value;
                std::memcpy(&value, address, sizeof(value));
                const math::Vec3 tangent =
                    math::normalize(normals * math::Vec3{value}, math::Vec3{1.0F, 0.0F, 0.0F});
                value = math::Vec4{tangent, value.w};
                std::memcpy(address, &value, sizeof(value));
            }
        }
    }
}

Hash64 compatibilityKey(const DrawItem& item) {
    Hash64 hash = kFnv1a64OffsetBasis;
    hashAppend(hash, item.pipeline.index);
    hashAppend(hash, item.pipeline.generation);
    hashAppend(hash, item.material.index);
    hashAppend(hash, item.material.generation);
    hashAppend(hash, item.renderPhase);
    hashAppend(hash, item.renderQueue);
    const Mesh* mesh = MESH_MANAGER.find(item.mesh);
    hashAppend(hash, mesh ? mesh->vertexLayoutHash() : 0U);
    hashAppend(hash, item.drawState.raster.cull);
    hashAppend(hash, item.drawState.raster.frontFace);
    hashAppend(hash, item.drawState.raster.fill);
    hashAppend(hash, item.drawState.depthStencil.depthTestEnable);
    hashAppend(hash, item.drawState.depthStencil.depthWriteEnable);
    hashAppend(hash, item.drawState.depthStencil.depthCompare);
    hashAppend(hash, item.drawState.blend.mode);
    hashAppend(hash, item.drawState.blend.colorWriteMask);
    hashAppend(hash, item.drawState.colorAttachmentCount);
    return hash;
}

} // namespace

struct StaticBatcher::CachedGeometry {
    std::vector<DrawItem::VertexBuffer> vertexBuffers;
    rhi::BufferHandle indexBuffer;
    std::uint32_t indexCount{};
};

StaticBatcher::StaticBatcher(StaticBatcherLimits limits) : limits_(limits) {}

StaticBatcher::~StaticBatcher() {
    clear();
}

std::uint64_t StaticBatcher::key(std::span<const DrawItem> items) const {
    Hash64 hash = kFnv1a64OffsetBasis;
    for (const DrawItem& item : items) {
        hashAppend(hash, item.mesh.index);
        hashAppend(hash, item.mesh.generation);
        if (const Mesh* mesh = MESH_MANAGER.find(item.mesh)) {
            hashAppend(hash, mesh->version());
        }
        if (const Material* material = MATERIAL_MANAGER.find(item.material)) {
            hashAppend(hash, material->version());
        }
        hashAppend(hash, item.arguments.firstIndex);
        hashAppend(hash, item.arguments.indexCount);
        hashAppend(hash, item.arguments.vertexOffset);
        hash = hashBytes(std::as_bytes(std::span{&item.worldMatrix, 1}), hash);
    }
    return hash;
}

StaticBatcher::CachedGeometry* StaticBatcher::resolve(std::span<const DrawItem> items,
                                                      rhi::IDevice& device) {
    const Hash64 cacheKey = key(items);
    if (const auto found = cache_.find(cacheKey); found != cache_.end()) {
        ++stats_.cacheHits;
        return found->second.get();
    }
    if (items.size() < 2) {
        return nullptr;
    }
    const Mesh* firstMesh = MESH_MANAGER.find(items.front().mesh);
    if (!firstMesh || firstMesh->data().vertexStreams.empty()) {
        return nullptr;
    }
    const VertexLayout& layout = firstMesh->desc().vertexLayout;
    std::vector<std::vector<std::byte>> streams(firstMesh->data().vertexStreams.size());
    std::vector<std::uint32_t> indices;
    std::uint32_t baseVertex{};

    for (const DrawItem& item : items) {
        const Mesh* mesh = MESH_MANAGER.find(item.mesh);
        if (!mesh || mesh->desc().vertexLayout != layout ||
            mesh->data().vertexStreams.size() != streams.size()) {
            return nullptr;
        }
        const VertexStream* positionStream = &mesh->data().vertexStreams.front();
        if (!positionStream) {
            return nullptr;
        }
        for (std::size_t streamIndex = 0; streamIndex < streams.size(); ++streamIndex) {
            const VertexStream& firstStream = firstMesh->data().vertexStreams[streamIndex];
            const VertexStream* source = findStream(mesh->data(), firstStream.binding);
            if (!source || source->vertexCount != positionStream->vertexCount) {
                return nullptr;
            }
            const std::size_t start = streams[streamIndex].size();
            streams[streamIndex].insert(
                streams[streamIndex].end(), source->bytes.begin(), source->bytes.end());
            transformStream(streams[streamIndex],
                            start,
                            source->vertexCount,
                            layout,
                            source->binding,
                            item.worldMatrix);
        }
        for (std::uint32_t index = 0; index < item.arguments.indexCount; ++index) {
            const std::size_t sourceIndex = item.arguments.firstIndex + index;
            std::uint32_t value{};
            if (mesh->desc().indexType == IndexType::UInt32) {
                if ((sourceIndex + 1) * sizeof(std::uint32_t) > mesh->data().indices.size()) {
                    return nullptr;
                }
                std::memcpy(&value,
                            mesh->data().indices.data() + sourceIndex * sizeof(std::uint32_t),
                            sizeof(value));
            } else {
                if ((sourceIndex + 1) * sizeof(std::uint16_t) > mesh->data().indices.size()) {
                    return nullptr;
                }
                std::uint16_t small{};
                std::memcpy(&small,
                            mesh->data().indices.data() + sourceIndex * sizeof(std::uint16_t),
                            sizeof(small));
                value = small;
            }
            const std::int64_t rebased = static_cast<std::int64_t>(value) +
                                         item.arguments.vertexOffset + baseVertex;
            if (rebased < 0 || rebased > std::numeric_limits<std::uint32_t>::max()) {
                return nullptr;
            }
            indices.push_back(static_cast<std::uint32_t>(rebased));
        }
        baseVertex += positionStream->vertexCount;
    }

    auto created = std::make_unique<CachedGeometry>();
    for (std::size_t index = 0; index < streams.size(); ++index) {
        const VertexStream& source = firstMesh->data().vertexStreams[index];
        const rhi::BufferHandle buffer = device.createBuffer({
            .size = streams[index].size(),
            .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDestination,
            .memoryUsage = rhi::MemoryUsage::DeviceLocal,
            .debugName = "StaticBatch.Vertex." + std::to_string(source.binding),
        });
        if (!buffer) {
            for (const DrawItem::VertexBuffer& vertex : created->vertexBuffers) {
                device.destroyBuffer(vertex.buffer);
            }
            return nullptr;
        }
        device.uploadBuffer(buffer, streams[index]);
        created->vertexBuffers.push_back({source.binding, buffer});
    }
    created->indexBuffer = device.createBuffer({
        .size = indices.size() * sizeof(std::uint32_t),
        .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDestination,
        .memoryUsage = rhi::MemoryUsage::DeviceLocal,
        .debugName = "StaticBatch.Index",
    });
    if (!created->indexBuffer) {
        for (const DrawItem::VertexBuffer& vertex : created->vertexBuffers) {
            device.destroyBuffer(vertex.buffer);
        }
        return nullptr;
    }
    device.uploadBuffer(created->indexBuffer, std::as_bytes(std::span{indices}));
    created->indexCount = static_cast<std::uint32_t>(indices.size());
    CachedGeometry* result = created.get();
    cache_.emplace(cacheKey, std::move(created));
    ++stats_.cacheMisses;
    return result;
}

void StaticBatcher::process(DrawList& drawList, rhi::IDevice& device) {
    device_ = &device;
    stats_ = {};
    std::map<int, std::vector<DrawItem>> rebuilt;
    for (auto& [queue, queueItems] : drawList.groups) {
        std::vector<DrawItem> passthrough;
        std::map<Hash64, std::vector<DrawItem>> candidates;
        for (DrawItem& item : queueItems) {
            if (item.batchingMode == MaterialBatchingMode::Static) {
                candidates[compatibilityKey(item)].push_back(std::move(item));
                ++stats_.sourceItems;
            } else {
                passthrough.push_back(std::move(item));
            }
        }
        for (auto& [unused, items] : candidates) {
            (void)unused;
            std::size_t begin{};
            while (begin < items.size()) {
                std::size_t end = begin;
                std::uint32_t indexCount{};
                while (end < items.size() && end - begin < limits_.maxSourceItemsPerBatch &&
                       indexCount + items[end].arguments.indexCount <=
                           limits_.maxIndicesPerBatch) {
                    indexCount += items[end].arguments.indexCount;
                    ++end;
                }
                if (end == begin) {
                    ++end;
                }
                const std::span batch{items.data() + begin, end - begin};
                if (CachedGeometry* geometry = resolve(batch, device)) {
                    DrawItem combined = batch.front();
                    combined.mesh = {};
                    combined.vertexBuffers = geometry->vertexBuffers;
                    combined.indexBuffer = geometry->indexBuffer;
                    combined.indexFormat = rhi::IndexFormat::UInt32;
                    combined.arguments = {.indexCount = geometry->indexCount,
                                          .instanceCount = 1,
                                          .firstIndex = 0,
                                          .vertexOffset = 0,
                                          .firstInstance = static_cast<std::uint32_t>(
                                              drawList.objects.size())};
                    combined.batchingMode = MaterialBatchingMode::None;
                    combined.worldMatrix = math::Mat44{1.0F};
                    drawList.objects.push_back({math::Mat44{1.0F}});
                    passthrough.push_back(std::move(combined));
                    ++stats_.combinedDraws;
                } else {
                    passthrough.insert(passthrough.end(), batch.begin(), batch.end());
                }
                begin = end;
            }
        }
        rebuilt.emplace(queue, std::move(passthrough));
    }
    drawList.groups = std::move(rebuilt);
}

void StaticBatcher::clear() {
    if (device_) {
        for (const auto& [key, geometry] : cache_) {
            (void)key;
            for (const DrawItem::VertexBuffer& vertex : geometry->vertexBuffers) {
                device_->destroyBuffer(vertex.buffer);
            }
            if (geometry->indexBuffer) {
                device_->destroyBuffer(geometry->indexBuffer);
            }
        }
    }
    cache_.clear();
    device_ = nullptr;
}

} // namespace engine

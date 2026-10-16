#pragma once

#include "render/texture/Texture.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine {
class RgRenderPassCache;

// Global pool for one device's graphics queue. CPU release and GPU completion
// are independent. The pool and external tracked-state owners outlive batches.
class RgTexturePool final {
    struct Bin;
public:
    enum class Status { Available, Leased, PendingSubmit, InFlight };
    struct Entry {
        Ref<Texture> texture;
        bool inUse{};
        rhi::ResourceState lastState{rhi::ResourceState::Undefined};
        rhi::AccessMode lastAccess{rhi::AccessMode::Read};
        [[nodiscard]] Status status() const { return status_; }
    private:
        friend class RgTexturePool;
        RgTexturePool* owner_{};
        Bin* bin_{};
        Status status_{Status::Available};
        rhi::ResourceState pendingState_{rhi::ResourceState::Undefined};
        rhi::AccessMode pendingAccess_{rhi::AccessMode::Read};
        std::uint64_t lastUsedEpoch_{};
    };

    RgTexturePool() = default;
    ~RgTexturePool();
    RgTexturePool(const RgTexturePool&) = delete;
    RgTexturePool& operator=(const RgTexturePool&) = delete;
    RgTexturePool(RgTexturePool&&) = delete;
    RgTexturePool& operator=(RgTexturePool&&) = delete;

    [[nodiscard]] Entry* acquire(const TextureDesc& desc, rhi::TextureUsage usage);
    static void release(Entry* entry);

    // One batch records one graphics command buffer. Allocates its retirement
    // record before GPU submission, so onSubmitted cannot fail allocation.
    void beginBatch(rhi::RID commandBuffer = {});
    void bindCommandBuffer(rhi::RID commandBuffer);
    void recordUsage(Entry* entry, rhi::ResourceState state, rhi::AccessMode access);
    void recordState(rhi::ResourceState* trackedState, rhi::ResourceState state);
    [[nodiscard]] rhi::ResourceState trackedState(rhi::ResourceState* state) const;
    void onSubmitted(rhi::SubmissionId submission);
    // Call only after discarding the batch's recorded commands.
    void cancelBatch();
    void collect(rhi::SubmissionId completed);
    // Only completed/unleased entries are evicted; invalidates native framebuffers first.
    void trim(std::uint64_t maxIdleBatches, RgRenderPassCache& cache);

    [[nodiscard]] bool batchActive() const { return batchActive_; }
    [[nodiscard]] std::size_t totalEntryCount() const { return entryCount_; }
    [[nodiscard]] std::size_t inUseCount() const { return activeLeases_; }

private:
    struct Key {
        TextureType type;
        TextureFormat format;
        std::uint32_t width, height, depth, arrayLayers, mipCount;
        rhi::TextureUsage usage;
        std::uint8_t samples;
        SamplerDesc sampler;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash { std::size_t operator()(const Key& key) const; };
    struct Bin {
        std::vector<std::unique_ptr<Entry>> entries;
        std::vector<Entry*> freeEntries;
    };
    struct Batch {
        rhi::SubmissionId submission{}; // zero while recording
        std::vector<Entry*> entries;
    };
    void bindActiveDevice();
    void makeAvailable(Entry& entry);
    void releaseEntry(Entry& entry);
    [[nodiscard]] std::unique_ptr<Entry> createEntry(const TextureDesc& desc, rhi::TextureUsage usage, Bin& bin);

    std::unordered_map<Key, Bin, KeyHash> bins_;
    std::deque<Batch> batches_;
    std::unordered_map<rhi::ResourceState*, rhi::ResourceState> pendingStates_;
    rhi::RID batchCommandBuffer_{};
    bool batchActive_{};
    std::size_t entryCount_{};
    std::size_t activeLeases_{};
    std::uint64_t deviceUid_{};
    std::uint64_t epoch_{};
    rhi::SubmissionId lastSubmitted_{};
    rhi::SubmissionId completed_{};
};
} // namespace engine

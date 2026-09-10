#pragma once

#include "core/logging/Log.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace engine {

template <typename Resource, typename HandleType> class HandlePool final {
public:
    [[nodiscard]] HandleType insert(Resource resource) { return emplace(std::move(resource)); }

    template <typename... Args> [[nodiscard]] HandleType emplace(Args&&... args) {
        std::uint32_t index{};
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
                Log::fatal("HandlePool", "Handle index overflow");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& slot = slots_[index];
        slot.value.emplace(std::forward<Args>(args)...);
        ++activeCount_;
        return {index, slot.generation};
    }

    [[nodiscard]] Resource* find(HandleType handle) {
        return const_cast<Resource*>(std::as_const(*this).find(handle));
    }

    [[nodiscard]] const Resource* find(HandleType handle) const {
        if (handle.index >= slots_.size())
            return nullptr;
        const Slot& slot = slots_[handle.index];
        return slot.value && slot.generation == handle.generation ? &*slot.value : nullptr;
    }

    [[nodiscard]] bool release(HandleType handle) {
        if (!find(handle))
            return false;
        Slot& slot = slots_[handle.index];
        slot.value.reset();
        incrementGeneration(slot);
        freeList_.push_back(handle.index);
        --activeCount_;
        return true;
    }

    void clear() {
        freeList_.clear();
        freeList_.reserve(slots_.size());
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            if (slot.value) {
                slot.value.reset();
                incrementGeneration(slot);
            }
            freeList_.push_back(index);
        }
        activeCount_ = 0;
    }

    template <typename Function> void forEach(Function&& function) {
        for (Slot& slot : slots_) {
            if (slot.value)
                function(*slot.value);
        }
    }

    template <typename Function> void forEach(Function&& function) const {
        for (const Slot& slot : slots_) {
            if (slot.value)
                function(*slot.value);
        }
    }

    template <typename Function> void forEachHandle(Function&& function) const {
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            const Slot& slot = slots_[index];
            if (slot.value)
                function(HandleType{index, slot.generation}, *slot.value);
        }
    }

    [[nodiscard]] std::size_t size() const { return activeCount_; }
    [[nodiscard]] std::size_t capacity() const { return slots_.size(); }
    [[nodiscard]] std::size_t freeCount() const { return freeList_.size(); }

private:
    struct Slot {
        std::optional<Resource> value;
        std::uint32_t generation{1};
    };

    static void incrementGeneration(Slot& slot) {
        if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
            Log::fatal("HandlePool", "Handle generation overflow");
        }
        ++slot.generation;
    }

    // deque keeps Resource pointers/references stable while new slots are added.
    std::deque<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::size_t activeCount_{};
};

} // namespace engine

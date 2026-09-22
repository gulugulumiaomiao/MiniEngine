#pragma once

#include <atomic>
#include <cstdint>

namespace engine {

// Intrusive atomic reference-count base. Types owned through Ref<T> derive from
// RefCounted so the counter lives inside the object: a single allocation backs
// both the object and its count, and a Ref can be rebuilt safely from a raw
// pointer (for example `this`). The count starts at 0; the first Ref that adopts
// the object takes it to 1, and the object is deleted when it drops back to 0.
// Only Ref<T> mutates the counter, which keeps the ownership protocol in one
// place. Derived types must inherit publicly so Ref<T> can reach this base.
class RefCounted {
public:
    RefCounted() = default;
    virtual ~RefCounted() = default;

    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
    RefCounted(RefCounted&&) = delete;
    RefCounted& operator=(RefCounted&&) = delete;

    [[nodiscard]] std::uint32_t refCount() const {
        return refCount_.load(std::memory_order_relaxed);
    }

private:
    template <typename T> friend class Ref;

    // relaxed is enough to add a reference: ordering only matters once the count
    // reaches zero and the object is destroyed.
    void addRef() const noexcept { refCount_.fetch_add(1, std::memory_order_relaxed); }

    // Returns true when the caller removed the last reference and therefore owns
    // deletion. acq_rel pairs this final decrement with every earlier release so
    // the destructor happens-after all other threads' use of the object.
    [[nodiscard]] bool releaseRef() const noexcept {
        return refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1U;
    }

    mutable std::atomic<std::uint32_t> refCount_{0};
};

} // namespace engine

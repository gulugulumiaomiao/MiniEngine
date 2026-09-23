#pragma once

#include "core/base/RefCounted.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace engine {

// Owning smart pointer for intrusively ref-counted types (see RefCounted).
// Copying shares ownership and increments the count; moving transfers ownership
// and leaves the count unchanged; the managed object is deleted when the last
// Ref drops. Because the count is atomic, operating on distinct Ref copies from
// multiple threads is safe, but concurrent access to the same Ref variable is
// not, matching std::shared_ptr's contract.
template <typename T> class Ref {
    static_assert(std::is_base_of_v<RefCounted, T>,
                  "Ref<T> requires T to derive from engine::RefCounted");

public:
    constexpr Ref() noexcept = default;
    constexpr Ref(std::nullptr_t) noexcept {}

    // Adopts a raw pointer and takes one additional reference. Safe to call with
    // `this` from inside a RefCounted-derived object.
    explicit Ref(T* ptr) noexcept { refFrom(ptr); }

    Ref(const Ref& other) noexcept { refFrom(other.ptr_); }
    Ref(Ref&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    // Implicit upcast, e.g. Ref<Derived> -> Ref<Base>.
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    Ref(const Ref<U>& other) noexcept {
        refFrom(other.ptr_);
    }
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    Ref(Ref<U>&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    Ref& operator=(const Ref& other) noexcept {
        // Cache the source pointer and take its reference before unref() clears
        // ptr_: under self-assignment other.ptr_ aliases ptr_, so reading it after
        // unref() would yield null. Ref-then-unref also keeps aliasing correct
        // without an explicit identity check.
        T* adopted = other.ptr_;
        if (adopted)
            adopted->addRef();
        unref();
        ptr_ = adopted;
        return *this;
    }

    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
            unref();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    Ref& operator=(std::nullptr_t) noexcept {
        unref();
        return *this;
    }

    ~Ref() { unref(); }

    void reset() noexcept { unref(); }

    // Adopts a new raw pointer (taking a reference) and drops the old one.
    void reset(T* ptr) noexcept {
        if (ptr)
            ptr->addRef();
        unref();
        ptr_ = ptr;
    }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] bool valid() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] std::uint32_t useCount() const noexcept { return ptr_ ? ptr_->refCount() : 0U; }

    [[nodiscard]] bool operator==(const Ref& other) const noexcept { return ptr_ == other.ptr_; }
    [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }

private:
    template <typename U> friend class Ref;

    void refFrom(T* ptr) noexcept {
        ptr_ = ptr;
        if (ptr_)
            ptr_->addRef();
    }

    void unref() noexcept {
        if (ptr_ && ptr_->releaseRef())
            delete ptr_;
        ptr_ = nullptr;
    }

    T* ptr_{};
};

// Constructs a T on the heap and returns the first owning Ref (count 1).
template <typename T, typename... Args> [[nodiscard]] Ref<T> makeRef(Args&&... args) {
    return Ref<T>(new T(std::forward<Args>(args)...));
}

// Convenience cast between related ref-counted types (e.g. a resource base and a
// concrete subtype). Uses static_cast, so a downcast must be known to be valid;
// the result is an independent Ref that takes its own reference.
template <typename U, typename T> [[nodiscard]] Ref<U> refCast(const Ref<T>& ref) noexcept {
    static_assert(std::is_base_of_v<T, U> || std::is_base_of_v<U, T>,
                  "refCast requires the types to be related by inheritance");
    return Ref<U>(static_cast<U*>(ref.get()));
}

// Runtime-checked downcast between related ref-counted types. Returns an empty Ref when
// the object's dynamic type is not U. The source Ref keeps the object alive across the
// cast, and a successful result takes its own reference.
template <typename U, typename T> [[nodiscard]] Ref<U> refDynamicCast(const Ref<T>& ref) noexcept {
    return Ref<U>(dynamic_cast<U*>(ref.get()));
}

} // namespace engine

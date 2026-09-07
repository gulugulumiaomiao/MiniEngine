#pragma once

namespace engine {

// CRTP singleton base. The function-local static keeps initialization lazy and
// thread-safe, while each derived class controls construction through friendship.
template <typename T> class Singleton {
public:
    [[nodiscard]] static T& instance() {
        static T value;
        return value;
    }

    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

protected:
    Singleton() = default;
    ~Singleton() = default;
};

} // namespace engine

#pragma once

namespace engine {

class Transfer;

class Transferable {
public:
    virtual ~Transferable() = default;

    [[nodiscard]] virtual bool transfer(Transfer& archive) = 0;
    bool operator==(const Transferable&) const { return true; }

protected:
    Transferable() = default;
    Transferable(const Transferable&) = default;
    Transferable& operator=(const Transferable&) = default;
    Transferable(Transferable&&) noexcept = default;
    Transferable& operator=(Transferable&&) noexcept = default;
};

} // namespace engine

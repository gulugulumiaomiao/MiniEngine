#pragma once

namespace engine {

class Transfer;

class Transferable {
public:
    virtual ~Transferable() = default;

    // 实现自行管理 beginObject({})/endObject()；命名子对象通过 archive.transfer 访问。
    [[nodiscard]] virtual bool transfer(Transfer& archive) = 0;

protected:
    Transferable() = default;
    Transferable(const Transferable&) = default;
    Transferable& operator=(const Transferable&) = default;
    Transferable(Transferable&&) noexcept = default;
    Transferable& operator=(Transferable&&) noexcept = default;
};

} // namespace engine

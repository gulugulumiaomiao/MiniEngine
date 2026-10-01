#include "render/uniform/Ubo.h"

#include "core/math/Math.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cstring>
#include <variant>

namespace engine {
namespace {

// std140 stores a bool as a 4-byte integer.
void writeBool(std::vector<std::byte>& dst, std::uint32_t offset, bool value) {
    const std::uint32_t encoded = value ? 1U : 0U;
    std::memcpy(dst.data() + offset, &encoded, sizeof(encoded));
}

template <typename T>
[[nodiscard]] bool writeValue(std::vector<std::byte>& dst, std::uint32_t offset, const T& value) {
    if (static_cast<std::size_t>(offset) + sizeof(T) > dst.size())
        return false;
    std::memcpy(dst.data() + offset, &value, sizeof(T));
    return true;
}

} // namespace

// ---- UboLayout ----

void UboLayout::addUniform(UboUniform uniform) {
    uniformMap_.emplace(uniform.name, static_cast<std::uint32_t>(uniforms_.size()));
    uniforms_.push_back(std::move(uniform));
}

void UboLayout::finish() {
    Hash64 hash = kFnv1a64OffsetBasis;
    std::uint32_t end = 0;
    for (const UboUniform& uniform : uniforms_) {
        hashAppend(hash, uniform.type);
        hashAppend(hash, uniform.offset);
        hashAppend(hash, uniform.size);
        hash = hashString(uniform.name, hash);
        end = std::max(end, uniform.offset + uniform.size);
    }
    hash_ = hash;
    // std140 rounds the whole block up to a multiple of 16 (the largest base alignment).
    byteSize_ = (end + 15U) & ~15U;
}

const UboUniform* UboLayout::findUniform(std::string_view name) const {
    const auto found = uniformMap_.find(std::string(name));
    return found == uniformMap_.end() ? nullptr : &uniforms_[found->second];
}

// ---- Ubo ----

Ubo::Ubo(rhi::IDevice& device, Ref<UboLayout> layout)
    : device_(&device), deviceUid_(device.uid()), layout_(std::move(layout)) {
    const std::uint32_t byteSize = layout_ ? layout_->byteSize() : 0;
    buffer_ = device_->buffer_allocate_rid(rhi::BufferDesc{
        .size = std::max<std::uint64_t>(16, byteSize),
        .usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDestination,
        .memoryUsage = rhi::MemoryUsage::Upload,
        .debugName = "Ubo",
    });
}

Ubo::~Ubo() {
    // Cross-device safety: only touch the buffer if the device we were created on is still the
    // active one; a recreated device already tore down its pools, so the old RID is meaningless.
    if (buffer_ && device_ && rhi::IDevice::active() &&
        rhi::IDevice::active()->uid() == deviceUid_) {
        device_->buffer_destroy(buffer_);
    }
    buffer_ = {};
    cpu_.clear();
    cpu_.shrink_to_fit();
}

void Ubo::create() {
    if (created_)
        return;
    device_->buffer_allocate_memory(buffer_);
    const std::uint32_t byteSize = layout_ ? layout_->byteSize() : 0;
    cpu_.assign(std::max<std::uint32_t>(16, byteSize), std::byte{0});
    created_ = true;
    dirty_ = true;
}

bool Ubo::setUniform(std::string_view name, const ShaderValue& value) {
    if (!layout_ || !created_ || cpu_.empty())
        return false;
    const UboUniform* uniform = layout_->findUniform(name);
    if (!uniform)
        return false;

    bool ok = false;
    if (const auto* v = std::get_if<float>(&value)) {
        ok = uniform->type == UboUniformType::Float && writeValue(cpu_, uniform->offset, *v);
    } else if (const auto* v = std::get_if<bool>(&value)) {
        if (uniform->type == UboUniformType::Bool &&
            static_cast<std::size_t>(uniform->offset) + sizeof(std::uint32_t) <= cpu_.size()) {
            writeBool(cpu_, uniform->offset, *v);
            ok = true;
        }
    } else if (const auto* v = std::get_if<math::Vec2>(&value)) {
        ok = uniform->type == UboUniformType::Vec2 && writeValue(cpu_, uniform->offset, *v);
    } else if (const auto* v = std::get_if<math::Vec3>(&value)) {
        ok = uniform->type == UboUniformType::Vec3 && writeValue(cpu_, uniform->offset, *v);
    } else if (const auto* v = std::get_if<math::Vec4>(&value)) {
        ok = uniform->type == UboUniformType::Vec4 && writeValue(cpu_, uniform->offset, *v);
    } else if (const auto* v = std::get_if<math::Mat44>(&value)) {
        ok = uniform->type == UboUniformType::Mat4 && writeValue(cpu_, uniform->offset, *v);
    }
    if (ok)
        dirty_ = true;
    return ok;
}

void Ubo::setRawBytes(std::span<const std::byte> data) {
    if (!created_ || cpu_.empty())
        return;
    const std::size_t count = std::min(data.size(), cpu_.size());
    std::memcpy(cpu_.data(), data.data(), count);
    dirty_ = true;
}

void Ubo::upload() {
    if (!dirty_ || !created_ || !buffer_ || cpu_.empty())
        return;
    device_->buffer_upload(buffer_, cpu_);
    dirty_ = false;
}

} // namespace engine

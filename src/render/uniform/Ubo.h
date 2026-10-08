#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/math/hash.h"
#include "render/shader/Shader.h"         // ShaderValue, UboUniformType
#include "render/uniform/UniformResource.h"
#include "rhi/api/ResourceDesc.h"         // rhi::RID, BufferDesc, ResourceType

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// One uniform-block member as reflected from SPIR-V: its name, GLSL type and std140 byte
// offset/size within the block.
struct UboUniform {
    std::string name;
    UboUniformType type{UboUniformType::Float};
    std::uint32_t offset{};
    std::uint32_t size{};
};

// CPU-side description of a uniform block: members in declaration order, a name -> index map, a
// content hash and the 16-aligned byte size. Pure data (no GPU handle, no RID), so a single
// UboLayout is shared by every Ubo instantiated from the same reflected block. Built by SPIR-V
// reflection: addUniform() per member in order, then finish() once.
class UboLayout final : public RefCounted {
public:
    void addUniform(UboUniform uniform);
    void finish();

    [[nodiscard]] Hash64 hash() const { return hash_; }
    [[nodiscard]] std::uint32_t byteSize() const { return byteSize_; }
    [[nodiscard]] const std::vector<UboUniform>& uniforms() const { return uniforms_; }
    [[nodiscard]] const UboUniform* findUniform(std::string_view name) const;

private:
    std::vector<UboUniform> uniforms_;
    std::unordered_map<std::string, std::uint32_t> uniformMap_;
    Hash64 hash_{};
    std::uint32_t byteSize_{};
};

// A concrete uniform-buffer instance: a CPU staging buffer laid out per its UboLayout plus the GPU
// buffer RID it uploads into. setUniform() writes the CPU buffer and marks dirty; upload() pushes it
// to the GPU when dirty. It is also a UniformResource (bindingType == UniformBuffer) so it can
// occupy a descriptor-set binding slot.
//
// Two-phase construction mirrors the device buffer lifecycle: the constructor reserves the buffer
// RID, create() allocates GPU memory and sizes the CPU buffer.
class Ubo final : public UniformResource {
public:
    explicit Ubo(Ref<UboLayout> layout);
    ~Ubo() override;
    Ubo(const Ubo&) = delete;
    Ubo& operator=(const Ubo&) = delete;
    Ubo(Ubo&&) = delete;
    Ubo& operator=(Ubo&&) = delete;

    void create();

    [[nodiscard]] UniformResourceType resourceType() const override {
        return UniformResourceType::Ubo;
    }
    [[nodiscard]] rhi::ResourceType bindingType() const override {
        return rhi::ResourceType::UniformBuffer;
    }

    // Writes `value` into the named member's slot in the CPU buffer. Returns false if the name is
    // unknown, the buffer is not created yet, or the value's variant does not match the member's
    // UboUniformType; marks dirty on success.
    bool setUniform(std::string_view name, const ShaderValue& value);
    // Bulk-writes a pre-packed std140 blob (e.g. SceneDrawData / a material's uniformBytes) into the
    // CPU buffer, truncating to the buffer size; marks dirty. For blocks whose payload the owner
    // already lays out to match the reflected UboLayout byte-for-byte.
    void setRawBytes(std::span<const std::byte> data);
    // Pushes the CPU buffer to the GPU when dirty (and created); clears the dirty flag.
    void upload();

    [[nodiscard]] const Ref<UboLayout>& layout() const { return layout_; }
    [[nodiscard]] rhi::RID buffer() const { return buffer_; }
    [[nodiscard]] bool dirty() const { return dirty_; }

private:
    std::uint64_t deviceUid_{};
    Ref<UboLayout> layout_;
    rhi::RID buffer_;
    std::vector<std::byte> cpu_;
    bool dirty_{true};
    bool created_{};
};

} // namespace engine

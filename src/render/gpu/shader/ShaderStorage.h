#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/shader/ShaderStorageCache.h"
#include "render/gpu/shader/ShaderStorageEntry.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID / ShaderStage

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {

class ShaderStorageFactory;

namespace rhi {
class IDevice;
}

class ShaderStorage final : public Singleton<ShaderStorage> {
public:
    ~ShaderStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] RID getOrCreateProgram(const Shader& shader,
                                         const ShaderPass& pass,
                                         const ShaderVariantKey& variant);
    [[nodiscard]] const ShaderProgram& resolveProgram(RID handle) const;
    [[nodiscard]] const CompiledShader& resolveCompiled(RID handle) const;
    [[nodiscard]] rhi::RID resolve(RID handle);
    [[nodiscard]] std::vector<CompiledShaderId> invalidateChanged(std::uint64_t retireSerial);
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void release(RID handle);
    void shutdown();
    [[nodiscard]] bool initialized() const { return compilePipeline_ != nullptr; }

private:
    struct RetiredShader {
        ShaderStorageEntry resource;
        std::uint64_t serial{};
    };

    friend class Singleton<ShaderStorage>;
    ShaderStorage();

    std::unique_ptr<ShaderCompilePipeline> compilePipeline_;
    rhi::IDevice* device_{};
    ShaderStorageCache cache_;
    std::unique_ptr<ShaderStorageFactory> factory_;
    std::vector<RetiredShader> retired_;
};

} // namespace engine

#define SHADER_STORAGE (::engine::ShaderStorage::instance())

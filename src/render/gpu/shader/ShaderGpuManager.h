#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/shader/ShaderModuleCache.h"
#include "render/gpu/shader/ShaderModuleGpuResource.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {

class ShaderModuleGpuFactory;

namespace rhi {
class IDevice;
}

class ShaderGpuManager final : public Singleton<ShaderGpuManager> {
public:
    ~ShaderGpuManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] ShaderProgramHandle getOrCreateProgram(const Shader& shader,
                                                         const ShaderPass& pass,
                                                         const ShaderVariantKey& variant);
    [[nodiscard]] const ShaderProgram& resolveProgram(ShaderProgramHandle handle) const;
    [[nodiscard]] const CompiledShader& resolveCompiled(CompiledShaderHandle handle) const;
    [[nodiscard]] rhi::ShaderHandle resolve(CompiledShaderHandle handle);
    [[nodiscard]] std::vector<CompiledShaderId> invalidateChanged(std::uint64_t retireSerial);
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void shutdown();
    [[nodiscard]] bool initialized() const { return compilePipeline_ != nullptr; }

private:
    struct RetiredShader {
        ShaderModuleGpuResource resource;
        std::uint64_t serial{};
    };

    friend class Singleton<ShaderGpuManager>;
    ShaderGpuManager();

    std::unique_ptr<ShaderCompilePipeline> compilePipeline_;
    ShaderModuleCache cache_;
    std::unique_ptr<ShaderModuleGpuFactory> factory_;
    std::vector<RetiredShader> retired_;
};

} // namespace engine

#define SHADER_GPU_MANAGER (::engine::ShaderGpuManager::instance())

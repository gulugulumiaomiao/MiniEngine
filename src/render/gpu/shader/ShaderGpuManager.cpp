#include "render/gpu/shader/ShaderGpuManager.h"

#include "core/base/BuildConfig.h"
#include "core/logging/Log.h"
#include "render/gpu/shader/ShaderModuleGpuFactory.h"
#include "render/shader/Shader.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace engine {

ShaderGpuManager::ShaderGpuManager() = default;
ShaderGpuManager::~ShaderGpuManager() = default;

bool ShaderGpuManager::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("ShaderGpuManager", "Manager is already initialized");
        return false;
    }
    ShaderCompilePipelineConfig config;
#if defined(MINI_PUBLISH)
    config.mode = ShaderCompileMode::PackagedRuntime;
#else
    config.mode = ShaderCompileMode::DevelopmentRuntime;
#endif
    config.preprocessorConfig.includeSearchPaths = {VirtualPath{"asset://shaders/include"}};
    config.compilerOptions.compilerVersion = MINI_GLSLC_EXECUTABLE;
#if !defined(NDEBUG)
    config.compilerOptions.optimization = ShaderOptimization::Debug;
#else
    config.compilerOptions.optimization = ShaderOptimization::Release;
#endif
    compilePipeline_ = std::make_unique<ShaderCompilePipeline>(std::move(config));
    factory_ = std::make_unique<ShaderModuleGpuFactory>(device);
    return true;
}

ShaderProgramHandle ShaderGpuManager::getOrCreateProgram(const Shader& shader,
                                                         const ShaderPass& pass,
                                                         const ShaderVariantKey& variant) {
    return initialized() ? compilePipeline_->getOrCreate(shader, pass, variant)
                         : ShaderProgramHandle{};
}

const ShaderProgram& ShaderGpuManager::resolveProgram(ShaderProgramHandle handle) const {
    return compilePipeline_->resolve(handle);
}

const CompiledShader& ShaderGpuManager::resolveCompiled(CompiledShaderHandle handle) const {
    return compilePipeline_->resolve(handle);
}

rhi::ShaderHandle ShaderGpuManager::resolve(CompiledShaderHandle handle) {
    if (!initialized())
        return {};
    const CompiledShader& shader = compilePipeline_->resolve(handle);
    if (const ShaderModuleGpuResource* cached = cache_.find(shader.id))
        return cached->shader;
    ShaderModuleGpuResource created;
    if (!factory_->create(shader, created))
        return {};
    auto stored = cache_.store(shader.id, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    return stored.stored->shader;
}

std::vector<CompiledShaderId> ShaderGpuManager::invalidateChanged(std::uint64_t retireSerial) {
    if (!initialized())
        return {};
    std::vector<CompiledShaderId> changed = compilePipeline_->invalidateChanged();
    invalidate(changed, retireSerial);
    return changed;
}

void ShaderGpuManager::invalidate(std::span<const CompiledShaderId> shaders,
                                  std::uint64_t retireSerial) {
    if (!initialized() || shaders.empty())
        return;
    auto modules =
        cache_.extractIf([&](CompiledShaderId id, const ShaderModuleGpuResource&) {
            return std::ranges::find(shaders, id) != shaders.end();
        });
    for (auto& entry : modules)
        retired_.push_back({std::move(entry.second), retireSerial});
}

void ShaderGpuManager::collect(std::uint64_t completedSerial) {
    if (!initialized())
        return;
    std::erase_if(retired_, [&](RetiredShader& retired) {
        if (retired.serial > completedSerial)
            return false;
        factory_->release(retired.resource);
        return true;
    });
}

void ShaderGpuManager::shutdown() {
    if (!initialized())
        return;
    for (RetiredShader& retired : retired_)
        factory_->release(retired.resource);
    retired_.clear();
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    factory_.reset();
    compilePipeline_->clear();
    compilePipeline_.reset();
}

} // namespace engine
